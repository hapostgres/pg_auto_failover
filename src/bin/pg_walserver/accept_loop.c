/*
 * src/bin/pg_walserver/accept_loop.c
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
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "accept_loop.h"
#include "auth.h"
#include "capture.h"
#include "defaults.h"
#include "file_utils.h"
#include "framing.h"
#include "hba.h"
#include "log.h"
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

	/*
	 * routes/routeCount are the currently installed, already-validated
	 * snapshot of pg_walserver.ini (config->routes, loaded once at startup
	 * and swapped in atomically by a successful SIGHUP reload, see
	 * ws_reload_config() below) -- this child, forked after that swap (or
	 * before the next one), never re-reads the file off disk itself.
	 */
	const WsRoute *routes = config->routes;
	int routeCount = config->routeCount;

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
								   "pg_walserver only accepts simple query "
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

	close(clientSock);
}


/*
 * WsConnectionChildren is the "other" context ws_capture_tick() (capture.h)
 * hands connection_child_exited() below -- see that function's own comment
 * for why connection children are reaped through capture.c's own tick
 * rather than a second, independent waitpid(-1, ...) call site here.
 */
typedef struct WsConnectionChildren
{
	pid_t *children;
	int *count;
} WsConnectionChildren;


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
 * connection_child_exited is ws_capture_tick()'s otherChildExited callback
 * (capture.h): pid/status just came from the *one* wildcard waitpid(-1,
 * ...) call site this whole process makes (process_supervisor_tick(),
 * inside capture.c's ws_capture_tick()) -- deliberately not a second,
 * independent wildcard wait here, which would race that one for the same
 * exited child's status (see process_supervisor.h's own comment: whichever
 * reaper's waitpid() call happens to run first silently consumes the
 * zombie, permanently hiding that child's death from the other). Returns
 * true (and removes pid from the connection-children array) when pid is
 * one of ours; false otherwise, so ws_capture_tick() can fall through to
 * its own "unrecognized pid" handling (a supervised capturer's own exit,
 * or -- only possible when running as PID 1 -- an orphaned, reparented
 * grandchild).
 */
static bool
connection_child_exited(void *ctx, pid_t pid, int status)
{
	(void) status;

	WsConnectionChildren *conn = (WsConnectionChildren *) ctx;

	for (int i = 0; i < *(conn->count); i++)
	{
		if (conn->children[i] == pid)
		{
			remove_child(conn->children, conn->count, pid);
			return true;
		}
	}

	return false;
}


/*
 * log_route_diff logs a summary of what changed between the previously
 * installed route set and a freshly, successfully reloaded one: routes
 * added, removed, or changed (path/upstream/hostname/capture), compared by
 * key. Called only once both pg_walserver.ini and archiver-hba.conf have
 * re-parsed cleanly, right before the new routes are installed.
 */
static void
log_route_diff(const WsRoute *oldRoutes, int oldCount,
			   const WsRoute *newRoutes, int newCount)
{
	int added = 0, removed = 0, changed = 0;

	for (int i = 0; i < newCount; i++)
	{
		const WsRoute *old = routes_find_exact(oldRoutes, oldCount,
											   newRoutes[i].key);

		if (old == NULL)
		{
			++added;
			log_info("reload: route \"%s\" added (path \"%s\")",
					 newRoutes[i].key, newRoutes[i].path);
			continue;
		}

		if (strcmp(old->path, newRoutes[i].path) != 0 ||
			strcmp(old->upstream, newRoutes[i].upstream) != 0 ||
			strcmp(old->hostname, newRoutes[i].hostname) != 0 ||
			old->capturePull != newRoutes[i].capturePull)
		{
			++changed;
			log_info("reload: route \"%s\" changed (path \"%s\" -> \"%s\", "
					 "upstream \"%s\" -> \"%s\", hostname \"%s\" -> \"%s\", "
					 "capture %s -> %s)",
					 newRoutes[i].key, old->path, newRoutes[i].path,
					 old->upstream, newRoutes[i].upstream,
					 old->hostname, newRoutes[i].hostname,
					 old->capturePull ? "pull" : "none",
					 newRoutes[i].capturePull ? "pull" : "none");
		}
	}

	for (int i = 0; i < oldCount; i++)
	{
		if (routes_find_exact(newRoutes, newCount, oldRoutes[i].key) == NULL)
		{
			++removed;
			log_info("reload: route \"%s\" removed", oldRoutes[i].key);
		}
	}

	if (added == 0 && removed == 0 && changed == 0)
	{
		log_info("reload: routes unchanged (%d route%s)",
				 newCount, newCount == 1 ? "" : "s");
	}
	else
	{
		log_info("reload: routes: %d added, %d removed, %d changed "
				 "(%d total now)", added, removed, changed, newCount);
	}
}


/*
 * hba_ruleset_signature appends a stable, one-line-per-rule text rendering
 * of ruleSet to buf, used only to tell whether two rulesets are byte-for-
 * byte the same even when they happen to have the same rule count.
 */
static void
hba_ruleset_signature(const WsHbaRuleSet *ruleSet, PQExpBuffer buf)
{
	for (int i = 0; i < ruleSet->count; i++)
	{
		HbaRule *rule = &ruleSet->rules[i];

		appendPQExpBuffer(buf, "%s|%s|%s|%s|%d\n",
						  rule->fields[0], rule->fields[1],
						  rule->fields[2], rule->fields[3],
						  (int) rule->method);
	}
}


/*
 * log_hba_diff logs whether the HBA ruleset changed at all between the
 * previously installed one and a freshly, successfully reloaded one: a
 * simple rule-count-plus-content comparison (not a rule-by-rule diff --
 * see README.md's "Config reload" section for why this level of detail was
 * judged enough).
 */
static void
log_hba_diff(const WsHbaRuleSet *oldSet, const WsHbaRuleSet *newSet)
{
	PQExpBuffer oldSig = createPQExpBuffer();
	PQExpBuffer newSig = createPQExpBuffer();

	hba_ruleset_signature(oldSet, oldSig);
	hba_ruleset_signature(newSet, newSig);

	bool unchanged = !PQExpBufferBroken(oldSig) && !PQExpBufferBroken(newSig) &&
					 oldSig->len == newSig->len &&
					 memcmp(oldSig->data, newSig->data, oldSig->len) == 0;

	if (unchanged)
	{
		log_info("reload: HBA ruleset unchanged (%d rule%s)",
				 newSet->count, newSet->count == 1 ? "" : "s");
	}
	else
	{
		log_info("reload: HBA ruleset changed (%d rule%s before, %d after)",
				 oldSet->count, oldSet->count == 1 ? "" : "s", newSet->count);
	}

	destroyPQExpBuffer(oldSig);
	destroyPQExpBuffer(newSig);
}


/*
 * ws_reload_config is what a SIGHUP tick in ws_accept_loop()'s own main loop
 * calls: it re-reads and re-validates pg_walserver.ini (routes_load()) and
 * archiver-hba.conf (hba_parse_file()) from disk, and atomically swaps in
 * the new versions ONLY when both parse successfully -- exactly like
 * PostgreSQL's own SIGHUP-triggered ProcessConfigFile(), a bad reload is
 * refused, never partially applied, and the previous, already-validated
 * configuration keeps serving every connection. Reconciles the embedded
 * pull capturer set against the new routes (capture.c's ws_capture_reload())
 * once both files are known-good. The TLS certificate/key are never
 * touched here -- see the one-line note logged below.
 */
static void
ws_reload_config(WsServerConfig *config)
{
	static bool loggedTlsReloadNote = false;

	if (config->routesPath[0] == '\0')
	{
		log_info("Received SIGHUP: running with --insecure and no --pgdata, "
				 "nothing to reload");
		return;
	}

	log_info("Received SIGHUP: reloading \"%s\" and \"%s\"",
			 config->routesPath, config->auth.hbaPath);

	if (!loggedTlsReloadNote)
	{
		log_info("Note: the TLS certificate/key are not reloaded by SIGHUP "
				 "(a fresh SSL_CTX is only ever built at startup); restart "
				 "pg_walserver to pick up a rotated \"server.crt\"/"
				 "\"server.key\"");
		loggedTlsReloadNote = true;
	}

	WsRoute *newRoutes = NULL;
	int newRouteCount = 0;
	bool routesOk = routes_load(config->routesPath, &newRoutes, &newRouteCount);

	if (!routesOk)
	{
		log_error("Reload failed: could not parse \"%s\": keeping the "
				  "current configuration", config->routesPath);
	}

	WsHbaRuleSet newHbaRuleSet = { 0 };
	bool hbaOk = hba_parse_file(config->auth.hbaPath, &newHbaRuleSet);

	if (!hbaOk)
	{
		log_error("Reload failed: could not parse \"%s\": keeping the "
				  "current configuration", config->auth.hbaPath);
	}

	if (!routesOk || !hbaOk)
	{
		routes_free(newRoutes);
		hba_ruleset_free(&newHbaRuleSet);
		return;
	}

	log_route_diff(config->routes, config->routeCount, newRoutes, newRouteCount);
	log_hba_diff(&config->auth.hbaRuleSet, &newHbaRuleSet);

	/* never restart an already-running capturer just because SIGHUP fired;
	 * only reconcile against what actually changed */
	ws_capture_reload(newRoutes, newRouteCount);

	routes_free(config->routes);
	hba_ruleset_free(&config->auth.hbaRuleSet);

	config->routes = newRoutes;
	config->routeCount = newRouteCount;
	config->auth.hbaRuleSet = newHbaRuleSet;

	log_info("Reload complete: now serving %d route%s",
			 newRouteCount, newRouteCount == 1 ? "" : "s");
}


/*
 * ws_accept_loop is the whole server: it creates the listening socket, then
 * loops accepting connections, forking a child per connection (no exec(),
 * matching real Postgres's postmaster/BackendMain() split) and reaping
 * exited children, until asked to stop. Returns false only if the listening
 * socket itself could not be created; otherwise it runs until shutdown and
 * returns true.
 */
bool
ws_accept_loop(WsServerConfig *config)
{
	int listenSock = create_listen_socket(config->port);

	if (listenSock < 0)
	{
		return false;
	}

	set_signal_handlers(false);
	signal(SIGPIPE, SIG_IGN);

	pid_t children[WS_MAX_CONNECTIONS];
	int childCount = 0;
	WsConnectionChildren connChildren = { children, &childCount };

	log_info("pg_walserver listening on port %d%s%s",
			 config->port,
			 config->routesPath[0] != '\0' ? ", routes " : " (no routes file)",
			 config->routesPath[0] != '\0' ? config->routesPath : "");

	while (!asked_to_stop && !asked_to_stop_fast)
	{
		/*
		 * set_signal_handlers() (common/signals.c) already installs SIGHUP
		 * -> catch_reload(), which only sets this flag -- never do real work
		 * inside a signal handler. Checked once per loop iteration,
		 * alongside ws_capture_tick() below, exactly like real PostgreSQL
		 * checks its own ConfigReloadPending flag in its main loops.
		 */
		if (asked_to_reload)
		{
			asked_to_reload = 0;
			ws_reload_config(config);
		}

		/*
		 * One tick, one wildcard waitpid(-1, ...) call site for this whole
		 * process (capture.c's own ws_capture_tick(), process_supervisor.c
		 * underneath it): reaps and restarts-on-death every "capture =
		 * pull" route's own supervised pg_receivewal child (a completely
		 * independent lifecycle from the connection children below -- one
		 * long-lived child per active route, alive for the server's whole
		 * lifetime, not per accepted connection, see capture.c's own
		 * header comment), and hands any pid it doesn't recognize to
		 * connection_child_exited() above.
		 */
		ws_capture_tick(connection_child_exited, &connChildren);

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
		ws_capture_tick(connection_child_exited, &connChildren);

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
			 * inherits nothing but the client socket: the listening socket
			 * is closed.
			 */
			close(listenSock);

			signal(SIGCHLD, SIG_DFL);
			handle_connection(clientSock, config);
			_exit(0);
		}

		children[childCount++] = pid;

		/* parent: keep accepting; reap_children() collects the child */
		close(clientSock);
	}

	close(listenSock);

	/*
	 * Stop every "capture = pull" route's own supervised pg_receivewal
	 * child cleanly (SIGINT, a bounded wait, then SIGKILL if needed --
	 * capture.c's own ws_capture_stop_all()) before this process itself
	 * exits: the same shutdown path every other part of this server uses
	 * (asked_to_stop/asked_to_stop_fast, above), not a second mechanism.
	 */
	ws_capture_stop_all();

	log_info("pg_walserver shutting down");

	return true;
}
