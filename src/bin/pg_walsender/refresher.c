/*
 * src/bin/pg_walsender/refresher.c
 *   See refresher.h and monitor_hosts.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "refresher.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor_hosts.h"
#include "routes.h"
#include "signals.h"
#include "string_utils.h"
#include "ws_util.h"

/* at most one monitor hash query per route per this many milliseconds */
#define WS_REFRESH_COALESCE_MS 1000

/*
 * The refresher does not wait for a first connection to fetch a route's
 * list: it validates every route shortly after starting and then every
 * WS_REFRESH_HEARTBEAT_MS, so a local copy exists (and stays fresh) before it
 * is needed. That is what lets an archiver keep admitting the cluster's
 * nodes while the monitor is down -- the disaster the archiver exists for.
 */
#define WS_REFRESH_FIRST_MS 1000
#define WS_REFRESH_HEARTBEAT_MS 30000
#define WS_REFRESH_MAX_ROUTES 256
#define WS_REFRESH_MAX_KEY (NAMEDATALEN + 16)


typedef struct RouteState
{
	char key[WS_REFRESH_MAX_KEY];
	int64_t lastQueryMs;        /* monotonic; 0: never */
	bool pending;               /* a request waits for its coalescing slot */
} RouteState;

static RouteState states[WS_REFRESH_MAX_ROUTES];
static int nStates = 0;

/* monitor failures are negative-cached: no attempt before this instant */
static int64_t negativeUntilMs = 0;


int
ws_refresh_socket_create(const char *path)
{
	struct sockaddr_un addr;

	memset(&addr, 0, sizeof(addr)); /* IGNORE-BANNED */
	addr.sun_family = AF_UNIX;

	if (strlen(path) >= sizeof(addr.sun_path))
	{
		log_error("The refresher socket path \"%s\" is too long", path);
		return -1;
	}

	strlcpy(addr.sun_path, path, sizeof(addr.sun_path));

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0)
	{
		log_error("Failed to create the refresher socket: %m");
		return -1;
	}

	(void) unlink(path);

	/* no window where another user could connect: 0600 from creation */
	mode_t oldMask = umask(0177);
	int rc = bind(fd, (struct sockaddr *) &addr, sizeof(addr));

	umask(oldMask);

	if (rc != 0)
	{
		log_error("Failed to bind the refresher socket \"%s\": %m", path);
		close(fd);
		return -1;
	}

	(void) chmod(path, 0600);

	return fd;
}


static RouteState *
state_for(const char *key)
{
	for (int i = 0; i < nStates; i++)
	{
		if (strcmp(states[i].key, key) == 0)
		{
			return &states[i];
		}
	}

	if (nStates >= WS_REFRESH_MAX_ROUTES)
	{
		return NULL;
	}

	RouteState *st = &states[nStates++];

	memset(st, 0, sizeof(*st)); /* IGNORE-BANNED */
	strlcpy(st->key, key, sizeof(st->key));

	return st;
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
	size_t size = 0;

	if (!ws_read_file_capped(monitorUriPath, WS_MAX_CONFIG_FILE_SIZE, true,
							 &uri, &size, NULL))
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


/* the hash recorded on the first line of the current list, if any */
static bool
current_list_hash(const char *listPath, char *hash)
{
	char *contents = NULL;
	size_t size = 0;

	if (!ws_read_file_capped(listPath, WS_MAX_CONFIG_FILE_SIZE, true,
							 &contents, &size, NULL))
	{
		return false;
	}

	bool ok = strncmp(contents, WS_HOSTS_HASH_LINE_PREFIX,
					  strlen(WS_HOSTS_HASH_LINE_PREFIX)) == 0 &&
			  strlen(contents) >= strlen(WS_HOSTS_HASH_LINE_PREFIX) +
			  WS_HOSTS_HASH_LEN;

	if (ok)
	{
		memcpy(hash, contents + strlen(WS_HOSTS_HASH_LINE_PREFIX), /* IGNORE-BANNED */
			   WS_HOSTS_HASH_LEN);
		hash[WS_HOSTS_HASH_LEN] = '\0';
	}

	free(contents);

	return ok;
}


static void
mark_failure(const char *listPath)
{
	char errPath[MAXPGPATH];

	sformat(errPath, sizeof(errPath), "%s" WS_HOSTS_ERR_SUFFIX, listPath);

	negativeUntilMs = ws_monotonic_ms() +
					  (int64_t) WS_HOSTS_NEGATIVE_SECONDS * 1000;

	(void) ws_write_file_atomic(errPath, "monitor unreachable\n",
								strlen("monitor unreachable\n"));
}


static void
clear_failure(const char *listPath)
{
	char errPath[MAXPGPATH];

	sformat(errPath, sizeof(errPath), "%s" WS_HOSTS_ERR_SUFFIX, listPath);

	(void) unlink(errPath);
}


/*
 * refresh_route validates the list of one route against the monitor:
 * nothing to do when the fingerprints agree (only the file's mtime is
 * renewed, which is what the children wait for), a full fetch and an
 * atomic rewrite when they differ.
 */
static void
refresh_route(const char *routesPath, const char *monitorUriPath,
			  const char *routeKey)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(routesPath, &routes, &routeCount))
	{
		return;
	}

	const WsRoute *route = routes_find(routes, routeCount, routeKey);
	char listPath[MAXPGPATH];

	if (route == NULL || route->path[0] == '\0')
	{
		/* not a route we serve: nothing to write, and never a path built
		 * from what a datagram said */
		routes_free(routes);
		return;
	}

	monitor_hosts_list_path(route->path, listPath, sizeof(listPath));
	routes_free(routes);

	char formation[NAMEDATALEN + 16];
	int groupId = 0;

	if (!split_route_key(routeKey, formation, sizeof(formation), &groupId))
	{
		return;
	}

	PGconn *conn = connect_to_monitor(monitorUriPath);

	if (conn == NULL)
	{
		mark_failure(listPath);
		return;
	}

	char groupStr[16];

	sformat(groupStr, sizeof(groupStr), "%d", groupId);

	const char *params[2] = { formation, groupStr };

	PGresult *res = PQexecParams(
		conn, "SELECT pgautofailover.get_group_hosts_hash($1, $2::int)",
		2, NULL, params, NULL, NULL, 0);

	bool queryOk = PQresultStatus(res) == PGRES_TUPLES_OK &&
				   PQntuples(res) == 1 && !PQgetisnull(res, 0, 0);
	char localHash[WS_HOSTS_HASH_LEN + 1] = { 0 };
	bool unchanged = queryOk && current_list_hash(listPath, localHash) &&
					 strcmp(PQgetvalue(res, 0, 0), localHash) == 0;

	PQclear(res);

	if (!queryOk)
	{
		mark_failure(listPath);
	}
	else if (unchanged)
	{
		(void) utimes(listPath, NULL);
		clear_failure(listPath);
	}
	else
	{
		res = PQexecParams(
			conn,
			"SELECT hash, array_to_string(hosts, E'\\n') "
			"FROM pgautofailover.get_group_hosts($1, $2::int)",
			2, NULL, params, NULL, NULL, 0);

		if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) == 1)
		{
			PQExpBuffer buffer = createPQExpBuffer();

			appendPQExpBuffer(buffer, WS_HOSTS_HASH_LINE_PREFIX "%s\n",
							  PQgetvalue(res, 0, 0));

			if (!PQgetisnull(res, 0, 1) && PQgetvalue(res, 0, 1)[0] != '\0')
			{
				appendPQExpBuffer(buffer, "%s\n", PQgetvalue(res, 0, 1));
			}

			if (!PQExpBufferBroken(buffer) &&
				ws_write_file_atomic(listPath, buffer->data, buffer->len))
			{
				clear_failure(listPath);
			}
			else
			{
				mark_failure(listPath);
			}

			destroyPQExpBuffer(buffer);
		}
		else
		{
			mark_failure(listPath);
		}

		PQclear(res);
	}

	PQfinish(conn);
}


static bool
key_is_plausible(const char *key)
{
	size_t len = strlen(key);

	if (len == 0 || len >= WS_REFRESH_MAX_KEY)
	{
		return false;
	}

	for (size_t i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char) key[i];

		if (c < 0x20 || c == 0x7f)
		{
			return false;
		}
	}

	return true;
}


void
ws_refresher_main(int sockFd, const char *routesPath,
				  const char *monitorUriPath)
{
	pid_t parent = getppid();

	signal(SIGCHLD, SIG_DFL);
	set_ps_title("pg_autoctl: walsender nodes list refresher");

	log_info("The nodes list refresher started (pid %d)", (int) getpid());

	int64_t nextHeartbeatMs = ws_monotonic_ms() + WS_REFRESH_FIRST_MS;

	while (!asked_to_stop && !asked_to_stop_fast && getppid() == parent)
	{
		int64_t now = ws_monotonic_ms();
		int timeoutMs = 1000;

		for (int i = 0; i < nStates; i++)
		{
			if (states[i].pending)
			{
				int64_t due = states[i].lastQueryMs + WS_REFRESH_COALESCE_MS;
				int64_t wait = due > now ? due - now : 0;

				timeoutMs = (int) Min(timeoutMs, wait);
			}
		}

		if (nextHeartbeatMs > now)
		{
			timeoutMs = (int) Min(timeoutMs, nextHeartbeatMs - now);
		}
		else
		{
			timeoutMs = 0;
		}

		struct pollfd pfd = { sockFd, POLLIN, 0 };

		(void) poll(&pfd, 1, timeoutMs);

		/* drain every waiting request; many for one route are one */
		for (;;)
		{
			char buf[WS_REFRESH_MAX_KEY + 8];
			ssize_t n = recv(sockFd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

			if (n < 0 && errno == EINTR)
			{
				continue;
			}

			if (n <= 0)
			{
				break;
			}

			buf[n] = '\0';

			if (!key_is_plausible(buf))
			{
				continue;
			}

			RouteState *st = state_for(buf);

			if (st != NULL)
			{
				st->pending = true;
			}
		}

		now = ws_monotonic_ms();

		if (now >= nextHeartbeatMs)
		{
			WsRoute *routes = NULL;
			int routeCount = 0;

			if (routes_load(routesPath, &routes, &routeCount))
			{
				for (int r = 0; r < routeCount; r++)
				{
					RouteState *st = state_for(routes[r].key);

					if (st != NULL)
					{
						st->pending = true;
					}
				}

				routes_free(routes);
			}

			nextHeartbeatMs = now + WS_REFRESH_HEARTBEAT_MS;
		}

		for (int i = 0; i < nStates; i++)
		{
			RouteState *st = &states[i];

			if (!st->pending ||
				now < st->lastQueryMs + WS_REFRESH_COALESCE_MS)
			{
				continue;
			}

			st->pending = false;

			/* negative cache: do not let requests cause connect attempts */
			if (now < negativeUntilMs)
			{
				continue;
			}

			refresh_route(routesPath, monitorUriPath, st->key);

			st->lastQueryMs = ws_monotonic_ms();
			now = st->lastQueryMs;
		}
	}

	_exit(0);
}
