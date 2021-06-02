/*
 * src/bin/pg_autoctl/monitor_config.c
 *     Monitor configuration functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdbool.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "config.h"
#include "defaults.h"
#include "ini_file.h"
#include "ipaddr.h"
#include "archiver.h"
#include "archiver_config.h"
#include "log.h"
#include "pgctl.h"

#define OPTION_AUTOCTL_ROLE(config) \
	make_strbuf_option_default("pg_autoctl", "role", NULL, true, NAMEDATALEN, \
							   config->role, ARCHIVER_ROLE)

#define OPTION_AUTOCTL_MONITOR(config) \
	make_strbuf_option("pg_autoctl", "monitor", "monitor", false, MAXCONNINFO, \
					   config->monitor_pguri)

#define OPTION_AUTOCTL_DIRECTORY(config) \
	make_strbuf_option("pg_autoctl", "directory", "directory", \
					   true, MAXPGPATH, config->directory)

#define OPTION_AUTOCTL_NAME(config) \
	make_strbuf_option_default("pg_autoctl", "name", "name", \
							   false, _POSIX_HOST_NAME_MAX, \
							   config->name, "")

#define OPTION_AUTOCTL_HOSTNAME(config) \
	make_strbuf_option("pg_autoctl", "hostname", "hostname", \
					   false, _POSIX_HOST_NAME_MAX, config->hostname)

#define SET_INI_OPTIONS_ARRAY(config) \
	{ \
		OPTION_AUTOCTL_ROLE(config), \
		OPTION_AUTOCTL_MONITOR(config), \
		OPTION_AUTOCTL_DIRECTORY(config), \
		OPTION_AUTOCTL_HOSTNAME(config), \
		INI_OPTION_LAST \
	}


/*
 * archiver_config_set_pathnames_from_pgdata sets the config pathnames from its
 * pgSetup.pgdata field, which must have already been set when calling this
 * function.
 */
bool
archiver_config_set_pathnames_from_directory(ArchiverConfig *config)
{
	if (IS_EMPTY_STRING_BUFFER(config->directory))
	{
		/* developer error */
		log_error("BUG: archiver_config_set_pathnames_from_pgdata: "
				  "empty directory");
		return false;
	}

	if (!SetConfigFilePath(&(config->pathnames), config->directory))
	{
		log_fatal("Failed to set configuration filename from directory \"%s\","
				  " see above for details.", config->directory);
		return false;
	}

	if (!SetStateFilePath(&(config->pathnames), config->directory))
	{
		log_fatal("Failed to set state filename from directory \"%s\","
				  " see above for details.", config->directory);
		return false;
	}

	if (!SetPidFilePath(&(config->pathnames), config->directory))
	{
		log_fatal("Failed to set pid filename from directory \"%s\","
				  " see above for details.", config->directory);
		return false;
	}
	return true;
}


/*
 * archiver_config_init initializes a ArchiverConfig with the default values.
 */
void
archiver_config_init(ArchiverConfig *config)
{
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	if (!ini_validate_options(archiverOptions))
	{
		log_error("Please review your setup options per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!archiver_config_update_with_absolute_pgdata(config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}
}


/*
 * archiver_config_read_file overrides values in given ArchiverConfig with
 * whatever values are read from given configuration filename.
 */
bool
archiver_config_read_file(ArchiverConfig *config)
{
	const char *filename = config->pathnames.config;
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	log_debug("Reading configuration from %s", filename);

	if (!read_ini_file(filename, archiverOptions))
	{
		log_error("Failed to parse configuration file \"%s\"", filename);
		return false;
	}

	return true;
}


/*
 * archiver_config_write_file writes the current values in given KeeperConfig to
 * filename.
 */
bool
archiver_config_write_file(ArchiverConfig *config)
{
	const char *filePath = config->pathnames.config;

	log_trace("archiver_config_write_file \"%s\"", filePath);

	FILE *fileStream = fopen_with_umask(filePath, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	bool success = archiver_config_write(fileStream, config);

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return success;
}


/*
 * archiver_config_write write the current config to given STREAM.
 */
bool
archiver_config_write(FILE *stream, ArchiverConfig *config)
{
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return write_ini_to_stream(stream, archiverOptions);
}


/*
 * archiver_config_to_json populates given jsRoot object with the INI
 * configuration sections as JSON objects, and the options as keys to those
 * objects.
 */
bool
archiver_config_to_json(ArchiverConfig *config, JSON_Value *js)
{
	JSON_Object *jsRoot = json_value_get_object(js);
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return ini_to_json(jsRoot, archiverOptions);
}


/*
 * archiver_config_log_settings outputs a DEBUG line per each config parameter
 * in the given ArchiverConfig.
 */
void
archiver_config_log_settings(ArchiverConfig *config)
{
	log_debug("pg_autoctl.directory: %s", config->directory);
	log_debug("pg_autoctl.monitor_pguri: %s", config->monitor_pguri);
	log_debug("pg_autoctl.name: %s", config->name);
	log_debug("pg_autoctl.hostname: %s", config->hostname);
}


/*
 * archiver_config_merge_options merges any option setup in options into config.
 * Its main use is to override configuration file settings with command line
 * options.
 */
bool
archiver_config_merge_options(ArchiverConfig *config, ArchiverConfig *options)
{
	IniOption archiverConfigOptions[] = SET_INI_OPTIONS_ARRAY(config);
	IniOption archiverOptionsOptions[] = SET_INI_OPTIONS_ARRAY(options);

	if (ini_merge(archiverConfigOptions, archiverOptionsOptions))
	{
		return archiver_config_write_file(config);
	}
	return false;
}


/*
 * archiver_config_get_setting returns the current value of the given option
 * "path" (thats a section.option string). The value is returned in the
 * pre-allocated value buffer of size size.
 */
bool
archiver_config_get_setting(ArchiverConfig *config,
							const char *path,
							char *value, size_t size)
{
	const char *filename = config->pathnames.config;
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return ini_get_setting(filename, archiverOptions, path, value, size);
}


/*
 * archiver_config_set_setting sets the setting identified by "path"
 * (section.option) to the given value. The value is passed in as a string,
 * which is going to be parsed if necessary.
 */
bool
archiver_config_set_setting(ArchiverConfig *config,
							const char *path,
							char *value)
{
	const char *filename = config->pathnames.config;
	IniOption archiverOptions[] = SET_INI_OPTIONS_ARRAY(config);

	if (ini_set_setting(filename, archiverOptions, path, value))
	{
		return true;
	}

	return false;
}


/*
 * archiver_config_update_with_absolute_pgdata verifies that the pgdata path
 * is an absolute one.
 *
 * If not, the config->directory is updated and we rewrite the archiver config
 * file.
 */
bool
archiver_config_update_with_absolute_pgdata(ArchiverConfig *config)
{
	if (!directory_exists(config->directory))
	{
		int mode = 0700;

		if (pg_mkdir_p(config->directory, mode) == -1)
		{
			log_error("Failed to ensure empty directory \"%s\": %m",
					  config->directory);
			return false;
		}
	}

	if (normalize_filename(config->directory, config->directory, MAXPGPATH))
	{
		if (!archiver_config_write_file(config))
		{
			/* errors have already been logged */
			return false;
		}
	}

	return true;
}


/*
 * archiver_config_print_from_file prints to stdout the contents of the given
 * archiver configuration file, either in a human formatted way, or in pretty
 * printed JSON.
 */
bool
archiver_config_print_from_file(const char *pathname,
								bool outputContents,
								bool outputJSON)
{
	ArchiverConfig config = { 0 };

	strlcpy(config.pathnames.config, pathname, MAXPGPATH);

	if (!archiver_config_read_file(&config))
	{
		return false;
	}

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();

		if (outputContents)
		{
			if (!archiver_config_to_json(&config, js))
			{
				log_error("Failed to serialize configuration to JSON");
				return false;
			}
		}
		else
		{
			JSON_Object *jsObj = json_value_get_object(js);

			json_object_set_string(jsObj, "pathname", pathname);
		}

		/* we have the config as a JSON object, print it out now */
		(void) pprint_json(js);
	}
	else
	{
		if (outputContents)
		{
			return fprint_file_contents(pathname);
		}
		else
		{
			fformat(stdout, "%s\n", pathname);
		}
	}

	return true;
}
