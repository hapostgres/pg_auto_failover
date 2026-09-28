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
	char routesPath[MAXPGPATH];   /* empty: no routing, manual-testing mode */
	WsAuthConfig auth;
	WsRoute *routes;               /* the currently installed, parsed
	                                * pg_walserver.ini -- loaded once at
	                                * startup (cli_serve_run()) and swapped
	                                * in atomically, together with auth.
	                                * hbaRuleSet above, by a successful
	                                * SIGHUP reload (ws_reload_config(),
	                                * accept_loop.c). Every connection reads
	                                * this same, already-validated snapshot;
	                                * none of them re-parses the file off
	                                * disk itself. Owned here: routes_free()
	                                * it, never a per-connection concern. */
	int routeCount;
} WsServerConfig;

/*
 * ws_accept_loop takes a mutable config: a successful SIGHUP reload updates
 * config->routes/routeCount and config->auth.hbaRuleSet in place (see
 * ws_reload_config() in accept_loop.c). Every forked connection child still
 * only ever reads it.
 */
bool ws_accept_loop(WsServerConfig *config);

/*
 * ws_bootstrap_missing_backups checks every route in routes[0..routeCount)
 * for whether it already has a base backup (cli_basebackup_route_has_
 * backup(), cli_basebackup.c: "<path>/basebackups/.latest" exists and is
 * non-empty) and, for any that don't, starts a one-shot background job
 * (backup_bootstrap.c's own ws_backup_bootstrap_start()) that takes one --
 * see that file's own header comment for the full fork/retry-bound design.
 * A route with "capture = pull" is only ever bootstrapped once its own
 * real, already-started capturer (capture.c) shows genuine on-disk
 * evidence of streaming; a route missing an "upstream" property to take a
 * backup from is logged and skipped, never an error.
 *
 * Called at exactly two points, both documented in the project's own
 * README.md: once from cli_serve_run() (cli_root.c), right after
 * ws_capture_start_all() has started every configured route's own real
 * capturer at "serve" startup; and once from ws_reload_config() (accept_
 * loop.c), right after a successful SIGHUP reload's own ws_capture_
 * reload() has reconciled the capturer set against the newly reloaded
 * routes. This one-time bootstrap attempt at either of those two moments
 * is the only "automatic" base backup behavior pg_walserver has: recurring
 * or scheduled backups are explicitly out of scope, the same
 * provide-the-facility-not-the-scheduling-policy philosophy this project
 * applies elsewhere -- an operator's own "pg_walserver basebackup"
 * invocation (or their own cron job around it) is what keeps a route's
 * backup current after its first, automatic one.
 */
void ws_bootstrap_missing_backups(const WsRoute *routes, int routeCount);

/*
 * WsBootstrapStatus is one still-running automatic bootstrap base backup
 * job's status -- see ws_bootstrap_get_status() below.
 */
typedef struct WsBootstrapStatus
{
	char routeKey[NAMEDATALEN + 16];
	pid_t pid;
	time_t startedAt;
} WsBootstrapStatus;

/*
 * ws_bootstrap_get_status fills out[0..min(bootstrapChildCount,maxOut))
 * with every currently in-flight automatic bootstrap backup job, and
 * returns how many entries it filled. Used by refresh_ps_state()
 * (accept_loop.c) to keep the on-disk ps state file (ps_state.h) current.
 */
int ws_bootstrap_get_status(WsBootstrapStatus *out, int maxOut);

#endif /* WS_ACCEPT_LOOP_H */
