/*
 * src/bin/pg_walserver/capture.h
 *   The embedded, supervised WAL capturer for every route with
 *   "capture = pull" configured (routes.h) -- see capture.c's own header
 *   comment for the full design (fork()+execv() shape, the shared
 *   process_supervisor.h it's built on, restart/backoff policy, and the
 *   single-wildcard-reaper contract ws_capture_tick() has with its
 *   caller) and README.md's "The embedded pull capturer" section for the
 *   design this implements.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CAPTURE_H
#define WS_CAPTURE_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

#include "routes.h"

/*
 * ws_capture_start_all forks one supervised capturer child per route in
 * routes[0..routeCount) with capturePull set (routes.h), each running
 * this same pg_walserver binary re-exec'd into "internal service
 * pg-receivewal" (cli_internal.c), which runs the vendored pg_receivewal
 * against that route's own "upstream", writing straight into that
 * route's own "path". A route with capturePull but no "upstream" is
 * logged and skipped, not a startup failure. Called once, from
 * cli_serve_run(), after pg_walserver.ini/HBA validation succeeds and
 * before ws_accept_loop() starts. Must not be called more than once per
 * process.
 */
bool ws_capture_start_all(const WsRoute *routes, int routeCount);

/*
 * ws_capture_tick drains every exited capturer child via this process's
 * one and only wildcard waitpid(-1, WNOHANG) loop (process_supervisor.c),
 * restarting any that need it, and hands any pid it doesn't recognize
 * (as one of its own supervised capturers) to otherChildExited -- the
 * caller's own separately tracked children (accept_loop.c's per-
 * connection children). Called once per ws_accept_loop() iteration,
 * *instead of* that loop running its own, second wildcard wait: see
 * process_supervisor.h's own comment for why two independent wildcard
 * reapers in the same process is a real, previously-hit bug, not a
 * theoretical concern. A no-op (beyond calling otherChildExited, if
 * given, for any of the caller's own exited children) when
 * ws_capture_start_all() was never called or started no children.
 */
void ws_capture_tick(bool (*otherChildExited)(void *ctx, pid_t pid,
											  int status),
					 void *otherCtx);

/*
 * ws_capture_stop_all signals every still-running capturer child to stop
 * cleanly (SIGINT, matching pg_receivewal's own documented clean-stop
 * signal -- see capture.c's own comment), waits up to a bounded timeout
 * for all of them, and escalates to SIGKILL for anything still alive past
 * that. Called once, from ws_accept_loop(), right before the server
 * itself exits -- never leaves an orphaned capturer child running past
 * pg_walserver's own shutdown.
 */
void ws_capture_stop_all(void);

#endif /* WS_CAPTURE_H */
