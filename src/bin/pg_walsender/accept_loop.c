/*
 * src/bin/pg_walsender/accept_loop.c
 *   See accept_loop.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "accept_loop.h"
#include "auth.h"
#include "cmd_fetch_file.h"
#include "defaults.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "repl_command.h"
#include "routes.h"
#include "signals.h"
#include "startup.h"

/* dbname prefix that routes a connection to the FETCH_FILE side-channel
 * instead of the normal replication command loop -- see cmd_fetch_file.h */
#define WS_FETCH_DBNAME_PREFIX "fetch/"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/*
 * A real, unmodified Postgres standby's own internal walreceiver process
 * (primary_conninfo-driven physical replication) always sends this literal
 * string as its startup packet's dbname -- confirmed against a real
 * standby: it does not forward whatever dbname the operator wrote into
 * primary_conninfo the way a generic libpq client (psql, pg_receivewal,
 * this project's own FETCH_FILE client) does. See the routeKey fallback
 * below.
 */
#define WS_REAL_WALRECEIVER_DBNAME "replication"


/*
 * Hardening limits: a peer that connects and then says nothing must not
 * hold a forked child forever, and a flood of connections must not fork
 * without bound.
 */
#define WS_STARTUP_TIMEOUT_SECONDS 30
#define WS_MAX_CONNECTIONS 64

static volatile sig_atomic_t activeChildren = 0;


/*
 * reap_children is the SIGCHLD handler: collect every exited child and
 * keep activeChildren accurate so the accept loop can enforce
 * WS_MAX_CONNECTIONS.
 */
static void
reap_children(int signo)
{
	int savedErrno = errno;

	while (waitpid(-1, NULL, WNOHANG) > 0)
	{
		if (activeChildren > 0)
		{
			activeChildren--;
		}
	}

	errno = savedErrno;
}


static void
set_receive_timeout(int sock, int seconds)
{
	struct timeval tv = { seconds, 0 };

	if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
	{
		log_warn("Failed to set SO_RCVTIMEO on the client socket: %m");
	}
}


static int
create_listen_socket(int port)
{
	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if (sock < 0)
	{
		log_error("Failed to create the listening socket: %m");
		return -1;
	}

	int reuse = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
	{
		log_warn("Failed to set SO_REUSEADDR on the listening socket: %m");
	}

	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		log_error("Failed to bind port %d: %m", port);
		close(sock);
		return -1;
	}

	if (listen(sock, 64) != 0)
	{
		log_error("Failed to listen on port %d: %m", port);
		close(sock);
		return -1;
	}

	return sock;
}


/*
 * handle_connection runs the full lifecycle of one accepted connection:
 * startup negotiation, routes-based auth, the initial handshake messages a
 * real client expects (AuthenticationOk/ParameterStatus/BackendKeyData/
 * ReadyForQuery), and then the simple-query command loop replication
 * connections use (see pgsql.c's own comment elsewhere in this project:
 * "extended query protocol not supported in a replication connection").
 * Runs entirely inside the forked child; the caller _exit()s right after.
 */
static void
handle_connection(int clientSock, const WsServerConfig *config)
{
	WsStartupParams params;

	/* startup and auth must complete promptly; cleared once authenticated */
	set_receive_timeout(clientSock, WS_STARTUP_TIMEOUT_SECONDS);

	if (!ws_startup_negotiate(clientSock, &params))
	{
		close(clientSock);
		return;
	}

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (config->routesPath[0] != '\0')
	{
		if (!routes_load(config->routesPath, &routes, &routeCount))
		{
			close(clientSock);
			return;
		}
	}

	bool isFetchMode = (strncmp(params.database, WS_FETCH_DBNAME_PREFIX,
								strlen(WS_FETCH_DBNAME_PREFIX)) == 0);
	const char *routeKey = isFetchMode
						   ? params.database + strlen(WS_FETCH_DBNAME_PREFIX)
						   : params.database;

	/*
	 * dbname-based routing cannot work for a real walreceiver connection
	 * (see WS_REAL_WALRECEIVER_DBNAME's own comment) -- fall back to the
	 * single configured route unambiguously, matching the
	 * one-membership-per-archiver scope. Multiple routes with a real
	 * walreceiver connecting is left as a clean auth rejection (routeKey
	 * stays "replication", which never matches a real route.key) rather
	 * than guessing; a multi-route archiver needs a different mechanism
	 * for a real standby to identify its route (e.g. application_name,
	 * which real walreceiver does forward from primary_conninfo, unlike
	 * dbname) -- not supported yet.
	 */
	if (!isFetchMode &&
		streq(routeKey, WS_REAL_WALRECEIVER_DBNAME) &&
		routeCount == 1)
	{
		routeKey = routes[0].key;
	}

	const WsRoute *route = NULL;

	if (!ws_authenticate(clientSock, &params, routeKey, routes, routeCount,
						 &(config->auth), &route))
	{
		routes_free(routes);
		close(clientSock);
		return;
	}

	set_receive_timeout(clientSock, 0);

	char title[256];

	sformat(title, sizeof(title), "pg_autoctl: walsender %s%s",
			isFetchMode ? "fetch " : "", route != NULL ? route->key : routeKey);
	set_ps_title(title);

	if (isFetchMode)
	{
		cmd_fetch_file(clientSock, route);
		routes_free(routes);
		close(clientSock);
		return;
	}

	if (!ws_send_authentication_ok(clientSock) ||
		!ws_send_parameter_status(clientSock, "server_version", WS_SERVER_VERSION) ||
		!ws_send_parameter_status(clientSock, "client_encoding", "UTF8") ||
		!ws_send_parameter_status(clientSock, "server_encoding", "UTF8") ||
		!ws_send_parameter_status(clientSock, "integer_datetimes", "on") ||
		!ws_send_parameter_status(clientSock, "default_transaction_read_only", "off") ||
		!ws_send_backend_key_data(clientSock, getpid(), 0) ||
		!ws_send_ready_for_query(clientSock))
	{
		routes_free(routes);
		close(clientSock);
		return;
	}

	for (;;)
	{
		char type;
		char *payload = NULL;
		int32_t payloadLen = 0;

		if (!ws_read_message(clientSock, &type, &payload, &payloadLen))
		{
			free(payload);
			break;
		}

		if (type == 'X')       /* Terminate */
		{
			free(payload);
			break;
		}

		if (type != 'Q')       /* Query -- the only message replication
		                        * connections send commands through */
		{
			ws_send_error_response(clientSock, "08P01",
								   "pg_walsender only accepts simple query "
								   "protocol messages");
			free(payload);
			break;
		}

		WsCommand cmd;

		if (!repl_command_parse(payload, &cmd))
		{
			ws_send_error_response(clientSock, "42601",
								   "unrecognized replication command");
		}
		else
		{
			ws_dispatch_command(clientSock, &cmd, route,
								params.replicationDatabase ? params.database : NULL);
		}

		free(payload);

		if (!ws_send_ready_for_query(clientSock))
		{
			break;
		}
	}

	routes_free(routes);
	close(clientSock);
}


bool
ws_accept_loop(const WsServerConfig *config)
{
	int listenSock = create_listen_socket(config->port);

	if (listenSock < 0)
	{
		return false;
	}

	set_signal_handlers(false);

	/* reap children and track how many are alive, see reap_children() */
	struct sigaction reapAction;

	memset(&reapAction, 0, sizeof(reapAction));
	reapAction.sa_handler = reap_children;
	sigemptyset(&reapAction.sa_mask);
	reapAction.sa_flags = SA_RESTART | SA_NOCLDSTOP;
	sigaction(SIGCHLD, &reapAction, NULL);

	log_info("pg_walsender listening on port %d%s%s",
			 config->port,
			 config->routesPath[0] != '\0' ? ", routes " : " (no routes file)",
			 config->routesPath[0] != '\0' ? config->routesPath : "");

	while (!asked_to_stop && !asked_to_stop_fast)
	{
		/*
		 * pqsignal() (signals.c, via postgres_fe.h) installs our handlers
		 * with SA_RESTART, so a blocking accept() is never interrupted by
		 * SIGTERM -- it would just keep sleeping through shutdown forever.
		 * Poll with a short timeout instead, so the loop condition above
		 * gets re-checked promptly after asked_to_stop is set.
		 */
		fd_set readSet;

		FD_ZERO(&readSet);
		FD_SET(listenSock, &readSet);

		struct timeval timeout = { 1, 0 };   /* 1 second */

		int selectRet = select(listenSock + 1, &readSet, NULL, NULL, &timeout);

		if (selectRet < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}

			log_error("select() failed: %m");
			sleep(1);       /* never spin on a persistent error */
			continue;
		}

		if (selectRet == 0)
		{
			/* timed out, no pending connection -- loop back to the
			 * asked_to_stop check above */
			continue;
		}

		struct sockaddr_storage clientAddr;
		socklen_t clientAddrLen = sizeof(clientAddr);

		int clientSock = accept(listenSock,
								(struct sockaddr *) &clientAddr,
								&clientAddrLen);

		if (clientSock < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}

			log_error("accept() failed: %m");
			sleep(1);       /* never spin on a persistent error */
			continue;
		}

		if (activeChildren >= WS_MAX_CONNECTIONS)
		{
			log_warn("Rejecting a connection: %d connections already open",
					 (int) activeChildren);
			close(clientSock);
			continue;
		}

		(void) fcntl(clientSock, F_SETFD, FD_CLOEXEC);

		activeChildren++;

		pid_t pid = fork();

		if (pid == -1)
		{
			log_error("fork() failed: %m");
			activeChildren--;
			close(clientSock);
			continue;
		}

		if (pid == 0)
		{
			/*
			 * Child: no exec(), just call straight into the connection
			 * handler -- matches real Postgres's BackendMain() model.
			 */
			close(listenSock);
			signal(SIGCHLD, SIG_DFL);
			handle_connection(clientSock, config);
			_exit(0);
		}

		/* parent: keep accepting; reap_children() collects the child */
		close(clientSock);
	}

	close(listenSock);
	log_info("pg_walsender shutting down");

	return true;
}
