/*
 * src/bin/pg_walsender/accept_loop.c
 *   See accept_loop.h.
 *
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
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "accept_loop.h"
#include "auth.h"
#include "defaults.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "refresher.h"
#include "repl_command.h"
#include "routes.h"
#include "signals.h"
#include "startup.h"
#include "ws_util.h"


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
 * Hardening limits: a flood of connections must not fork without bound.
 * Like the postmaster's own child list, the live children are kept in an
 * array of pids, reaped in the main loop (never from a signal handler) and
 * counted from that array.
 */
#define WS_MAX_CONNECTIONS 64

/* the refresher is restarted at most this often when it keeps dying */
#define WS_REFRESHER_RESTART_MIN_MS 1000


/*
 * auth_timeout_handler is the connection's absolute authentication
 * deadline: PostgreSQL arms authentication_timeout (STARTUP_PACKET_TIMEOUT
 * before v14's rework) the same way, for the whole startup packet, TLS
 * handshake, HBA and password exchange, and its handler simply exits the
 * backend. Only async-signal-safe calls here: _exit().
 */
static void
auth_timeout_handler(int signo)
{
	_exit(1);
}


/*
 * create_listen_socket creates, binds (SO_REUSEADDR, INADDR_ANY) and
 * listen()s on a TCP socket for the given port. Returns -1 on any failure,
 * having logged it and cleaned up the socket.
 */
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

	/*
	 * ABSOLUTE deadline on everything up to authentication success:
	 * startup packet, SSLRequest/TLS handshake, HBA (DNS included), SCRAM.
	 * Not a per-read idle timeout, which a slow-loris client keeps
	 * resetting. Cancelled right after authentication succeeded.
	 */
	struct sigaction alarmAction;

	memset(&alarmAction, 0, sizeof(alarmAction));
	alarmAction.sa_handler = auth_timeout_handler;
	sigemptyset(&alarmAction.sa_mask);
	sigaction(SIGALRM, &alarmAction, NULL);

	ws_auth_deadline_set(config->authTimeout);
	alarm((unsigned int) config->authTimeout);

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

	const char *routeKey = params.database;

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
	if (streq(routeKey, WS_REAL_WALRECEIVER_DBNAME) && routeCount == 1)
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

	alarm(0);
	ws_auth_deadline_clear();

	char title[256];
	char safeKey[NAMEDATALEN + 24];

	ws_sanitize_for_log(route != NULL ? route->key : routeKey,
						safeKey, sizeof(safeKey));
	sformat(title, sizeof(title), "pg_autoctl: walsender %s", safeKey);
	set_ps_title(title);

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

		if (!ws_read_message(clientSock, &type, &payload, &payloadLen,
							 WS_MAX_COMMAND_MESSAGE_LEN))
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

		/*
		 * A command that failed inside COPY/streaming cannot resynchronize
		 * the protocol: like PostgreSQL's walsender (a FATAL there), end the
		 * connection instead of announcing ReadyForQuery.
		 */
		if (ws_connection_close_after_command ||
			!ws_send_ready_for_query(clientSock))
		{
			break;
		}
	}

	routes_free(routes);
	close(clientSock);
}


/*
 * remove_child drops pid from the children array (if present), replacing it
 * with the last live entry and shrinking *count -- order among children is
 * never meaningful, so this O(1) swap-and-shrink is fine.
 */
static void
remove_child(pid_t *children, int *count, pid_t pid)
{
	for (int i = 0; i < *count; i++)
	{
		if (children[i] == pid)
		{
			children[i] = children[--(*count)];
			return;
		}
	}
}


/*
 * reap_children collects every exited child, in normal (main loop) context
 * like the postmaster's own CleanupBackend(): connection children leave the
 * array, and the refresher is noted as gone so it gets restarted.
 */
static void
reap_children(pid_t *children, int *count, pid_t *refresherPid)
{
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
	{
		if (pid == *refresherPid)
		{
			log_warn("The nodes list refresher (pid %d) exited (status %d), "
					 "restarting it", (int) pid, status);
			*refresherPid = 0;
		}
		else
		{
			remove_child(children, count, pid);
		}
	}
}


/*
 * start_refresher forks the single writer of the nodes lists, only once a
 * monitor URI exists to talk to the monitor with.
 */
static pid_t
start_refresher(const WsServerConfig *config, int refreshSock, int listenSock)
{
	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("fork() of the nodes list refresher failed: %m");
		return 0;
	}

	if (pid == 0)
	{
		close(listenSock);
		ws_refresher_main(refreshSock, config->routesPath,
						  config->auth.monitorUriPath);
	}

	return pid;
}


/*
 * stop_refresher asks the refresher child to stop (SIGTERM), waits up to
 * about 5 seconds (100 * 50ms) for it to exit, and SIGKILLs it if it hasn't
 * -- called during shutdown, so this may block the parent briefly rather
 * than leaving a zombie or an orphaned refresher behind.
 */
static void
stop_refresher(pid_t refresherPid)
{
	if (refresherPid <= 0)
	{
		return;
	}

	kill(refresherPid, SIGTERM);

	for (int i = 0; i < 100; i++)
	{
		if (waitpid(refresherPid, NULL, WNOHANG) != 0)
		{
			return;
		}

		usleep(50 * 1000);
	}

	kill(refresherPid, SIGKILL);
	(void) waitpid(refresherPid, NULL, 0);
}


/*
 * ws_accept_loop is the whole server: it creates the listening socket and
 * (when a monitor URI is configured) the refresher's own datagram socket,
 * then loops accepting connections, forking a child per connection (no
 * exec(), matching real Postgres's postmaster/BackendMain() split), reaping
 * exited children and restarting the refresher if it dies, until asked to
 * stop. Returns false only if the listening socket itself could not be
 * created; otherwise it runs until shutdown and returns true.
 */
bool
ws_accept_loop(const WsServerConfig *config)
{
	int listenSock = create_listen_socket(config->port);

	if (listenSock < 0)
	{
		return false;
	}

	set_signal_handlers(false);
	signal(SIGPIPE, SIG_IGN);

	/*
	 * The refresher's datagram socket is created by this parent BEFORE any
	 * fork, so a restarted refresher gets the very same socket (requests
	 * queue meanwhile) and the connection children only know its path.
	 */
	int refreshSock = -1;

	if (config->auth.refreshSockPath[0] != '\0')
	{
		refreshSock = ws_refresh_socket_create(config->auth.refreshSockPath);

		if (refreshSock < 0)
		{
			log_warn("Running without a nodes list refresher: the \"monitor\" "
					 "HBA address will rely on the list as it is");
		}
	}

	pid_t children[WS_MAX_CONNECTIONS];
	int childCount = 0;
	pid_t refresherPid = 0;
	int64_t nextRefresherStartMs = 0;

	log_info("pg_walsender listening on port %d%s%s",
			 config->port,
			 config->routesPath[0] != '\0' ? ", routes " : " (no routes file)",
			 config->routesPath[0] != '\0' ? config->routesPath : "");

	while (!asked_to_stop && !asked_to_stop_fast)
	{
		reap_children(children, &childCount, &refresherPid);

		if (refreshSock >= 0 && refresherPid == 0 &&
			ws_monotonic_ms() >= nextRefresherStartMs &&
			file_exists(config->auth.monitorUriPath))
		{
			refresherPid = start_refresher(config, refreshSock, listenSock);
			nextRefresherStartMs = ws_monotonic_ms() +
								   WS_REFRESHER_RESTART_MIN_MS;
		}

		/*
		 * pqsignal() (signals.c, via postgres_fe.h) installs our handlers
		 * with SA_RESTART, so a blocking accept() is never interrupted by
		 * SIGTERM -- it would just keep sleeping through shutdown forever.
		 * Poll with a short timeout instead (which is also what bounds the
		 * delay before an exited child is reaped and counted out), so the
		 * loop condition above gets re-checked promptly.
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
			/* timed out, no pending connection -- loop back to reap and to
			 * the asked_to_stop check above */
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

		/* children that exited meanwhile must not count against the cap */
		reap_children(children, &childCount, &refresherPid);

		if (childCount >= WS_MAX_CONNECTIONS)
		{
			log_warn("Rejecting a connection: %d connections already open",
					 childCount);
			close(clientSock);
			continue;
		}

		(void) fcntl(clientSock, F_SETFD, FD_CLOEXEC);

		fflush(stdout);
		fflush(stderr);

		pid_t pid = fork();

		if (pid == -1)
		{
			log_error("fork() failed: %m");
			close(clientSock);
			continue;
		}

		if (pid == 0)
		{
			/*
			 * Child: no exec(), just call straight into the connection
			 * handler -- matches real Postgres's BackendMain() model. It
			 * inherits nothing but the client socket: the listening and
			 * refresher sockets are closed.
			 */
			close(listenSock);

			if (refreshSock >= 0)
			{
				close(refreshSock);
			}

			signal(SIGCHLD, SIG_DFL);
			handle_connection(clientSock, config);
			_exit(0);
		}

		children[childCount++] = pid;

		/* parent: keep accepting; reap_children() collects the child */
		close(clientSock);
	}

	close(listenSock);
	stop_refresher(refresherPid);

	if (refreshSock >= 0)
	{
		close(refreshSock);
		(void) unlink(config->auth.refreshSockPath);
	}

	log_info("pg_walsender shutting down");

	return true;
}
