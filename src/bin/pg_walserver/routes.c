/*
 * src/bin/pg_walserver/routes.c
 *   See routes.h. Deliberately built on the low-level, dynamic-section
 *   ini.h API (ini_load/ini_section_count/...) rather than this project's
 *   own ini_file.c wrapper: ini_file.c's IniOption model assumes a fixed,
 *   compile-time-known set of section/key names, which doesn't fit a file
 *   whose sections are one per route, under whatever key names the
 *   operator (or a driver such as pg_auto_failover) chose -- unknown in
 *   advance. ini.h's lower-level, enumerable API is exactly the right
 *   shape and is already vendored into this project (src/bin/lib/libs/
 *   ini.h, compiled into libpgaf_common.a via common/ini_implementation.c).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <netdb.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "ini.h"

#include "routes.h"
#include "file_utils.h"
#include "log.h"
#include "ws_util.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * routes_load reads and parses the routes ini file at path into a freshly
 * malloc'd array (*routesOut, *countOut entries; free with routes_free()),
 * one WsRoute per non-global section, keyed by its section name (an opaque
 * string -- see routes.h) with its "path" property (the only key
 * recognized; any other key logs a warning and is ignored). Returns false
 * (with *routesOut and *countOut left untouched) when the file cannot be
 * read or parsed.
 */
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
			else if (streq(propName, "upstream"))
			{
				strlcpy(route->upstream, propValue, sizeof(route->upstream));
			}
			else if (streq(propName, "hostname"))
			{
				strlcpy(route->hostname, propValue, sizeof(route->hostname));
			}
			else if (streq(propName, "capture"))
			{
				if (streq(propValue, "pull"))
				{
					route->capturePull = true;
				}
				else
				{
					log_warn("Ignoring unknown \"capture\" value \"%s\" in "
							 "section [%s]: the only recognized value is "
							 "\"pull\"", propValue, name);
				}
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


/* routes_free releases an array returned by routes_load(). */
void
routes_free(WsRoute *routes)
{
	free(routes);
}


/*
 * routes_find returns the route whose key exactly matches (case-sensitive)
 * key, or, failing that, the route whose key is the wildcard
 * WS_ROUTES_WILDCARD_KEY ("*"), if the file has one. NULL when neither
 * exists.
 *
 * The precedence -- an exact match always wins, the wildcard is only ever
 * a fallback -- and the wildcard's own syntax are deliberately the same as
 * PgBouncer's [databases] "*" entry: "if there is an entry (and no other
 * overriding entries) '* = host=foo', then a connection ... specifying a
 * database 'bar' will effectively behave as if an entry 'bar = host=foo
 * dbname=bar' exists" (pgbouncer.org/config.html). The one deliberate
 * difference: PgBouncer's substitution is safe because its destination is
 * another dbname handed to a real Postgres server, which validates it on
 * its own; ours would be a directory on this server's own filesystem, so
 * this project does NOT substitute the requested key into the wildcard
 * route's path the way PgBouncer substitutes dbname into its connection
 * string -- every dbname that falls through to the wildcard shares that
 * one configured path verbatim, never a per-key subdirectory synthesized
 * from a string an unauthenticated client provided (which would turn an
 * operator-chosen key -- like pg_auto_failover's own "<formation>/<group>"
 * -- into a path-traversal surface the moment it contained a "/" or "..").
 * A route key is, and stays, just an opaque label matched by this function;
 * only a route's own explicit, operator-written "path" property ever
 * touches the filesystem, see routes.h's own comment.
 */
const WsRoute *
routes_find(const WsRoute *routes, int count, const char *key)
{
	const WsRoute *exact = routes_find_exact(routes, count, key);

	if (exact != NULL)
	{
		return exact;
	}

	return routes_find_exact(routes, count, WS_ROUTES_WILDCARD_KEY);
}


/* routes_find() without the wildcard fallback, see routes.h's own comment */
const WsRoute *
routes_find_exact(const WsRoute *routes, int count, const char *key)
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


/*
 * routes_find_by_hostname matches hostname (case-insensitively, DNS names
 * are not case sensitive: RFC 952/RFC 921, the same rule real PostgreSQL's
 * own sni_clienthello_cb() applies to its pg_hosts.conf lookup) against
 * every route's own "hostname" property. See routes.h's own comment on
 * why this exists at all.
 */
const WsRoute *
routes_find_by_hostname(const WsRoute *routes, int count, const char *hostname)
{
	if (hostname == NULL || hostname[0] == '\0')
	{
		return NULL;
	}

	for (int i = 0; i < count; i++)
	{
		if (routes[i].hostname[0] != '\0' &&
			strcasecmp(routes[i].hostname, hostname) == 0)
		{
			return &routes[i];
		}
	}

	return NULL;
}
