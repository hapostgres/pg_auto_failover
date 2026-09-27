/*
 * src/bin/pg_walsender/refresher.c
 *   See refresher.h and monitor_hosts.h.
 *
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
#include "pgsql.h"
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


/*
 * The refresher's own connection to the monitor: opened once and kept alive
 * across ticks of ws_refresher_main's own loop (and across every route it
 * validates on a given tick), rather than reconnected on every single query
 * -- the refresher already runs its own periodic loop with a coalescing
 * timer per route and a negative cache on failure (mark_failure() below), so
 * a short-lived request/response connection buys nothing here, only extra
 * TCP and SCRAM round-trips on every tick. monitor_ensure_connection() is
 * the only place that (re)connects, and only does so when there is no
 * connection yet or the existing one is found dead.
 */
static PGSQL monitorPgsql = { 0 };
static bool monitorPgsqlReady = false;


/*
 * monitor_ensure_connection makes sure monitorPgsql is usable: initializes
 * it (from the URI on disk at monitorUriPath) the first time it is called,
 * or again whenever that URI changes or the previous connection is found
 * dead (PQstatus() != CONNECTION_OK); otherwise the existing, still-open
 * connection is left untouched and reused.
 *
 * The connection uses this project's own standard retry-policy mechanism
 * (pgsql_set_interactive_retry_policy: a bounded, backed-off handful of
 * attempts within pgconnect_timeout seconds) rather than a single bare
 * PQconnectdbParams() attempt, so one transient hiccup -- a dropped packet,
 * the monitor mid-restart -- does not by itself mark every route on this
 * tick as failed. The refresher's own outer loop, together with
 * mark_failure()'s negative cache, still provides the longer-horizon retry
 * for a real, sustained outage.
 *
 * Returns false when the URI file cannot be read or pgsql_init() rejects
 * the URI; the actual connection attempt (and its retries) only happens
 * lazily, inside pgsql_execute_with_params(), the first time a query is run
 * on this connection.
 */
static bool
monitor_ensure_connection(const char *monitorUriPath)
{
	char *uri = NULL;
	size_t size = 0;

	if (!ws_read_file_capped(monitorUriPath, WS_MAX_CONFIG_FILE_SIZE, true,
							 &uri, &size, NULL))
	{
		return false;
	}

	/* one line, no trailing newline */
	uri[strcspn(uri, "\r\n")] = '\0';

	bool needsInit = !monitorPgsqlReady ||
					 strcmp(monitorPgsql.connectionString, uri) != 0;

	if (!needsInit && monitorPgsql.connection != NULL &&
		PQstatus(monitorPgsql.connection) != CONNECTION_OK)
	{
		/* the long-lived connection died since the previous tick */
		pgsql_finish(&monitorPgsql);
		needsInit = true;
	}

	if (needsInit)
	{
		pgsql_finish(&monitorPgsql);

		if (!pgsql_init(&monitorPgsql, uri, PGSQL_CONN_MONITOR))
		{
			free(uri);
			return false;
		}

		/* pgsql_finish() (just above, and internally on a single-statement
		 * failure) always resets this to SINGLE_STATEMENT, so it must be set
		 * again on every (re)init to keep the connection open across calls */
		monitorPgsql.connectionStatementType = PGSQL_CONNECTION_MULTI_STATEMENT;

		pgsql_set_interactive_retry_policy(&monitorPgsql.retryPolicy);

		monitorPgsqlReady = true;
	}

	free(uri);

	return true;
}


/* the single-column result of get_group_hosts_hash(), or the first column
 * of get_group_hosts() */
typedef struct WsHostsHashContext
{
	char sqlstate[SQLSTATE_LENGTH];
	bool parsedOk;
	char hash[WS_HOSTS_HASH_LEN + 1];
} WsHostsHashContext;


/*
 * parse_hosts_hash_result is a pgsql_execute_with_params() parse callback:
 * it expects exactly one row with a non-NULL text hash in column 0, the
 * shape of both SELECT pgautofailover.get_group_hosts_hash(...) and the
 * "hash" column of SELECT ... FROM pgautofailover.get_group_hosts(...).
 */
static void
parse_hosts_hash_result(void *ctx, PGresult *result)
{
	WsHostsHashContext *context = (WsHostsHashContext *) ctx;

	context->parsedOk = false;

	if (PQntuples(result) != 1 || PQgetisnull(result, 0, 0))
	{
		return;
	}

	strlcpy(context->hash, PQgetvalue(result, 0, 0), sizeof(context->hash));
	context->parsedOk = true;
}


/*
 * monitor_get_group_hosts_hash runs
 * "SELECT pgautofailover.get_group_hosts_hash($1, $2::int)" for (formation,
 * groupId) on the refresher's own monitor connection and copies the result
 * into hashOut (a buffer of at least WS_HOSTS_HASH_LEN + 1 bytes). Returns
 * false on any connection or query failure, or when the monitor returned no
 * row or a NULL hash.
 */
static bool
monitor_get_group_hosts_hash(const char *formation, int groupId, char *hashOut)
{
	char groupStr[16];

	sformat(groupStr, sizeof(groupStr), "%d", groupId);

	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2] = { formation, groupStr };
	WsHostsHashContext context = {
		{ 0 }, false, { 0 }
	};

	if (!pgsql_execute_with_params(
			&monitorPgsql,
			"SELECT pgautofailover.get_group_hosts_hash($1, $2::int)",
			2, paramTypes, paramValues,
			&context, &parse_hosts_hash_result) ||
		!context.parsedOk)
	{
		return false;
	}

	strlcpy(hashOut, context.hash, WS_HOSTS_HASH_LEN + 1);

	return true;
}


/* the two-column result of get_group_hosts(): the hash, and the hosts text
 * blob (one host per line), which may legitimately be empty */
typedef struct WsHostsListContext
{
	char sqlstate[SQLSTATE_LENGTH];
	bool parsedOk;
	char hash[WS_HOSTS_HASH_LEN + 1];
	char *hosts;             /* strdup'd; NULL when empty; caller frees */
} WsHostsListContext;


/*
 * parse_hosts_list_result is a pgsql_execute_with_params() parse callback
 * for "SELECT hash, array_to_string(hosts, E'\n') FROM
 * pgautofailover.get_group_hosts($1, $2::int)": one row, hash in column 0
 * (must be non-NULL), the newline-joined hosts list in column 1 (NULL or
 * empty is a legitimate "no hosts yet").
 */
static void
parse_hosts_list_result(void *ctx, PGresult *result)
{
	WsHostsListContext *context = (WsHostsListContext *) ctx;

	context->parsedOk = false;
	context->hosts = NULL;

	if (PQntuples(result) != 1 || PQgetisnull(result, 0, 0))
	{
		return;
	}

	strlcpy(context->hash, PQgetvalue(result, 0, 0), sizeof(context->hash));

	if (!PQgetisnull(result, 0, 1) && PQgetvalue(result, 0, 1)[0] != '\0')
	{
		context->hosts = strdup(PQgetvalue(result, 0, 1));
	}

	context->parsedOk = true;
}


/*
 * monitor_get_group_hosts runs "SELECT hash, array_to_string(hosts, E'\n')
 * FROM pgautofailover.get_group_hosts($1, $2::int)" for (formation, groupId)
 * on the refresher's own monitor connection, copies the hash into hashOut (a
 * buffer of at least WS_HOSTS_HASH_LEN + 1 bytes) and sets *hostsOut to a
 * malloc'd copy of the hosts text (NULL when there are none yet; the caller
 * must free() a non-NULL result). Returns false on any connection or query
 * failure, or when the monitor returned no row or a NULL hash.
 */
static bool
monitor_get_group_hosts(const char *formation, int groupId,
						char *hashOut, char **hostsOut)
{
	char groupStr[16];

	sformat(groupStr, sizeof(groupStr), "%d", groupId);

	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2] = { formation, groupStr };
	WsHostsListContext context = { { 0 }, false, { 0 }, NULL };

	if (!pgsql_execute_with_params(
			&monitorPgsql,
			"SELECT hash, array_to_string(hosts, E'\\n') "
			"FROM pgautofailover.get_group_hosts($1, $2::int)",
			2, paramTypes, paramValues,
			&context, &parse_hosts_list_result) ||
		!context.parsedOk)
	{
		free(context.hosts);
		return false;
	}

	strlcpy(hashOut, context.hash, WS_HOSTS_HASH_LEN + 1);
	*hostsOut = context.hosts;

	return true;
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

	(void) write_file_atomic((char *) "monitor unreachable\n",
							 strlen("monitor unreachable\n"), errPath);
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
 * atomic rewrite when they differ. Connection setup (monitor_ensure_
 * connection), running one query, and parsing its one result are each
 * split into their own function above, the same way monitor.c's own
 * monitor_get_*() functions are structured around pgsql_execute_with_params.
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

	if (!monitor_ensure_connection(monitorUriPath))
	{
		mark_failure(listPath);
		return;
	}

	char monitorHash[WS_HOSTS_HASH_LEN + 1];

	if (!monitor_get_group_hosts_hash(formation, groupId, monitorHash))
	{
		mark_failure(listPath);
		return;
	}

	char localHash[WS_HOSTS_HASH_LEN + 1] = { 0 };
	bool unchanged = current_list_hash(listPath, localHash) &&
					 strcmp(monitorHash, localHash) == 0;

	if (unchanged)
	{
		(void) utimes(listPath, NULL);
		clear_failure(listPath);
		return;
	}

	char hash[WS_HOSTS_HASH_LEN + 1];
	char *hosts = NULL;

	if (!monitor_get_group_hosts(formation, groupId, hash, &hosts))
	{
		mark_failure(listPath);
		return;
	}

	PQExpBuffer buffer = createPQExpBuffer();

	appendPQExpBuffer(buffer, WS_HOSTS_HASH_LINE_PREFIX "%s\n", hash);

	if (hosts != NULL)
	{
		appendPQExpBuffer(buffer, "%s\n", hosts);
	}

	if (!PQExpBufferBroken(buffer) &&
		write_file_atomic(buffer->data, buffer->len, listPath))
	{
		clear_failure(listPath);
	}
	else
	{
		mark_failure(listPath);
	}

	destroyPQExpBuffer(buffer);
	free(hosts);
}


/*
 * key_is_plausible is a cheap sanity check on a route key read off the
 * refresher's own datagram socket: non-empty, short enough to fit a
 * RouteState, and free of control characters -- a malformed or truncated
 * datagram is dropped rather than stored or logged verbatim.
 */
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
