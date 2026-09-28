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
#include "pqexpbuffer.h"
#include "port/pg_crc32c.h"

#include "routes.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"
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

	/*
	 * No routes file yet is a normal, expected state -- the first "setup"/
	 * "cluster register" call for a fresh --pgdata, most notably -- never
	 * an error to log; the same file_exists()-before-read convention
	 * pg_autoctl's own config-file callers already use throughout (see
	 * e.g. cli_create_node.c), rather than attempting the read and
	 * demoting whatever error comes back.
	 */
	if (!file_exists(path))
	{
		return true;
	}

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
			else if (streq(propName, "receivewal"))
			{
				if (streq(propValue, "pull"))
				{
					route->receivewalPull = true;
				}
				else
				{
					log_warn("Ignoring unknown \"receivewal\" value \"%s\" in "
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


/*
 * routes_slot_name -- see routes.h's own comment.
 */
void
routes_slot_name(const char *routeKey, char *out, size_t outSize)
{
	pg_crc32c crc;

	INIT_CRC32C(crc);
	COMP_CRC32C(crc, routeKey, strlen(routeKey));
	FIN_CRC32C(crc);

	char sanitized[NAMEDATALEN] = { 0 };
	size_t si = 0;

	for (const char *p = routeKey; *p != '\0' && si < sizeof(sanitized) - 1; p++)
	{
		unsigned char c = (unsigned char) *p;

		if (isalnum(c))
		{
			sanitized[si++] = (char) tolower(c);
		}
		else if (si > 0 && sanitized[si - 1] != '_')
		{
			sanitized[si++] = '_';
		}
	}

	while (si > 0 && sanitized[si - 1] == '_')
	{
		si--;
	}

	sanitized[si] = '\0';

	if (sanitized[0] == '\0')
	{
		strlcpy(sanitized, "route", sizeof(sanitized));
	}

	char suffix[16];

	sformat(suffix, sizeof(suffix), "_%08x", crc);

	/* PostgreSQL slot names are NAMEDATALEN-1 (63) bytes max; keep the
	 * fixed prefix and CRC suffix intact, truncating only the sanitized
	 * route key if the combination would overflow that */
	size_t maxLen = NAMEDATALEN - 1;
	size_t fixedLen = strlen("pgws_") + strlen(suffix);

	if (fixedLen < maxLen && strlen(sanitized) > maxLen - fixedLen)
	{
		sanitized[maxLen - fixedLen] = '\0';
	}

	sformat(out, outSize, "pgws_%s%s", sanitized, suffix);
}


/*
 * routes_persist_path writes "path = <path>" into an existing [routeKey]
 * section that doesn't have one yet, right after its header line. See
 * routes.h's own comment: never creates a new section, and a no-op (true)
 * if the section already has a "path" property -- callers only reach this
 * for a route cli_resolve_upstream() found in the file but had to default
 * a path for, so both of those should already hold, but a plain text
 * re-scan here is cheap insurance against acting on stale information.
 */
bool
routes_persist_path(const char *routesPath, const char *routeKey,
					const char *path)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!ws_read_file_capped(routesPath, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &fileSize, NULL))
	{
		log_error("Failed to read routes file \"%s\"", routesPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", routeKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to add \"path\" to",
				  header, routesPath);
		free(contents);
		return false;
	}

	char *lineEnd = strchr(sectionStart, '\n');
	char *afterHeader = (lineEnd != NULL)
						? lineEnd + 1
						: sectionStart + strlen(sectionStart);

	/* scan this section's own lines (up to the next "[" at start of line,
	 * or end of file) for an already-present "path" property */
	bool hasPath = false;
	char *cursor = afterHeader;

	while (*cursor != '\0' && *cursor != '[')
	{
		char *key = cursor;

		while (*key == ' ' || *key == '\t')
		{
			key++;
		}

		if (strncmp(key, "path", 4) == 0)
		{
			char *afterKey = key + 4;

			while (*afterKey == ' ' || *afterKey == '\t')
			{
				afterKey++;
			}

			if (*afterKey == '=')
			{
				hasPath = true;
				break;
			}
		}

		char *nextLine = strchr(cursor, '\n');

		if (nextLine == NULL)
		{
			break;
		}

		cursor = nextLine + 1;
	}

	if (hasPath)
	{
		free(contents);
		return true;
	}

	PQExpBuffer whole = createPQExpBuffer();

	appendBinaryPQExpBuffer(whole, contents, afterHeader - contents);
	appendPQExpBuffer(whole, "path = %s\n", path);
	appendPQExpBufferStr(whole, afterHeader);

	bool ok = !PQExpBufferBroken(whole) &&
			  write_file_atomic(whole->data, whole->len, routesPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", routesPath);
		return false;
	}

	log_info("Added \"path = %s\" to route \"%s\" in \"%s\"",
			 path, routeKey, routesPath);

	return true;
}


/*
 * routes_set_property sets propName = propValue in an existing [routeKey]
 * section: replacing that property's own line in place if the section
 * already has one, appending a new line right after the header
 * otherwise. Unlike routes_persist_path() above, this always writes the
 * given value -- the whole point of "pg_walserver cluster set-upstream"
 * (cli_root.c) is to *change* an already-set "upstream", not merely fill
 * in a gap. Never creates a new section (that's "pg_walserver cluster
 * register"'s own job); false, with an error already logged, if
 * routeKey has no section to set anything in.
 */
bool
routes_set_property(const char *routesPath, const char *routeKey,
					const char *propName, const char *propValue)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!ws_read_file_capped(routesPath, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &fileSize, NULL))
	{
		log_error("Failed to read routes file \"%s\"", routesPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", routeKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to set \"%s\" in",
				  header, routesPath, propName);
		free(contents);
		return false;
	}

	char *lineEnd = strchr(sectionStart, '\n');
	char *afterHeader = (lineEnd != NULL)
						? lineEnd + 1
						: sectionStart + strlen(sectionStart);

	size_t propNameLen = strlen(propName);
	char *propLineStart = NULL;
	char *propLineEnd = NULL;
	char *cursor = afterHeader;

	while (*cursor != '\0' && *cursor != '[')
	{
		char *key = cursor;

		while (*key == ' ' || *key == '\t')
		{
			key++;
		}

		char *nextLine = strchr(cursor, '\n');
		char *thisLineEnd = (nextLine != NULL) ? nextLine : cursor + strlen(cursor);

		if (strncmp(key, propName, propNameLen) == 0)
		{
			char *afterKey = key + propNameLen;

			while (*afterKey == ' ' || *afterKey == '\t')
			{
				afterKey++;
			}

			if (*afterKey == '=')
			{
				propLineStart = cursor;
				propLineEnd = (nextLine != NULL) ? nextLine + 1 : thisLineEnd;
				break;
			}
		}

		if (nextLine == NULL)
		{
			break;
		}

		cursor = nextLine + 1;
	}

	PQExpBuffer whole = createPQExpBuffer();

	if (propLineStart != NULL)
	{
		/* replace the existing property line with the new value */
		appendBinaryPQExpBuffer(whole, contents, propLineStart - contents);
		appendPQExpBuffer(whole, "%s = %s\n", propName, propValue);
		appendPQExpBufferStr(whole, propLineEnd);
	}
	else
	{
		/* no existing property line: append one right after the header */
		appendBinaryPQExpBuffer(whole, contents, afterHeader - contents);
		appendPQExpBuffer(whole, "%s = %s\n", propName, propValue);
		appendPQExpBufferStr(whole, afterHeader);
	}

	bool ok = !PQExpBufferBroken(whole) &&
			  write_file_atomic(whole->data, whole->len, routesPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", routesPath);
		return false;
	}

	log_info("Set \"%s = %s\" for route \"%s\" in \"%s\"",
			 propName, propValue, routeKey, routesPath);

	return true;
}


/*
 * routes_drop_section removes the whole [routeKey] section (header and
 * every property line under it, up to the next section or end of file)
 * from the routes file at routesPath -- "pg_walserver cluster drop"'s own
 * job. Never touches anything on disk under the route's own "path": that
 * is a deliberate, separate decision (--purge, cli_root.c's own cluster-
 * drop command), not an automatic side effect of removing the
 * registration alone. false, with an error already logged, if routeKey
 * has no section to remove.
 */
bool
routes_drop_section(const char *routesPath, const char *routeKey)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!ws_read_file_capped(routesPath, WS_MAX_CONFIG_FILE_SIZE, false,
							 &contents, &fileSize, NULL))
	{
		log_error("Failed to read routes file \"%s\"", routesPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", routeKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to drop",
				  header, routesPath);
		free(contents);
		return false;
	}

	/* the blank line write_route_section() always writes right before a
	 * new section's own header belongs to the *previous* section as far
	 * as a human editing this file is concerned; drop it along with the
	 * section itself so removing a route never leaves a stray blank line
	 * behind */
	char *removeFrom = sectionStart;

	if (removeFrom > contents && removeFrom[-1] == '\n' &&
		removeFrom - 1 > contents && removeFrom[-2] == '\n')
	{
		removeFrom--;
	}

	char *cursor = sectionStart;
	char *sectionEnd = NULL;

	while (*cursor != '\0')
	{
		char *nextLine = strchr(cursor, '\n');

		if (nextLine == NULL)
		{
			sectionEnd = cursor + strlen(cursor);
			break;
		}

		char *lineStart = nextLine + 1;

		if (*lineStart == '[')
		{
			sectionEnd = lineStart;
			break;
		}

		cursor = lineStart;
	}

	if (sectionEnd == NULL)
	{
		sectionEnd = contents + strlen(contents);
	}

	PQExpBuffer whole = createPQExpBuffer();

	appendBinaryPQExpBuffer(whole, contents, removeFrom - contents);
	appendPQExpBufferStr(whole, sectionEnd);

	bool ok = !PQExpBufferBroken(whole) &&
			  write_file_atomic(whole->data, whole->len, routesPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", routesPath);
		return false;
	}

	log_info("Dropped route \"%s\" from \"%s\"", routeKey, routesPath);

	return true;
}
