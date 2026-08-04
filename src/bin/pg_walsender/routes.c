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


bool
routes_load(const char *path, WsRoute **routesOut, int *countOut)
{
	*routesOut = NULL;
	*countOut = 0;

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file(path, &contents, &fileSize))
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

			if (strcmp(propName, "walcache") == 0)
			{
				strlcpy(route->walcacheDir, propValue, sizeof(route->walcacheDir));
			}
			else if (strcmp(propName, "basebackup") == 0)
			{
				strlcpy(route->basebackupDir, propValue, sizeof(route->basebackupDir));
			}
			else if (strcmp(propName, "allowed_hosts") == 0)
			{
				strlcpy(route->allowedHosts, propValue, sizeof(route->allowedHosts));
			}
			else if (strcmp(propName, "systemid") == 0)
			{
				strlcpy(route->systemId, propValue, sizeof(route->systemId));
			}
			else if (strcmp(propName, "timeline") == 0)
			{
				route->timeline = atoi(propValue);
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
		if (strcmp(routes[i].key, key) == 0)
		{
			return &routes[i];
		}
	}

	return NULL;
}


bool
routes_host_allowed(const WsRoute *route, const char *peerIP)
{
	if (route->allowedHosts[0] == '\0')
	{
		return true;   /* no restriction configured for this route */
	}

	char list[sizeof(route->allowedHosts)];

	strlcpy(list, route->allowedHosts, sizeof(list));

	char *saveptr = NULL;

	for (char *tok = strtok_r(list, ",", &saveptr);
		 tok != NULL;
		 tok = strtok_r(NULL, ",", &saveptr))
	{
		while (*tok == ' ' || *tok == '\t')
		{
			tok++;
		}

		if (strcmp(tok, peerIP) == 0)
		{
			return true;
		}

		/* also resolve hostnames in the allow-list and compare addresses */
		struct addrinfo hints;

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;

		struct addrinfo *res = NULL;

		if (getaddrinfo(tok, NULL, &hints, &res) == 0)
		{
			for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next)
			{
				char resolved[NI_MAXHOST];

				if (getnameinfo(rp->ai_addr, rp->ai_addrlen,
								resolved, sizeof(resolved),
								NULL, 0, NI_NUMERICHOST) == 0 &&
					strcmp(resolved, peerIP) == 0)
				{
					freeaddrinfo(res);
					return true;
				}
			}

			freeaddrinfo(res);
		}
	}

	return false;
}
