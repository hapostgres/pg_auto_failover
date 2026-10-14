/*
 * src/bin/pg_walserver/clusters.c
 *   See clusters.h. Deliberately built on the low-level, dynamic-section
 *   ini.h API (ini_load/ini_section_count/...) rather than this project's
 *   own ini_file.c wrapper: ini_file.c's IniOption model assumes a fixed,
 *   compile-time-known set of section/key names, which doesn't fit a file
 *   whose sections are one per cluster, under whatever key names the
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

#include "clusters.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * clusters_load reads and parses the clusters ini file at path into a freshly
 * malloc'd array (*clustersOut, *countOut entries; free with clusters_free()),
 * one WsCluster per non-global section, keyed by its section name (an opaque
 * string -- see clusters.h) with its "path" property (the only key
 * recognized; any other key logs a warning and is ignored). Returns false
 * (with *clustersOut and *countOut left untouched) when the file cannot be
 * read or parsed.
 */
bool
clusters_load(const char *path, WsCluster **clustersOut, int *countOut)
{
	*clustersOut = NULL;
	*countOut = 0;

	/*
	 * No clusters file yet is a normal, expected state -- the first "setup"/
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

	if (!read_file_capped(path, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read clusters file \"%s\"", path);
		return false;
	}

	ini_t *ini = ini_load(contents, NULL);

	free(contents);

	if (ini == NULL)
	{
		log_error("Failed to parse clusters file \"%s\"", path);
		return false;
	}

	int sectionCount = ini_section_count(ini);

	/* section 0 is ini.h's implicit global section: never a real cluster */
	WsCluster *clusters = (WsCluster *) calloc(sectionCount, sizeof(WsCluster));

	if (clusters == NULL && sectionCount > 0)
	{
		log_error("Failed to allocate memory for %d clusters", sectionCount);
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

		WsCluster *cluster = &clusters[n];

		memset(cluster, 0, sizeof(WsCluster));
		strlcpy(cluster->key, name, sizeof(cluster->key));

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
			 * rather than relying on every clusters file being written with
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
				strlcpy(cluster->path, propValue, sizeof(cluster->path));
			}
			else if (streq(propName, "upstream"))
			{
				strlcpy(cluster->upstream, propValue, sizeof(cluster->upstream));
			}
			else if (streq(propName, "hostname"))
			{
				strlcpy(cluster->hostname, propValue, sizeof(cluster->hostname));
			}
			else if (streq(propName, "receivewal"))
			{
				if (streq(propValue, "pull"))
				{
					cluster->receivewalPull = true;
				}
				else
				{
					log_warn("Ignoring unknown \"receivewal\" value \"%s\" in "
							 "section [%s]: the only recognized value is "
							 "\"pull\"", propValue, name);
				}
			}
			else if (streq(propName, "disabled"))
			{
				cluster->disabled = streq(propValue, "true");
			}
			else
			{
				log_warn("Ignoring unknown clusters file key \"%s\" in section [%s]",
						 propName, name);
			}
		}

		n++;
	}

	ini_destroy(ini);

	*clustersOut = clusters;
	*countOut = n;

	return true;
}


/* clusters_free releases an array returned by clusters_load(). */
void
clusters_free(WsCluster *clusters)
{
	free(clusters);
}


/*
 * clusters_find returns the cluster whose key exactly matches (case-sensitive)
 * key, or, failing that, the cluster whose key is the wildcard
 * WS_CLUSTERS_WILDCARD_KEY ("*"), if the file has one. NULL when neither
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
 * cluster's path the way PgBouncer substitutes dbname into its connection
 * string -- every dbname that falls through to the wildcard shares that
 * one configured path verbatim, never a per-key subdirectory synthesized
 * from a string an unauthenticated client provided (which would turn an
 * operator-chosen key -- like pg_auto_failover's own "<formation>/<group>"
 * -- into a path-traversal surface the moment it contained a "/" or "..").
 * A cluster key is, and stays, just an opaque label matched by this function;
 * only a cluster's own explicit, operator-written "path" property ever
 * touches the filesystem, see clusters.h's own comment.
 */
const WsCluster *
clusters_find(const WsCluster *clusters, int count, const char *key)
{
	const WsCluster *exact = clusters_find_exact(clusters, count, key);

	if (exact != NULL)
	{
		return exact;
	}

	return clusters_find_exact(clusters, count, WS_CLUSTERS_WILDCARD_KEY);
}


/* clusters_find() without the wildcard fallback, see clusters.h's own comment */
const WsCluster *
clusters_find_exact(const WsCluster *clusters, int count, const char *key)
{
	for (int i = 0; i < count; i++)
	{
		if (streq(clusters[i].key, key))
		{
			return &clusters[i];
		}
	}

	return NULL;
}


/*
 * clusters_find_by_hostname matches hostname (case-insensitively, DNS names
 * are not case sensitive: RFC 952/RFC 921, the same rule real PostgreSQL's
 * own sni_clienthello_cb() applies to its pg_hosts.conf lookup) against
 * every cluster's own "hostname" property. See clusters.h's own comment on
 * why this exists at all.
 */
const WsCluster *
clusters_find_by_hostname(const WsCluster *clusters, int count, const char *hostname)
{
	if (hostname == NULL || hostname[0] == '\0')
	{
		return NULL;
	}

	for (int i = 0; i < count; i++)
	{
		if (clusters[i].hostname[0] != '\0' &&
			strcasecmp(clusters[i].hostname, hostname) == 0)
		{
			return &clusters[i];
		}
	}

	return NULL;
}


/*
 * clusters_slot_name derives a valid, deterministic PostgreSQL replication
 * slot name (lowercase alnum/underscore only, NAMEDATALEN-1 bytes max) from
 * an arbitrary cluster key -- which, unlike a slot name, is an entirely
 * opaque string with no character restrictions (see this file's own header
 * comment: pg_auto_failover's own archiver reconciler uses
 * "<formation>/<group>" keys, for one). The sanitized key alone could
 * collide (e.g. "a/b" and "a-b" both sanitize to "a_b"); a short CRC32C
 * suffix of the *original*, unsanitized key makes every slot name unique
 * per cluster regardless. Always writes a NUL-terminated name into out
 * (truncating the sanitized part, never the suffix, if it would overflow
 * outSize/NAMEDATALEN).
 */
void
clusters_slot_name(const char *clusterKey, char *out, size_t outSize)
{
	pg_crc32c crc;

	INIT_CRC32C(crc);
	COMP_CRC32C(crc, clusterKey, strlen(clusterKey));
	FIN_CRC32C(crc);

	char sanitized[NAMEDATALEN] = { 0 };
	size_t si = 0;

	for (const char *p = clusterKey; *p != '\0' && si < sizeof(sanitized) - 1; p++)
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
		strlcpy(sanitized, "cluster", sizeof(sanitized));
	}

	char suffix[16];

	sformat(suffix, sizeof(suffix), "_%08x", crc);

	/* PostgreSQL slot names are NAMEDATALEN-1 (63) bytes max; keep the
	 * fixed prefix and CRC suffix intact, truncating only the sanitized
	 * cluster key if the combination would overflow that */
	size_t maxLen = NAMEDATALEN - 1;
	size_t fixedLen = strlen("pgws_") + strlen(suffix);

	if (fixedLen < maxLen && strlen(sanitized) > maxLen - fixedLen)
	{
		sanitized[maxLen - fixedLen] = '\0';
	}

	sformat(out, outSize, "pgws_%s%s", sanitized, suffix);
}


/*
 * clusters_persist_path writes "path = <path>" into an existing [clusterKey]
 * section that doesn't have one yet, right after its header line. See
 * clusters.h's own comment: never creates a new section, and a no-op (true)
 * if the section already has a "path" property -- callers only reach this
 * for a cluster cli_resolve_upstream() found in the file but had to default
 * a path for, so both of those should already hold, but a plain text
 * re-scan here is cheap insurance against acting on stale information.
 */
bool
clusters_persist_path(const char *clustersPath, const char *clusterKey,
					  const char *path)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!read_file_capped(clustersPath, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read clusters file \"%s\"", clustersPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", clusterKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to add \"path\" to",
				  header, clustersPath);
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
			  write_file_atomic(whole->data, whole->len, clustersPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", clustersPath);
		return false;
	}

	log_info("Added \"path = %s\" to cluster \"%s\" in \"%s\"",
			 path, clusterKey, clustersPath);

	return true;
}


/*
 * clusters_set_property sets propName = propValue in an existing [clusterKey]
 * section: replacing that property's own line in place if the section
 * already has one, appending a new line right after the header
 * otherwise. Unlike clusters_persist_path() above, this always writes the
 * given value -- the whole point of "pg_walserver cluster set-upstream"
 * (cli_root.c) is to *change* an already-set "upstream", not merely fill
 * in a gap. Never creates a new section (that's "pg_walserver cluster
 * register"'s own job); false, with an error already logged, if
 * clusterKey has no section to set anything in.
 */
bool
clusters_set_property(const char *clustersPath, const char *clusterKey,
					  const char *propName, const char *propValue)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!read_file_capped(clustersPath, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read clusters file \"%s\"", clustersPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", clusterKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to set \"%s\" in",
				  header, clustersPath, propName);
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
			  write_file_atomic(whole->data, whole->len, clustersPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", clustersPath);
		return false;
	}

	log_info("Set \"%s = %s\" for cluster \"%s\" in \"%s\"",
			 propName, propValue, clusterKey, clustersPath);

	return true;
}


/*
 * clusters_drop_section removes the whole [clusterKey] section (header and
 * every property line under it, up to the next section or end of file)
 * from the clusters file at clustersPath -- "pg_walserver cluster drop"'s own
 * job. Never touches anything on disk under the cluster's own "path": that
 * is a deliberate, separate decision (--purge, cli_root.c's own cluster-
 * drop command), not an automatic side effect of removing the
 * registration alone. false, with an error already logged, if clusterKey
 * has no section to remove.
 */
bool
clusters_drop_section(const char *clustersPath, const char *clusterKey)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (!read_file_capped(clustersPath, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read clusters file \"%s\"", clustersPath);
		return false;
	}

	char header[NAMEDATALEN + 16 + 2] = { 0 };

	sformat(header, sizeof(header), "[%s]", clusterKey);

	char *sectionStart = strstr(contents, header);

	if (sectionStart == NULL ||
		(sectionStart != contents && sectionStart[-1] != '\n'))
	{
		log_error("No section \"%s\" found in \"%s\" to drop",
				  header, clustersPath);
		free(contents);
		return false;
	}

	/* the blank line write_cluster_section() always writes right before a
	 * new section's own header belongs to the *previous* section as far
	 * as a human editing this file is concerned; drop it along with the
	 * section itself so removing a cluster never leaves a stray blank line
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
			  write_file_atomic(whole->data, whole->len, clustersPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", clustersPath);
		return false;
	}

	log_info("Dropped cluster \"%s\" from \"%s\"", clusterKey, clustersPath);

	return true;
}


/*
 * config_load_global reads pg_walserver.ini's own anonymous/global section
 * (see WsGlobalConfig's own comment) into *out. Always succeeds (true),
 * leaving *out zeroed, when the file doesn't exist yet or has no global
 * section -- a normal, expected state, not an error (the same "missing
 * config is not a failure" convention clusters_load() itself already
 * follows). Named "config_", not "clusters_": this reads pg_walserver's own
 * settings, never a cluster -- those stay clusters_*, this file's own
 * ini-parsing home for both concerns notwithstanding.
 */
bool
config_load_global(const char *configPath, WsGlobalConfig *out)
{
	memset(out, 0, sizeof(WsGlobalConfig));

	if (!file_exists(configPath))
	{
		return true;
	}

	char *contents = NULL;
	size_t fileSize = 0;

	if (!read_file_capped(configPath, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read config file \"%s\"", configPath);
		return false;
	}

	ini_t *ini = ini_load(contents, NULL);

	free(contents);

	if (ini == NULL)
	{
		log_error("Failed to parse config file \"%s\"", configPath);
		return false;
	}

	int propCount = ini_property_count(ini, INI_GLOBAL_SECTION);

	for (int p = 0; p < propCount; p++)
	{
		const char *rawPropName = ini_property_name(ini, INI_GLOBAL_SECTION, p);
		const char *propValue = ini_property_value(ini, INI_GLOBAL_SECTION, p);

		if (rawPropName == NULL || propValue == NULL)
		{
			continue;
		}

		char propName[128];

		strlcpy(propName, rawPropName, sizeof(propName));

		size_t nameLen = strlen(propName);

		while (nameLen > 0 && isspace((unsigned char) propName[nameLen - 1]))
		{
			propName[--nameLen] = '\0';
		}

		if (streq(propName, "port"))
		{
			out->havePort = stringToInt(propValue, &out->port);
		}
		else if (streq(propName, "ssl-cert-file"))
		{
			strlcpy(out->sslCertFile, propValue, sizeof(out->sslCertFile));
		}
		else if (streq(propName, "ssl-key-file"))
		{
			strlcpy(out->sslKeyFile, propValue, sizeof(out->sslKeyFile));
		}
		else if (streq(propName, "ssl-ca-file"))
		{
			strlcpy(out->sslCaFile, propValue, sizeof(out->sslCaFile));
		}
		else if (streq(propName, "auth-timeout"))
		{
			out->haveAuthTimeout = stringToInt(propValue, &out->authTimeout);
		}
		else
		{
			log_warn("Ignoring unknown global config file key \"%s\"",
					 propName);
		}
	}

	ini_destroy(ini);

	return true;
}


/*
 * config_set_global_property sets "propName = propValue" as a plain
 * top-of-file line, before any cluster's own [section] header -- creating
 * the file (with just that one line) if it doesn't exist yet, replacing
 * an already-present line with the same propName otherwise. "pg_walserver
 * setup"'s own way to persist one instance-level setting; never touches
 * any cluster's own section. See config_load_global()'s own comment for
 * why this is "config_", not "clusters_".
 */
bool
config_set_global_property(const char *configPath, const char *propName,
						   const char *propValue)
{
	char *contents = NULL;
	size_t fileSize = 0;

	if (file_exists(configPath) &&
		!read_file_capped(configPath, WS_MAX_CONFIG_FILE_SIZE, false,
						  &contents, &fileSize, NULL))
	{
		log_error("Failed to read config file \"%s\"", configPath);
		return false;
	}

	/* the end of the anonymous/global region: the first line starting
	 * with '[', or the whole file if there is none yet */
	char *globalEnd = contents;

	if (contents != NULL)
	{
		char *cursor = contents;

		while (*cursor != '\0')
		{
			if (*cursor == '[' && (cursor == contents || cursor[-1] == '\n'))
			{
				break;
			}

			char *nextLine = strchr(cursor, '\n');

			if (nextLine == NULL)
			{
				cursor += strlen(cursor);
				break;
			}

			cursor = nextLine + 1;
		}

		globalEnd = cursor;
	}

	size_t propNameLen = strlen(propName);
	char *propLineStart = NULL;
	char *propLineEnd = NULL;

	if (contents != NULL)
	{
		char *cursor = contents;

		while (cursor < globalEnd)
		{
			char *key = cursor;

			while (*key == ' ' || *key == '\t')
			{
				key++;
			}

			char *nextLine = strchr(cursor, '\n');
			char *thisLineEnd = (nextLine != NULL) ? nextLine + 1 : globalEnd;

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
					propLineEnd = thisLineEnd;
					break;
				}
			}

			if (nextLine == NULL)
			{
				break;
			}

			cursor = nextLine + 1;
		}
	}

	PQExpBuffer whole = createPQExpBuffer();

	if (propLineStart != NULL)
	{
		appendBinaryPQExpBuffer(whole, contents, propLineStart - contents);
		appendPQExpBuffer(whole, "%s = %s\n", propName, propValue);
		appendPQExpBufferStr(whole, propLineEnd);
	}
	else if (contents != NULL)
	{
		appendBinaryPQExpBuffer(whole, contents, globalEnd - contents);
		appendPQExpBuffer(whole, "%s = %s\n", propName, propValue);
		appendPQExpBufferStr(whole, globalEnd);
	}
	else
	{
		appendPQExpBuffer(whole, "%s = %s\n", propName, propValue);
	}

	bool ok = !PQExpBufferBroken(whole) &&
			  write_file_atomic(whole->data, whole->len, configPath);

	destroyPQExpBuffer(whole);
	free(contents);

	if (!ok)
	{
		log_error("Failed to write \"%s\"", configPath);
		return false;
	}

	log_info("Set \"%s = %s\" in \"%s\"", propName, propValue, configPath);

	return true;
}


/*
 * config_file_path resolves the on-disk path of pg_walserver's own config
 * file (pg_walserver.ini: the global settings section this file's own
 * config_load_global()/config_set_global_property() manage, plus one
 * [section] per cluster the rest of this file manages) into out, up to
 * outSize bytes:
 *
 *   1. configFile itself, when given explicitly (a command's own
 *      --config flag, --config-file on "ls" specifically, see its own
 *      header comment) -- always wins;
 *   2. else the PG_WALSERVER_CONFIG_FILE environment variable, when set;
 *   3. else "<pgdata>/pg_walserver.ini", the long-standing default.
 *
 * This is what lets a Debian-style deployment -- config under
 * /etc/pg_walserver/pg_walserver.ini, data under /var/lib/pg_walserver/,
 * the same split a systemd unit file or a container entrypoint commonly
 * wants -- point every pg_walserver command at a config file that lives
 * outside --pgdata, without changing where clusters/basebackups/WAL/certs
 * themselves are stored (those stay under --pgdata unconditionally; only
 * this one file's own location becomes independently configurable).
 */
void
config_file_path(const char *pgdata, const char *configFile,
				 char *out, size_t outSize)
{
	if (configFile != NULL && configFile[0] != '\0')
	{
		strlcpy(out, configFile, outSize);
		return;
	}

	if (env_exists(WS_CONFIG_FILE_ENV_VAR))
	{
		char fromEnv[MAXPGPATH] = { 0 };

		if (get_env_copy(WS_CONFIG_FILE_ENV_VAR, fromEnv, sizeof(fromEnv)) &&
			fromEnv[0] != '\0')
		{
			strlcpy(out, fromEnv, outSize);
			return;
		}
	}

	sformat(out, outSize, "%s/pg_walserver.ini", pgdata);
}
