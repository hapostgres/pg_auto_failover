/*
 * src/bin/pg_autoctl/azure_config.c
 *     Configuration file for azure QA/test environments
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <pwd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "azure.h"
#include "azure_config.h"
#include "cli_root.h"
#include "config.h"
#include "defaults.h"
#include "ini_file.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

#define OPTION_AZURE_PREFIX(config) \
	make_strbuf_option_default("az", "prefix", "prefix", true, NAMEDATALEN, \
							   config->prefix, "ha-demo-")

#define OPTION_AZURE_REGION(config) \
	make_strbuf_option_default("az", "region", "region", true, NAMEDATALEN, \
							   config->region, "paris")

#define OPTION_AZURE_LOCATION(config) \
	make_strbuf_option_default("az", "location", "location", true, NAMEDATALEN, \
							   config->location, "francecentral")

#define OPTION_AZURE_MONITOR(config) \
	make_int_option_default("group", "monitor", "monitor", true, \
							&(config->monitor), 1)

#define OPTION_AZURE_NODES(config) \
	make_int_option_default("group", "nodes", "nodes", true, \
							&(config->nodes), 2)

#define OPTION_AZURE_APP_NODES(config) \
	make_int_option_default("group", "appNodes", NULL, true, \
							&(config->appNodes), 0)

#define OPTION_AZURE_GROUP(config) \
	make_strbuf_option("resource", "group", NULL, false, NAMEDATALEN, \
					   config->group)

#define OPTION_AZURE_VNET(config) \
	make_strbuf_option("resource", "vnet", NULL, false, NAMEDATALEN, \
					   config->vnet)

#define OPTION_AZURE_NSG(config) \
	make_strbuf_option("resource", "nsg", NULL, false, NAMEDATALEN, \
					   config->nsg)

#define OPTION_AZURE_RULE(config) \
	make_strbuf_option("resource", "rule", NULL, false, NAMEDATALEN, \
					   config->rule)

#define OPTION_AZURE_SUBNET(config) \
	make_strbuf_option("resource", "subnet", NULL, false, NAMEDATALEN, \
					   config->subnet)


#define SET_INI_OPTIONS_ARRAY(config) \
	{ \
		OPTION_AZURE_PREFIX(config), \
		OPTION_AZURE_REGION(config), \
		OPTION_AZURE_LOCATION(config), \
		OPTION_AZURE_MONITOR(config), \
		OPTION_AZURE_NODES(config), \
		OPTION_AZURE_APP_NODES(config), \
		OPTION_AZURE_GROUP(config), \
		OPTION_AZURE_VNET(config), \
		OPTION_AZURE_NSG(config), \
		OPTION_AZURE_RULE(config), \
		OPTION_AZURE_SUBNET(config), \
		INI_OPTION_LAST \
	}


/*
 * azure_config_read_file reads our azure configuration from an INI
 * configuration file that has been previously created by our pg_autoctl do
 * azure commands.
 */
bool
azure_config_read_file(AzureRegionResources *azRegion)
{
	IniOption azureOptions[] = SET_INI_OPTIONS_ARRAY(azRegion);

	log_debug("Reading azure configuration from %s", azRegion->filename);

	if (!read_ini_file(azRegion->filename, azureOptions))
	{
		log_error("Failed to parse azure configuration file \"%s\"",
				  azRegion->filename);
		return false;
	}

	return true;
}


/*
 * azure_config_write write the current azure config to given STREAM.
 */
bool
azure_config_write(FILE *stream, AzureRegionResources *azRegion)
{
	IniOption azureOptions[] = SET_INI_OPTIONS_ARRAY(azRegion);

	return write_ini_to_stream(stream, azureOptions);
}


/*
 * azure_config_write_file writes the current values in given azRegion to the
 * given filename.
 */
bool
azure_config_write_file(AzureRegionResources *azRegion)
{
	bool success = false;
	FILE *fileStream = NULL;

	log_trace("azure_config_write_file \"%s\"", azRegion->filename);

	fileStream = fopen_with_umask(azRegion->filename, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	success = azure_config_write(fileStream, azRegion);

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", azRegion->filename);
		return false;
	}

	return success;
}


/*
 * azure_config_prepare prepares the names we use for the different
 * Azure network objects that we need: vnet, nsg, nsgrule, subnet.
 */
void
azure_config_prepare(AzureOptions *options, AzureRegionResources *azRegion)
{
	/* build the path to our configuration file on-disk */
	if (!build_xdg_path(azRegion->filename, XDG_CONFIG, ".", "azure.cfg"))
	{
		log_fatal("Failed to prepare azure configuration filename");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	strlcpy(azRegion->prefix, options->prefix, sizeof(azRegion->prefix));
	strlcpy(azRegion->region, options->region, sizeof(azRegion->region));
	strlcpy(azRegion->location, options->location, sizeof(azRegion->location));

	sformat(azRegion->group, sizeof(azRegion->group),
			"%s-%s",
			options->prefix,
			options->region);

	/*
	 * Prepare our Azure object names from the group objects: vnet, subnet,
	 * nsg, nsg rule.
	 */
	sformat(azRegion->vnet, sizeof(azRegion->vnet), "%s-net", azRegion->group);
	sformat(azRegion->nsg, sizeof(azRegion->nsg), "%s-nsg", azRegion->group);

	sformat(azRegion->rule, sizeof(azRegion->rule),
			"%s-ssh-and-pg", azRegion->group);

	sformat(azRegion->subnet, sizeof(azRegion->subnet),
			"%s-subnet", azRegion->group);

	/* transform --monitor and --no-app booleans into integer counts */
	azRegion->monitor = options->monitor ? 1 : 0;
	azRegion->appNodes = options->appNode ? 1 : 0;
	azRegion->nodes = options->nodes;

	azRegion->fromSource = options->fromSource;

	/*
	 * Prepare vnet and subnet IP addresses prefixes.
	 */
	sformat(azRegion->vnetPrefix, sizeof(azRegion->vnetPrefix),
			"10.%d.0.0/16",
			options->cidr);

	sformat(azRegion->subnetPrefix, sizeof(azRegion->subnetPrefix),
			"10.%d.%d.0/24",
			options->cidr,
			options->cidr);
}
