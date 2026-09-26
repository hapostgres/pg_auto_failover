/*
 * src/bin/pg_walsender/routes.c
 *   See routes.h. Deliberately built on the low-level, dynamic-section
 *   ini.h API (ini_load/ini_section_count/...) rather than this project's
 *   own ini_file.c wrapper: ini_file.c's IniOption model assumes a fixed,
 *   compile-time-known set of section/key names, which doesn't fit a file
 *   whose sections are one per archived (formation, group) -- unknown in
 *   advance. ini.h's lower-level, enumerable API is exactly the right
 *   shape and is already vendored into this project (src/bin/lib/libs/
 *   ini.h, compiled into libpgaf_common.a via common/ini_implementation.c).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "ini.h"

#include "routes.h"
#include "file_utils.h"
#include "log.h"
#include "ws_util.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


bool
routes_load(const char *path, WsRoute **routesOut, int *countOut)
{
	*routesOut = NULL;
	*countOut = 0;

	char *contents = NULL;
	size_t fileSize = 0;

	if (!ws_read_file_capped(path, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &fileSize, NULL))
	{
		log_error("Failed to read routes file \"%s\"", path);
		return false;
	}

	ini_t *ini = ini_load(contents, NULL);

	free(contents);

	if (ini == NULL)
	{
		log_error("Failed to parse routes file \"%s\"", path);
		return false;
	}

	int sectionCount = ini_section_count(ini);

	/* section 0 is ini.h's implicit global section: never a real route */
	WsRoute *routes = (WsRoute *) calloc(sectionCount, sizeof(WsRoute));

	if (routes == NULL && sectionCount > 0)
	{
		log_error("Failed to allocate memory for %d routes", sectionCount);
		ini_destroy(ini);
		return false;
	}

	int n = 0;

	for (int s = 0; s < sectionCount; s++)
	{
		const char *name = ini_section_name(ini, s);

		if (name == NULL || name[0] == '\0')
		{
			continue;   /* the global section */
		}

		WsRoute *route = &routes[n];

		memset(route, 0, sizeof(WsRoute));
		strlcpy(route->key, name, sizeof(route->key));

		int propCount = ini_property_count(ini, s);

		for (int p = 0; p < propCount; p++)
		{
			const char *rawPropName = ini_property_name(ini, s, p);
			const char *propValue = ini_property_value(ini, s, p);

			if (rawPropName == NULL || propValue == NULL)
			{
				continue;
			}

			/*
			 * ini.h's own parser (src/bin/lib/libs/ini.h's ini_load) trims
			 * whitespace around the value but NOT trailing whitespace
			 * between a key and '=' -- "walcache = /path" parses the key
			 * as "walcache " with a trailing space. Trim defensively here
			 * rather than relying on every routes file being written with
			 * no space before '='.
			 */
			char propName[128];

			strlcpy(propName, rawPropName, sizeof(propName));

			size_t nameLen = strlen(propName);

			while (nameLen > 0 && isspace((unsigned char) propName[nameLen - 1]))
			{
				propName[--nameLen] = '\0';
			}

			if (streq(propName, "path"))
			{
				strlcpy(route->path, propValue, sizeof(route->path));
			}
			else
			{
				log_warn("Ignoring unknown routes file key \"%s\" in section [%s]",
						 propName, name);
			}
		}

		n++;
	}

	ini_destroy(ini);

	*routesOut = routes;
	*countOut = n;

	return true;
}


void
routes_free(WsRoute *routes)
{
	free(routes);
}


const WsRoute *
routes_find(const WsRoute *routes, int count, const char *key)
{
	for (int i = 0; i < count; i++)
	{
		if (streq(routes[i].key, key))
		{
			return &routes[i];
		}
	}

	return NULL;
}
