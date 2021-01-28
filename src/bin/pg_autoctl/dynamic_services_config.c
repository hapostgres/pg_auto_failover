/*
 * src/bin/pg_autoctl/dynamic_services_config.h
 *     Dynamic services configuration data structure and function definitions
 *
 * XXX: Add copywrite as required
 */

#include "postgres_fe.h"

#include "config.h"
#include "defaults.h"
#include "dynamic_services_config.h"
#include "env_utils.h"
#include "log.h"
#include "monitor.h"
#include "parson.h"
#include "service_monitor.h"
#include "supervisor.h"

/* Helper definitions */
#define SERVICES_CONFIG_FILENAME "services.cfg"

/*
 * Helper structs for json parsing
 */
typedef struct DynamicConfigEntry
{
	bool enabled;
	char role[NAMEDATALEN];
	char name[NAMEDATALEN];
} DynamicConfigEntry;

typedef struct DynamicServicesConfig
{
	int entryCount;
	DynamicConfigEntry entries[MAX_SERVICES];
} DynamicServicesConfig;

/*
 * Holds a read only array of services that can be considered as dynamic.
 * For a matching role, the user can use the template to fill in the service
 * definition for a fullfletched running service if required.
 */
ServiceArray const serviceTemplates = {
	.array = {
#if 0

		/* Example of a service template */
		.role = PGBOUNCER_ROLE,
		.name = "placeholder",
		.policy = RP_PERMANENT,
		.startFunction = service_pgbouncer_start,
		.context = NULL, /* Be explicit */
#endif
	},
	.serviceCount = 0,
};

static bool dynamic_services_template_from_role(const char *role,
												Service *const service);
static bool dynamic_services_get_config_filename(char *servicesFilename);
static void dynamic_services_edit_json_array(JSON_Array *jsonArray,
											 const ServiceArray *services,
											 bool enabledValue);

/*
 * dynamic_services_role_exists checks wether the role exists in the template
 * services definitions. The user may pass in a service to be filled in with
 * the template if needed.
 *
 * Returns true if the role is defined, false otherwise.
 */
static bool
dynamic_services_template_from_role(const char *role,
									Service *const service)
{
	for (int serviceIndex = 0;
		 serviceIndex < serviceTemplates.serviceCount;
		 serviceIndex++)
	{
		const Service *const template = &(serviceTemplates.array[serviceIndex]);
		if (strcmp(role, template->role) == 0)
		{
			if (service != NULL)
			{
				*service = *template;
			}
			return true;
		}
	}

	return false;
}


/*
 * dynamic_services_get_config_filename constructs the complete, absolute path,
 * filename used for dynamic services configuration. It is heavily relying on
 * environmental values because one main user of the file is the supervisor who
 * does not have any knowledge of paths. Any other users should have set the
 * environment accordingly before calling dynamic_services_* functions.
 *
 * It is the caller's responsibility that servicesFilename is large enough.
 *
 * Returns true on success. Otherwise the contents of servicesFilename are not
 * to be trusted.
 */
static bool
dynamic_services_get_config_filename(char *servicesFilename)
{
	char home[MAXPGPATH];
	char pgdata[MAXPGPATH];

	if (!get_env_pgdata(pgdata))
	{
		log_error("BUG: PGDATA env not set");
		return false;
	}

	/* build_xdg_path call exit if it fails to find HOME */
	if (!get_env_copy("HOME", home, MAXPGPATH))
	{
		log_error("BUG: HOME env not set");
		return false;
	}

	if (!build_xdg_path(servicesFilename,
						XDG_CONFIG,
						pgdata,
						SERVICES_CONFIG_FILENAME))
	{
		/* It has already logged why */
		return false;
	}

	return true;
}


/*
 * dynamic_services_edit_json_array edits a valid jsonArray to contain the
 * service entries passed. The services array passed is not meant to be
 * exchaustive and any existing entries in the jsonArray not present in the
 * provided services argument, will not be altered. This makes it safe for
 * callers of this function to be clients that have limited scope.
 *
 * Parameters
 *		jsonArray: an editable jsonArray to hold the entries, can be empty.
 *		services:  a read only ServiceArray which holds the services to be
 *				   edited, can be NULL
 *		enabledValue: a boolean which sets the 'enabled' value in configuration
 */
static void
dynamic_services_edit_json_array(JSON_Array *jsonArray,
								 const ServiceArray *services,
								 bool enabledValue)
{
	int enabled = enabledValue ? 1 : 0; /* json is a bit silly */
	int arrayCount = json_array_get_count(jsonArray);

	/* Nothing to be done */
	if (services == NULL)
	{
		return;
	}

	for (int serviceIndex = 0;
		 serviceIndex < services->serviceCount;
		 serviceIndex++)
	{
		const Service *service = &(services->array[serviceIndex]);
		JSON_Value *jsonEntryValue;
		JSON_Object *jsonEntryObject;
		bool inlineEdit = false;

		if (!dynamic_services_template_from_role(service->role, NULL))
		{
			log_info("Service role %s is not defined as dynamic",
					 service->role);
			continue;
		}

		/* Change inline if existing */
		for (int i = 0; i < arrayCount; i++)
		{
			const char *role;
			const char *name;

			jsonEntryObject = json_array_get_object(jsonArray, i);
			role = json_object_get_string(jsonEntryObject, "role");
			name = json_object_get_string(jsonEntryObject, "name");
			if (role == NULL || strcmp(role, service->role) != 0 ||
				name == NULL || strcmp(name, service->name) != 0)
			{
				continue;
			}

			inlineEdit = true;
			if (json_object_get_boolean(jsonEntryObject, "enabled") != enabled)
			{
				json_object_remove(jsonEntryObject, "enabled");
				json_object_set_boolean(jsonEntryObject, "enabled", enabled);
			}
			break;
		}

		if (inlineEdit)
		{
			continue;
		}

		/* create a new entry */
		jsonEntryValue = json_value_init_object();
		jsonEntryObject = json_value_get_object(jsonEntryValue);
		json_object_set_string(jsonEntryObject, "role", service->role);
		json_object_set_string(jsonEntryObject, "name", service->name);
		json_object_set_boolean(jsonEntryObject, "enabled", enabled);
		json_array_append_value(jsonArray, jsonEntryValue);

		/* should not free jsonEntryValue here */
	}
}


/*
 * dynamic_services_read_config
 */
bool
dynamic_services_read_config(ServiceArray *enabledServices)
{
	JSON_Value *jsonValue;
	const JSON_Array *jsonArray;
	const JSON_Object *jsonObject;
	char servicesFilename[MAXPGPATH];
	DynamicServicesConfig dynConf = { 0 };

	if (!dynamic_services_get_config_filename(servicesFilename))
	{
		/* it has already logged why */
		return false;
	}

	jsonValue = json_parse_file_with_comments(servicesFilename);
	if (jsonValue == NULL)
	{
		if (!file_exists(servicesFilename))
		{
			log_error("Failed to parse json format in file %s", servicesFilename);
		}
		return false;
	}

	jsonObject = json_value_get_object(jsonValue);
	jsonArray = json_object_get_array(jsonObject, "services");
	if (jsonArray == NULL)
	{
		log_error("Corrupted services configuration file %s", servicesFilename);
		json_value_free(jsonValue);
		return false;
	}

	/*
	 * Do not get tempted to limit array count to MAX_SERVICES because that
	 * limit applies only to the enabled services, there can as many disabled
	 * services as desired.
	 */
	for (int i = 0; i < json_array_get_count(jsonArray); i++)
	{
		DynamicConfigEntry *entry = &(dynConf.entries[dynConf.entryCount]);
		JSON_Object *jsonObject = json_array_get_object(jsonArray, i);
		const char *role;
		const char *name;
		size_t buflen;

		/* Skip not strictly enabled services */
		if (json_object_get_boolean(jsonObject, "enabled") != 1)
		{
			continue;
		}

		role = json_object_get_string(jsonObject, "role");
		buflen = json_object_get_string_len(jsonObject, "role");
		if (buflen == 0 || buflen >= sizeof(entry->role))
		{
			continue;
		}

		name = json_object_get_string(jsonObject, "name");
		buflen = json_object_get_string_len(jsonObject, "name");
		if (buflen == 0 || buflen >= sizeof(entry->name))
		{
			continue;
		}

		strlcpy(entry->role, role, sizeof(entry->role));
		strlcpy(entry->name, name, sizeof(entry->name));

		/* not strictly needed but be a good citizen */
		entry->enabled = true;

		if (dynConf.entryCount == (MAX_SERVICES - 1))
		{
			break;
		}
		dynConf.entryCount++;
	}
	json_value_free(jsonValue);

	/*
	 * Now create a service struct for each of the enabled services based on
	 * configured roles.
	 */
	for (int i = 0; i < dynConf.entryCount; i++)
	{
		const DynamicConfigEntry *entry = &(dynConf.entries[i]);
		Service templateService = { 0 };
		Service *enabledService =
			&(enabledServices->array[enabledServices->serviceCount]);

		if (!dynamic_services_template_from_role(entry->role, &templateService))
		{
			log_debug("Skipping entry for role %s", entry->role);
			continue;
		}

		strlcpy(enabledService->role, entry->role, sizeof(enabledService->role));
		strlcpy(enabledService->name, entry->name, sizeof(enabledService->name));
		enabledService->policy = templateService.policy;
		enabledService->startFunction = templateService.startFunction;
		enabledService->context = NULL; /* be explicit */
		enabledServices->serviceCount++;
	}

	return true;
}


/*
 * dynamic_services_write_config constructs entries from the provided
 * ServiceArrays and includes them to the configuration file.
 *
 * If the file does not exist, then it creates it.
 * If the file does exist, then it modifies it.
 *
 * Either of the ServiceArrays can be NULL.
 * It will go over each of the Services and will try to find the corresponding
 * entry in the configuration file. If it finds it, then it will modify it
 * inline if needed. If it does not find it will add it.
 *
 * If an entry exists in both ServiceArrays, then only the disabled one will be
 * added.
 *
 * The function is written with the explicit assumption that the caller, usually
 * a client, does not have a wholistic knowledge of all the services running and
 * wants to modify only the service(s) the client is responsible for.
 */
bool
dynamic_services_write_config(const ServiceArray *const enabledServices,
							  const ServiceArray *const disabledServices)
{
	JSON_Array *jsonArray;
	JSON_Object *jsonObject;
	JSON_Value *jsonValue;
	char servicesFilename[MAXPGPATH];

	if (enabledServices == NULL && disabledServices == NULL)
	{
		return false;
	}

	if (!dynamic_services_get_config_filename(servicesFilename))
	{
		/* it has already logged why */
		return false;
	}

	jsonValue = json_parse_file_with_comments(servicesFilename);
	if (jsonValue == NULL)
	{
		jsonValue = json_value_init_object();
	}

	jsonObject = json_value_get_object(jsonValue);
	jsonArray = json_object_get_array(jsonObject, "services");
	if (jsonArray == NULL)
	{
		json_object_set_value(jsonObject, "services", json_value_init_array());
		jsonArray = json_object_get_array(jsonObject, "services");
	}

	dynamic_services_edit_json_array(jsonArray, enabledServices, true);
	dynamic_services_edit_json_array(jsonArray, disabledServices, false);

	json_serialize_to_file(jsonValue, servicesFilename);
	json_value_free(jsonValue);

	return true;
}
