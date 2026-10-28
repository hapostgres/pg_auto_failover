/*
 * src/bin/pg_walserver/accept_loop.h
 *   The bare accept loop: socket()/bind()/listen()/accept(), fork()
 *   per connection with no exec() (matching real Postgres's
 *   BackendStartup()/BackendMain() model for cheap concurrency -- one
 *   process per connection), each forked child running
 *   the full startup/auth/command-loop for exactly one connection.
 *
 *   Hardening, modelled on PostgreSQL's postmaster: every child runs under
 *   an absolute authentication deadline (--auth-timeout, default 30
 *   seconds, armed with alarm() and a SIGALRM handler that _exit()s: the
 *   startup packet, TLS handshake, HBA including DNS and SCRAM all count,
 *   and it is cancelled once authentication succeeded); live children are
 *   tracked in a pid array and reaped in the main loop (no SIGCHLD handler,
 *   no shared counter), capping the connections.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_ACCEPT_LOOP_H
#define WS_ACCEPT_LOOP_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "auth.h"

typedef struct WsServerConfig
{
	int port;
	int authTimeout;              /* absolute authentication deadline, seconds */
	char pgdata[MAXPGPATH];       /* empty in --insecure mode; set by
	                               * cli_serve_run() -- read by
	                               * refresh_ps_state() (accept_loop.c) to
	                               * know where to write the ps state file
	                               * (ps_state.h) */
	char clustersPath[MAXPGPATH];   /* empty: no cluster dispatch, manual-testing mode */
	WsAuthConfig auth;
	WsCluster *clusters;               /* the currently installed, parsed
	                                    * pg_walserver.ini -- loaded once at
	                                    * startup (cli_serve_run()) and swapped
	                                    * in atomically, together with auth.
	                                    * hbaRuleSet above, by a successful
	                                    * SIGHUP reload (ws_reload_config(),
	                                    * accept_loop.c). Every connection reads
	                                    * this same, already-validated snapshot;
	                                    * none of them re-parses the file off
	                                    * disk itself. Owned here: clusters_free()
	                                    * it, never a per-connection concern. */
	int clusterCount;
} WsServerConfig;

bool ws_accept_loop(WsServerConfig *config);

void ws_bootstrap_missing_backups(const WsCluster *clusters, int clusterCount);

/*
 * WsBootstrapStatus is one still-running automatic bootstrap base backup
 * job's status -- see ws_bootstrap_get_status() below.
 */
typedef struct WsBootstrapStatus
{
	char clusterKey[NAMEDATALEN + 16];
	pid_t pid;
	time_t startedAt;
} WsBootstrapStatus;

int ws_bootstrap_get_status(WsBootstrapStatus *out, int maxOut);

#endif /* WS_ACCEPT_LOOP_H */
