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
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "accept_loop.h"
#include "auth.h"
#include "backup_bootstrap.h"
#include "receivewal.h"
#include "cli_basebackup.h"
#include "defaults.h"
#include "file_utils.h"
#include "framing.h"
#include "hba.h"
#include "tls.h"
#include "log.h"
#include "ps_state.h"
#include "repl_command.h"
#include "clusters.h"
#include "signals.h"
#include "startup.h"
#include "string_utils.h"
#include "wal_dir_scan.h"
#include "ws_util.h"


#ifndef streq
#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))
#endif

/*
 * A real, unmodified Postgres standby's own internal walreceiver process
 * (primary_conninfo-driven physical replication) always sends this literal
 * string as its startup packet's dbname -- confirmed against a real
 * standby: it does not forward whatever dbname the operator wrote into
 * primary_conninfo the way a generic libpq client (psql, pg_receivewal,
 * this project's own FETCH_FILE client) does. See the clusterKey fallback
 * below.
 */
#define WS_REAL_WALRECEIVER_DBNAME "replication"

/* forward declaration: ws_reload_config() below calls this, but it is
 * defined further down, right before ws_accept_loop() -- see its own
 * header comment */
static void refresh_ps_state(const WsServerConfig *config, pid_t servePid,
							 time_t serveStartedAt);

/* this process's own pid/start time, set once at the top of ws_accept_loop()
 * and read by both refresh_ps_state() and ws_reload_config() */
static pid_t gServePid = 0;
static time_t gServeStartedAt = 0;


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
 * startup negotiation, clusters-based auth, the initial handshake messages a
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
	 * clusters/clusterCount are the currently installed, already-validated
	 * snapshot of pg_walserver.ini (config->clusters, loaded once at startup
	 * and swapped in atomically by a successful SIGHUP reload, see
	 * ws_reload_config() below) -- this child, forked after that swap (or
	 * before the next one), never re-reads the file off disk itself.
	 */
	const WsCluster *clusters = config->clusters;
	int clusterCount = config->clusterCount;

	const char *clusterKey = params.database;

	/*
	 * dbname-based addressing cannot work for a real walreceiver connection
	 * (see WS_REAL_WALRECEIVER_DBNAME's own comment) -- fall back to the
	 * single configured cluster unambiguously, matching the
	 * one-membership-per-archiver scope. Multiple clusters with a real
	 * walreceiver connecting is left as a clean auth rejection (clusterKey
	 * stays "replication", which never matches a real cluster.key) rather
	 * than guessing; a multi-cluster archiver needs a different mechanism
	 * for a real standby to identify its cluster (e.g. application_name,
	 * which real walreceiver does forward from primary_conninfo, unlike
	 * dbname) -- not supported yet.
	 */
	if (streq(clusterKey, WS_REAL_WALRECEIVER_DBNAME) && clusterCount == 1)
	{
		clusterKey = clusters[0].key;
	}

	const WsCluster *cluster = NULL;

	if (!ws_authenticate(clientSock, &params, clusterKey, clusters, clusterCount,
						 &(config->auth), &cluster))
	{
		close(clientSock);
		return;
	}

	alarm(0);
	ws_auth_deadline_clear();

	char title[256];
	char safeKey[NAMEDATALEN + 24];

	sanitizeForLog(cluster != NULL ? cluster->key : clusterKey,
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
			ws_dispatch_command(clientSock, &cmd, cluster,
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
 * WsConnectionChildren is the "other" context ws_receivewal_tick() (receivewal.h)
 * hands connection_child_exited() below -- see that function's own comment
 * for why connection children are reaped through receivewal.c's own tick
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
 * connection_child_exited is ws_receivewal_tick()'s otherChildExited callback
 * (receivewal.h): pid/status just came from the *one* wildcard waitpid(-1,
 * ...) call site this whole process makes (process_supervisor_tick(),
 * inside receivewal.c's ws_receivewal_tick()) -- deliberately not a second,
 * independent wildcard wait here, which would race that one for the same
 * exited child's status (see process_supervisor.h's own comment: whichever
 * reaper's waitpid() call happens to run first silently consumes the
 * zombie, permanently hiding that child's death from the other). Returns
 * true (and removes pid from the connection-children array) when pid is
 * one of ours; false otherwise, so ws_receivewal_tick() can fall through to
 * its own "unrecognized pid" handling (a supervised receivewal worker's own exit,
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
 * bootstrapChildren tracks every still-running ws_backup_bootstrap_start()
 * child (backup_bootstrap.c) -- a third, independent kind of child this
 * process forks, alongside the embedded receivewal workers (receivewal.c's own
 * receivewalSupervisor) and per-connection children (WsConnectionChildren
 * above). Reaped through the exact same single wildcard reaper as both of
 * those (see ws_receivewal_tick()'s own header comment on why there is only
 * ever one waitpid(-1, ...) call site in this whole process): without
 * tracking these pids here too, process_supervisor_tick() would hand them
 * to process_supervisor_log_unknown_pid() as an "unknown subprocess",
 * logged as an ERROR outside of PID 1 -- misleading for an expected,
 * successfully-reaped child of our own.
 */
static pid_t bootstrapChildren[WS_MAX_CONNECTIONS];
static char bootstrapChildClusters[WS_MAX_CONNECTIONS][NAMEDATALEN + 16];
static time_t bootstrapChildStartedAt[WS_MAX_CONNECTIONS];
static int bootstrapChildCount = 0;


/*
 * bootstrap_child_exited is bootstrap_or_connection_child_exited()'s own
 * half of the otherChildExited chain -- see bootstrapChildren's own comment
 * just above. Keeps the three parallel arrays (pid/cluster/startedAt) in
 * sync: the same swap-with-last removal, applied to all three at once.
 */
static bool
bootstrap_child_exited(pid_t pid)
{
	for (int i = 0; i < bootstrapChildCount; i++)
	{
		if (bootstrapChildren[i] == pid)
		{
			int last = --bootstrapChildCount;

			bootstrapChildren[i] = bootstrapChildren[last];
			strlcpy(bootstrapChildClusters[i], bootstrapChildClusters[last],
					sizeof(bootstrapChildClusters[i]));
			bootstrapChildStartedAt[i] = bootstrapChildStartedAt[last];
			return true;
		}
	}

	return false;
}


/*
 * ws_bootstrap_get_status fills out[0..min(bootstrapChildCount,maxOut))
 * with every currently in-flight automatic bootstrap backup job, and
 * returns how many entries it filled. Used by refresh_ps_state()
 * (accept_loop.c) to keep the on-disk ps state file (ps_state.h) current.
 */
int
ws_bootstrap_get_status(WsBootstrapStatus *out, int maxOut)
{
	int n = 0;

	for (int i = 0; i < bootstrapChildCount && n < maxOut; i++)
	{
		strlcpy(out[n].clusterKey, bootstrapChildClusters[i],
				sizeof(out[n].clusterKey));
		out[n].pid = bootstrapChildren[i];
		out[n].startedAt = bootstrapChildStartedAt[i];
		n++;
	}

	return n;
}


/*
 * bootstrap_or_connection_child_exited is the single otherChildExited
 * callback ws_receivewal_tick() is actually given below: it chains connection_
 * child_exited() (ctx: the WsConnectionChildren array) and bootstrap_child_
 * exited() (its own file-scope array), so a pid recognized by either one is
 * reaped quietly -- never handed further down to process_supervisor_tick()'s
 * own "unknown subprocess" logging.
 */
static bool
bootstrap_or_connection_child_exited(void *ctx, pid_t pid, int status)
{
	if (connection_child_exited(ctx, pid, status))
	{
		return true;
	}

	return bootstrap_child_exited(pid);
}


/*
 * ws_bootstrap_missing_backups checks every cluster in clusters[0..clusterCount)
 * for whether it already has a base backup (cli_basebackup_cluster_has_
 * backup(), cli_basebackup.c: "<path>/basebackups/.latest" exists and is
 * non-empty) and, for any that don't, starts a one-shot background job
 * (backup_bootstrap.c's own ws_backup_bootstrap_start()) that takes one --
 * see that file's own header comment for the full fork/retry-bound design.
 * A cluster with "receivewal = pull" is only ever bootstrapped once its own
 * real, already-started receivewal worker (receivewal.c) shows genuine on-disk
 * evidence of streaming; a cluster missing an "upstream" property to take a
 * backup from is logged and skipped, never an error; a disabled cluster
 * (clusters.h's own WsCluster.disabled) is skipped silently, the same "never
 * touch a dropped cluster" guarantee every other automatic pg_walserver
 * action gives it.
 *
 * Called at exactly two points, both documented in the project's own
 * README.md: once from cli_serve_run() (cli_root.c), right after
 * ws_receivewal_start_all() has started every configured cluster's own real
 * receivewal worker at "serve" startup; and once from ws_reload_config() (accept_
 * loop.c), right after a successful SIGHUP reload's own ws_receivewal_
 * reload() has reconciled the receivewal worker set against the newly reloaded
 * clusters. This one-time bootstrap attempt at either of those two moments
 * is the only "automatic" base backup behavior pg_walserver has: recurring
 * or scheduled backups are explicitly out of scope, the same
 * provide-the-facility-not-the-scheduling-policy philosophy this project
 * applies elsewhere -- an operator's own "pg_walserver basebackup"
 * invocation (or their own cron job around it) is what keeps a cluster's
 * backup current after its first, automatic one.
 */
void
ws_bootstrap_missing_backups(const WsCluster *clusters, int clusterCount)
{
	for (int i = 0; i < clusterCount; i++)
	{
		const WsCluster *cluster = &clusters[i];

		if (cluster->path[0] == '\0' || cluster->disabled ||
			cli_basebackup_cluster_has_backup(cluster->path))
		{
			continue;
		}

		if (cluster->upstream[0] == '\0')
		{
			log_warn("Cluster \"%s\" has no base backup yet, and no "
					 "\"upstream\" property to take one from -- run "
					 "\"pg_walserver basebackup\" by hand once it has one",
					 cluster->key);
			continue;
		}

		if (bootstrapChildCount >= WS_MAX_CONNECTIONS)
		{
			log_error("Too many pending automatic bootstrap base backups "
					  "(max %d): not starting one for cluster \"%s\" this "
					  "time -- it will be retried at the next start or "
					  "reload", WS_MAX_CONNECTIONS, cluster->key);
			continue;
		}

		pid_t pid = -1;

		if (ws_backup_bootstrap_start(cluster, &pid))
		{
			strlcpy(bootstrapChildClusters[bootstrapChildCount], cluster->key,
					sizeof(bootstrapChildClusters[bootstrapChildCount]));
			bootstrapChildStartedAt[bootstrapChildCount] = time(NULL);
			bootstrapChildren[bootstrapChildCount++] = pid;
			log_info("Cluster \"%s\" has no base backup yet: starting an "
					 "automatic bootstrap base backup in the background "
					 "(pid %d)", cluster->key, pid);
		}
		else
		{
			/* errors have already been logged */
		}
	}
}


/*
 * log_cluster_diff logs a summary of what changed between the previously
 * installed cluster set and a freshly, successfully reloaded one: clusters
 * added, removed, or changed (path/upstream/hostname/receivewal), compared by
 * key. Called only once both pg_walserver.ini and pg_walserver_hba.conf have
 * re-parsed cleanly, right before the new clusters are installed.
 */
static void
log_cluster_diff(const WsCluster *oldClusters, int oldCount,
				 const WsCluster *newClusters, int newCount)
{
	int added = 0, removed = 0, changed = 0;

	for (int i = 0; i < newCount; i++)
	{
		const WsCluster *old = clusters_find_exact(oldClusters, oldCount,
												   newClusters[i].key);

		if (old == NULL)
		{
			++added;
			log_info("reload: cluster \"%s\" added (path \"%s\")",
					 newClusters[i].key, newClusters[i].path);
			continue;
		}

		if (strcmp(old->path, newClusters[i].path) != 0 ||
			strcmp(old->upstream, newClusters[i].upstream) != 0 ||
			strcmp(old->hostname, newClusters[i].hostname) != 0 ||
			old->receivewalPull != newClusters[i].receivewalPull)
		{
			++changed;
			log_info("reload: cluster \"%s\" changed (path \"%s\" -> \"%s\", "
					 "upstream \"%s\" -> \"%s\", hostname \"%s\" -> \"%s\", "
					 "receivewal %s -> %s)",
					 newClusters[i].key, old->path, newClusters[i].path,
					 old->upstream, newClusters[i].upstream,
					 old->hostname, newClusters[i].hostname,
					 old->receivewalPull ? "pull" : "none",
					 newClusters[i].receivewalPull ? "pull" : "none");
		}
	}

	for (int i = 0; i < oldCount; i++)
	{
		if (clusters_find_exact(newClusters, newCount, oldClusters[i].key) == NULL)
		{
			++removed;
			log_info("reload: cluster \"%s\" removed", oldClusters[i].key);
		}
	}

	if (added == 0 && removed == 0 && changed == 0)
	{
		log_info("reload: clusters unchanged (%d cluster%s)",
				 newCount, newCount == 1 ? "" : "s");
	}
	else
	{
		log_info("reload: clusters: %d added, %d removed, %d changed "
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
 * calls: it re-reads and re-validates pg_walserver.ini (clusters_load()) and
 * pg_walserver_hba.conf (hba_parse_file()) from disk, and atomically swaps in
 * the new versions ONLY when both parse successfully -- exactly like
 * PostgreSQL's own SIGHUP-triggered ProcessConfigFile(), a bad reload is
 * refused, never partially applied, and the previous, already-validated
 * configuration keeps serving every connection. Reconciles the embedded
 * receivewal worker set against the new clusters (receivewal.c's ws_receivewal_reload())
 * once both files are known-good. The TLS certificate/key are never
 * touched here -- see the one-line note logged below.
 */
static void
ws_reload_config(WsServerConfig *config)
{
	static bool loggedTlsReloadNote = false;

	if (config->clustersPath[0] == '\0')
	{
		log_info("Received SIGHUP: running with --insecure and no --pgdata, "
				 "nothing to reload");
		return;
	}

	log_info("Received SIGHUP: reloading \"%s\" and \"%s\"",
			 config->clustersPath, config->auth.hbaPath);

	if (!loggedTlsReloadNote)
	{
		log_info("Note: the TLS certificate/key are not reloaded by SIGHUP "
				 "(a fresh SSL_CTX is only ever built at startup); restart "
				 "pg_walserver to pick up a rotated \"server.crt\"/"
				 "\"server.key\"");
		loggedTlsReloadNote = true;
	}

	WsCluster *newClusters = NULL;
	int newClusterCount = 0;
	bool clustersOk = clusters_load(config->clustersPath, &newClusters, &newClusterCount);

	if (!clustersOk)
	{
		log_error("Reload failed: could not parse \"%s\": keeping the "
				  "current configuration", config->clustersPath);
	}

	WsHbaRuleSet newHbaRuleSet = { 0 };
	bool hbaOk = hba_parse_file(config->auth.hbaPath, &newHbaRuleSet);

	if (!hbaOk)
	{
		log_error("Reload failed: could not parse \"%s\": keeping the "
				  "current configuration", config->auth.hbaPath);
	}

	if (hbaOk && hba_ruleset_requires_client_cert(&newHbaRuleSet) &&
		!ws_tls_client_verification_enabled())
	{
		log_error("Reload failed: \"%s\" has a \"clientcert=verify-full\" "
				  "rule but no usable TLS CA file was loaded at startup: "
				  "keeping the current configuration", config->auth.hbaPath);
		hbaOk = false;
	}

	if (!clustersOk || !hbaOk)
	{
		clusters_free(newClusters);
		hba_ruleset_free(&newHbaRuleSet);
		return;
	}

	log_cluster_diff(config->clusters, config->clusterCount, newClusters,
					 newClusterCount);
	log_hba_diff(&config->auth.hbaRuleSet, &newHbaRuleSet);

	/* never restart an already-running receivewal worker just because SIGHUP fired;
	 * only reconcile against what actually changed */
	ws_receivewal_reload(newClusters, newClusterCount);

	clusters_free(config->clusters);
	hba_ruleset_free(&config->auth.hbaRuleSet);

	config->clusters = newClusters;
	config->clusterCount = newClusterCount;
	config->auth.hbaRuleSet = newHbaRuleSet;

	log_info("Reload complete: now serving %d cluster%s",
			 newClusterCount, newClusterCount == 1 ? "" : "s");

	/*
	 * Now that the reconciled receivewal worker set above has had a chance to start
	 * a real, supervised receivewal worker for any newly-added "receivewal = pull"
	 * cluster, check every currently-configured cluster for a missing base
	 * backup and kick off an automatic bootstrap for it -- the second of
	 * the two trigger points documented in accept_loop.h's own
	 * ws_bootstrap_missing_backups() comment (the first being "serve"'s own
	 * startup, cli_root.c's cli_serve_run()). This is exactly how "pg_
	 * walserver setup" reloading an already-running "serve" (cli_setup.c)
	 * ends up with a base backup with no manual "pg_walserver basebackup"
	 * invocation needed at all.
	 */
	ws_bootstrap_missing_backups(config->clusters, config->clusterCount);

	refresh_ps_state(config, gServePid, gServeStartedAt);
}


/*
 * refresh_ps_state gathers a fresh snapshot of every receivewal worker (receivewal.c)
 * and in-flight bootstrap backup job (backup_bootstrap.c) this process is
 * currently tracking, and writes it to the on-disk ps state file
 * (ps_state.h) that "pg_walserver ps"/"pg_walserver status", run later as
 * a brand-new process, read back. Called once before the main loop starts
 * (so "ps" has something accurate to read even before the first tick),
 * once per loop iteration alongside ws_receivewal_tick(), and once more at
 * the end of a successful reload -- see ws_accept_loop()'s own call sites.
 * Cheap: a handful of small structs and one small atomic file write, not
 * worth gating behind a "did anything actually change" check.
 */
static void
refresh_ps_state(const WsServerConfig *config, pid_t servePid,
				 time_t serveStartedAt)
{
	if (config->pgdata[0] == '\0')
	{
		return;
	}

	WsPsState state = { 0 };

	state.servePid = servePid;
	state.serveStartedAt = serveStartedAt;

	WsReceivewalStatus receivewalStatus[WS_PS_MAX_ENTRIES];
	int receivewalCount = ws_receivewal_get_status(receivewalStatus, WS_PS_MAX_ENTRIES);

	for (int i = 0; i < receivewalCount; i++)
	{
		WsPsReceivewalEntry *dst =
			&state.receivewalWorkers[state.receivewalWorkerCount++];

		strlcpy(dst->clusterKey, receivewalStatus[i].clusterKey, sizeof(dst->clusterKey));
		strlcpy(dst->path, receivewalStatus[i].path, sizeof(dst->path));
		dst->pid = receivewalStatus[i].pid;
		dst->startedAt = receivewalStatus[i].startedAt;
		dst->restarts = receivewalStatus[i].restarts;

		/*
		 * Fold in the receivewal worker's own last-observed (lsn, timeline),
		 * relayed via its own "<path>/receivewal-progress" file (wal_dir_
		 * scan.h, written by that worker's own hook callbacks, cli_
		 * internal.c) -- that worker is a separate fork()+execv()'d
		 * process, so this file is the only way this tick can see it. A
		 * missing/unparseable file (worker never ticked yet, or isn't
		 * running) just leaves dst->lsn empty -- not an error.
		 */
		(void) ws_receivewal_progress_read(receivewalStatus[i].path,
										   dst->lsn, sizeof(dst->lsn),
										   &dst->lsnTimeline,
										   &dst->lsnObservedAt);
	}

	WsBootstrapStatus bootstrapStatus[WS_PS_MAX_ENTRIES];
	int bootstrapCount = ws_bootstrap_get_status(bootstrapStatus, WS_PS_MAX_ENTRIES);

	for (int i = 0; i < bootstrapCount; i++)
	{
		WsPsBootstrapEntry *dst = &state.bootstraps[state.bootstrapCount++];

		strlcpy(dst->clusterKey, bootstrapStatus[i].clusterKey, sizeof(dst->clusterKey));
		dst->pid = bootstrapStatus[i].pid;
		dst->startedAt = bootstrapStatus[i].startedAt;
	}

	if (!ws_ps_state_write(config->pgdata, &state))
	{
		log_warn("Failed to update the ps state file under \"%s\"",
				 config->pgdata);
	}
}


/*
 * ws_accept_loop is the whole server: it creates the listening socket, then
 * loops accepting connections, forking a child per connection (no exec(),
 * matching real Postgres's postmaster/BackendMain() split) and reaping
 * exited children, until asked to stop. Returns false only if the listening
 * socket itself could not be created; otherwise it runs until shutdown and
 * returns true.
 *
 * Takes a mutable config: a successful SIGHUP reload updates
 * config->clusters/clusterCount and config->auth.hbaRuleSet in place (see
 * ws_reload_config() in this same file). Every forked connection child still
 * only ever reads it.
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
			 config->clustersPath[0] != '\0' ? ", clusters " : " (no clusters file)",
			 config->clustersPath[0] != '\0' ? config->clustersPath : "");

	gServePid = getpid();
	gServeStartedAt = time(NULL);
	refresh_ps_state(config, gServePid, gServeStartedAt);

	while (!asked_to_stop && !asked_to_stop_fast)
	{
		/*
		 * set_signal_handlers() (common/signals.c) already installs SIGHUP
		 * -> catch_reload(), which only sets this flag -- never do real work
		 * inside a signal handler. Checked once per loop iteration,
		 * alongside ws_receivewal_tick() below, exactly like real PostgreSQL
		 * checks its own ConfigReloadPending flag in its main loops.
		 */
		if (asked_to_reload)
		{
			asked_to_reload = 0;
			ws_reload_config(config);
		}

		/*
		 * One tick, one wildcard waitpid(-1, ...) call site for this whole
		 * process (receivewal.c's own ws_receivewal_tick(), process_supervisor.c
		 * underneath it): reaps and restarts-on-death every "receivewal =
		 * pull" cluster's own supervised pg_receivewal child (a completely
		 * independent lifecycle from the connection children below -- one
		 * long-lived child per active cluster, alive for the server's whole
		 * lifetime, not per accepted connection, see receivewal.c's own
		 * header comment), and hands any pid it doesn't recognize to
		 * connection_child_exited() above.
		 */
		ws_receivewal_tick(bootstrap_or_connection_child_exited, &connChildren);
		refresh_ps_state(config, gServePid, gServeStartedAt);

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
		ws_receivewal_tick(bootstrap_or_connection_child_exited, &connChildren);

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
	 * Stop every "receivewal = pull" cluster's own supervised pg_receivewal
	 * child cleanly (SIGINT, a bounded wait, then SIGKILL if needed --
	 * receivewal.c's own ws_receivewal_stop_all()) before this process itself
	 * exits: the same shutdown path every other part of this server uses
	 * (asked_to_stop/asked_to_stop_fast, above), not a second mechanism.
	 */
	ws_receivewal_stop_all();

	log_info("pg_walserver shutting down");

	return true;
}
