/*
 * src/bin/pg_walserver/receivewal.h
 *   The embedded, supervised WAL receivewal worker for every route with
 *   "receivewal = pull" configured (routes.h) -- see receivewal.c's own header
 *   comment for the full design (fork()+execv() shape, the shared
 *   process_supervisor.h it's built on, restart/backoff policy, and the
 *   single-wildcard-reaper contract ws_receivewal_tick() has with its
 *   caller) and README.md's "The embedded receivewal worker" section for the
 *   design this implements.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_RECEIVEWAL_H
#define WS_RECEIVEWAL_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

#include "routes.h"

/*
 * ws_receivewal_start_all forks one supervised receivewal worker child per route in
 * routes[0..routeCount) with receivewalPull set (routes.h), each running
 * this same pg_walserver binary re-exec'd into "internal service
 * pg-receivewal" (cli_internal.c), which runs the vendored pg_receivewal
 * against that route's own "upstream", writing straight into that
 * route's own "path". A route with receivewalPull but no "upstream" is
 * logged and skipped, not a startup failure -- so is a disabled route
 * (routes.h's own WsRoute.disabled), silently: a dropped route never
 * gets its embedded receivewal worker started in the first place, the
 * same guarantee ws_receivewal_reload() already gives a route that
 * becomes disabled while already running. Called once, from
 * cli_serve_run(), after pg_walserver.ini/HBA validation succeeds and
 * before ws_accept_loop() starts. Must not be called more than once per
 * process.
 */
bool ws_receivewal_start_all(const WsRoute *routes, int routeCount);

/*
 * ws_receivewal_tick drains every exited receivewal worker child via this process's
 * one and only wildcard waitpid(-1, WNOHANG) loop (process_supervisor.c),
 * restarting any that need it, and hands any pid it doesn't recognize
 * (as one of its own supervised receivewalWorkers) to otherChildExited -- the
 * caller's own separately tracked children (accept_loop.c's per-
 * connection children). Called once per ws_accept_loop() iteration,
 * *instead of* that loop running its own, second wildcard wait: see
 * process_supervisor.h's own comment for why two independent wildcard
 * reapers in the same process is a real, previously-hit bug, not a
 * theoretical concern. A no-op (beyond calling otherChildExited, if
 * given, for any of the caller's own exited children) when
 * ws_receivewal_start_all() was never called or started no children.
 */
void ws_receivewal_tick(bool (*otherChildExited)(void *ctx, pid_t pid,
												 int status),
						void *otherCtx);

/*
 * ws_receivewal_reload reconciles the running "receivewal = pull" receivewal worker set
 * against a freshly, successfully reloaded (SIGHUP) route list -- it never
 * restarts a receivewal worker whose route is unchanged:
 *
 *   - a route that newly has "receivewal = pull" (or is new outright) gets a
 *     receivewal worker started;
 *   - a route whose "receivewal = pull" was removed, whose route
 *     disappeared entirely, or that is now disabled ("cluster drop"
 *     without --purge, see routes.h's own WsRoute.disabled comment),
 *     gets its receivewal worker stopped (SIGINT), and never gets a new
 *     one started for it either;
 *   - a route whose "upstream" or "path" changed while "receivewal = pull"
 *     stayed on gets stopped (SIGINT) and, once reaped, automatically
 *     restarted with the new values by the ordinary PERMANENT-policy
 *     restart path in ws_receivewal_tick() -- it cannot retarget an
 *     already-forked/exec'd pg_receivewal child in place, so this is
 *     always a stop-then-start, never a live retarget.
 *
 * Logs every start/stop/restart decision it makes. Must only be called
 * after ws_receivewal_start_all() has already run once.
 */
void ws_receivewal_reload(const WsRoute *newRoutes, int newRouteCount);

/*
 * WsReceivewalStatus is one supervised "receivewal = pull" receivewal worker's current
 * status, as seen from inside "serve" itself -- see ws_receivewal_get_status()
 * below, and ps_state.h for why a *different* process (pg_walserver ps/
 * status) cannot just read receivewalServices/receivewalRoutes directly and
 * instead goes through a state file "serve" writes from this same data.
 */
typedef struct WsReceivewalStatus
{
	char routeKey[NAMEDATALEN + 16];
	char path[MAXPGPATH];
	char upstream[MAXCONNINFO];
	pid_t pid;          /* <= 0: not currently running */
	time_t startedAt;   /* this incarnation's own start time */
	int restarts;        /* how many times it has been restarted */
} WsReceivewalStatus;

/*
 * ws_receivewal_get_status fills out[0..min(serviceCount,maxOut)) with the
 * current status of every route ws_receivewal_start_all()/ws_receivewal_reload()
 * is tracking (whether or not each one is currently running), and returns
 * how many entries it filled. Used by accept_loop.c's own refresh_ps_
 * state() to keep the on-disk ps state file (ps_state.h) current.
 */
int ws_receivewal_get_status(WsReceivewalStatus *out, int maxOut);

/*
 * ws_receivewal_stop_all signals every still-running receivewal worker child to stop
 * cleanly (SIGINT, matching pg_receivewal's own documented clean-stop
 * signal -- see receivewal.c's own comment), waits up to a bounded timeout
 * for all of them, and escalates to SIGKILL for anything still alive past
 * that. Called once, from ws_accept_loop(), right before the server
 * itself exits -- never leaves an orphaned receivewal worker child running past
 * pg_walserver's own shutdown.
 */
void ws_receivewal_stop_all(void);

#endif /* WS_RECEIVEWAL_H */
