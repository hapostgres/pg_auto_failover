/*
 * src/bin/pg_walsender/monitor_hosts.c
 *   See monitor_hosts.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "monitor_hosts.h"

#include "defaults.h"
#include "file_utils.h"
#include "ipaddr.h"
#include "log.h"
#include "string_utils.h"

#define HASH_LINE_PREFIX "# hash "
#define HASH_LEN 32


/* the local copy: its recorded fingerprint and the raw file contents */
typedef struct LocalHosts
{
	bool exists;
	char hash[HASH_LEN + 1];
	char *contents;             /* whole file, malloc'ed */
	time_t mtime;
} LocalHosts;


static void
local_hosts_read(const char *path, LocalHosts *local)
{
	memset(local, 0, sizeof(*local)); /* IGNORE-BANNED */

	struct stat st;
	long size = 0;

	if (stat(path, &st) != 0 ||
		!read_file_if_exists(path, &(local->contents), &size) ||
		local->contents == NULL)
	{
		return;
	}

	local->exists = true;
	local->mtime = st.st_mtime;

	if (strncmp(local->contents, HASH_LINE_PREFIX, strlen(HASH_LINE_PREFIX)) == 0 &&
		strlen(local->contents) >= strlen(HASH_LINE_PREFIX) + HASH_LEN)
	{
		strlcpy(local->hash, local->contents + strlen(HASH_LINE_PREFIX),
				HASH_LEN + 1);
	}
}


static bool
local_hosts_match(const LocalHosts *local, const char *peerIP)
{
	if (!local->exists)
	{
		return false;
	}

	/* iterate over a copy: strtok_r writes into what it walks */
	char *copy = strdup(local->contents);

	if (copy == NULL)
	{
		return false;
	}

	bool found = false;
	char *save = NULL;

	for (char *line = strtok_r(copy, "\n", &save);
		 line != NULL && !found;
		 line = strtok_r(NULL, "\n", &save))
	{
		if (line[0] == '#')
		{
			continue;
		}

		found = ipaddrHostMatchesAddress(line, peerIP);
	}

	free(copy);

	return found;
}


static bool
split_route_key(const char *routeKey, char *formation, size_t formationSize,
				int *groupId)
{
	const char *slash = strrchr(routeKey, '/');

	if (slash == NULL || slash == routeKey ||
		(size_t) (slash - routeKey) >= formationSize)
	{
		return false;
	}

	strlcpy(formation, routeKey, (size_t) (slash - routeKey) + 1);

	return stringToInt(slash + 1, groupId);
}


static PGconn *
connect_to_monitor(const char *monitorUriPath)
{
	char *uri = NULL;
	long size = 0;

	if (monitorUriPath[0] == '\0' ||
		!read_file_if_exists(monitorUriPath, &uri, &size) || uri == NULL)
	{
		return NULL;
	}

	/* one line, no trailing newline */
	uri[strcspn(uri, "\r\n")] = '\0';

	const char *keys[] = { "dbname", "connect_timeout", NULL };
	const char *values[] = { uri, "3", NULL };

	PGconn *conn = PQconnectdbParams(keys, values, 1);

	free(uri);

	if (PQstatus(conn) != CONNECTION_OK)
	{
		log_warn("pg_walsender could not reach the monitor to validate its "
				 "list of nodes: %s", PQerrorMessage(conn));
		PQfinish(conn);
		return NULL;
	}

	return conn;
}


/*
 * refresh validates the local copy against the monitor: nothing to do when
 * the fingerprints agree (only the file's mtime is renewed, which is what
 * rate-limits the next validation), a full fetch when they differ.
 */
static void
refresh(const char *routeKey, const char *listPath, const char *monitorUriPath,
		LocalHosts *local)
{
	char formation[NAMEDATALEN + 16];
	int groupId = 0;

	if (!split_route_key(routeKey, formation, sizeof(formation), &groupId))
	{
		return;
	}

	PGconn *conn = connect_to_monitor(monitorUriPath);

	if (conn == NULL)
	{
		return;
	}

	char groupStr[16];

	sformat(groupStr, sizeof(groupStr), "%d", groupId);

	const char *params[2] = { formation, groupStr };

	PGresult *res = PQexecParams(
		conn, "SELECT pgautofailover.get_group_hosts_hash($1, $2::int)",
		2, NULL, params, NULL, NULL, 0);

	bool unchanged = PQresultStatus(res) == PGRES_TUPLES_OK &&
					 PQntuples(res) == 1 && !PQgetisnull(res, 0, 0) &&
					 local->exists &&
					 strcmp(PQgetvalue(res, 0, 0), local->hash) == 0;
	bool queryOk = PQresultStatus(res) == PGRES_TUPLES_OK;

	PQclear(res);

	if (unchanged)
	{
		(void) utimes(listPath, NULL);
	}
	else if (queryOk)
	{
		res = PQexecParams(
			conn,
			"SELECT hash, array_to_string(hosts, E'\\n') "
			"FROM pgautofailover.get_group_hosts($1, $2::int)",
			2, NULL, params, NULL, NULL, 0);

		if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
		{
			PQExpBuffer buffer = createPQExpBuffer();

			appendPQExpBuffer(buffer, HASH_LINE_PREFIX "%s\n",
							  PQgetvalue(res, 0, 0));

			if (!PQgetisnull(res, 0, 1) && PQgetvalue(res, 0, 1)[0] != '\0')
			{
				appendPQExpBuffer(buffer, "%s\n", PQgetvalue(res, 0, 1));
			}

			if (!PQExpBufferBroken(buffer) &&
				write_file_atomic(buffer->data, buffer->len, (char *) listPath))
			{
				free(local->contents);
				local_hosts_read(listPath, local);
			}

			destroyPQExpBuffer(buffer);
		}

		PQclear(res);
	}

	PQfinish(conn);
}


bool
monitor_hosts_contain(const char *routeKey, const char *routePath,
					  const char *monitorUriPath, const char *peerIP)
{
	char listPath[MAXPGPATH];

	sformat(listPath, sizeof(listPath), "%s/" PG_AUTOCTL_ARCHIVER_NODES_FILE,
			routePath);

	LocalHosts local;

	local_hosts_read(listPath, &local);

	time_t now = time(NULL);

	/* validate an old (or missing) copy first, see monitor_hosts.h */
	if (!local.exists || (now - local.mtime) >= WS_HOSTS_MAX_AGE_SECONDS)
	{
		refresh(routeKey, listPath, monitorUriPath, &local);
	}

	bool found = local_hosts_match(&local, peerIP);

	if (!found && local.exists &&
		(time(NULL) - local.mtime) >= WS_HOSTS_MISS_MIN_AGE_SECONDS)
	{
		refresh(routeKey, listPath, monitorUriPath, &local);
		found = local_hosts_match(&local, peerIP);
	}

	free(local.contents);

	return found;
}
