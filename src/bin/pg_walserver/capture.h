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
 * ws_capture_reload reconciles the running "capture = pull" capturer set
 * against a freshly, successfully reloaded (SIGHUP) route list -- it never
 * restarts a capturer whose route is unchanged:
 *
 *   - a route that newly has "capture = pull" (or is new outright) gets a
 *     capturer started;
 *   - a route whose "capture = pull" was removed, or whose route
 *     disappeared entirely, gets its capturer stopped (SIGINT);
 *   - a route whose "upstream" or "path" changed while "capture = pull"
 *     stayed on gets stopped (SIGINT) and, once reaped, automatically
 *     restarted with the new values by the ordinary PERMANENT-policy
 *     restart path in ws_capture_tick() -- it cannot retarget an
 *     already-forked/exec'd pg_receivewal child in place, so this is
 *     always a stop-then-start, never a live retarget.
 *
 * Logs every start/stop/restart decision it makes. Must only be called
 * after ws_capture_start_all() has already run once.
 */
void ws_capture_reload(const WsRoute *newRoutes, int newRouteCount);

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

/*
 * ws_capture_prime_route starts a single, unsupervised embedded pull
 * capturer for exactly one route (the same fork()+execv() into "internal
 * service pg-receivewal" ws_capture_start_all()'s own per-route children
 * use), handing back its pid in *pidOut. Unlike ws_capture_start_all(),
 * this pid is never registered with this process's own captureSupervisor:
 * the caller owns its whole lifecycle directly and must eventually call
 * ws_capture_stop_primed() on it. Safe to call from a process that never
 * calls ws_capture_start_all() at all (e.g. "pg_walserver setup", a
 * one-shot CLI invocation, never "serve").
 *
 * This exists to close a real historical-continuity gap: "pg_walserver
 * setup --with-basebackup" (cli_setup.c) takes its very first base backup
 * directly against the route's own upstream, before "serve" -- and
 * therefore this route's own real, supervised capturer -- has ever run for
 * it. A base backup taken with nothing yet capturing this route's WAL
 * reports a "start position" (its own backup_label) that this route's
 * walcache can never actually reach later: the first capturer "serve"
 * eventually starts for it begins from *its own* connection time, not
 * retroactively from the backup's already-past start LSN, and capture =
 * pull's archive-wal (cli_archive.c) never backstop-pushes to fill a gap
 * like that (a deliberate fix for a real two-writer race, not an oversight
 * to work around here). Priming a capturer first, and only taking the base
 * backup once it has genuinely started streaming (see wal_dir_scan.h's own
 * wal_dir_has_any_segment(), the readiness check cli_setup.c polls with),
 * guarantees the base backup's own start LSN -- always timestamped after
 * the primer already began -- falls inside WAL the primer is already
 * capturing: continuous history all the way back to that start LSN, with
 * no gap left for a later CHECK_FILE-only archive-wal invocation to have
 * silently needed a backstop push for.
 */
bool ws_capture_prime_route(const char *routeKey, const char *path,
							const char *upstream, pid_t *pidOut);

/*
 * ws_capture_stop_primed cleanly stops (SIGINT, escalating to SIGKILL after
 * WS_CAPTURE_STOP_TIMEOUT_MS) and reaps the single capturer pid an earlier
 * ws_capture_prime_route() call started. A no-op for pid <= 0. Must be
 * called before the caller's own process exits, and before "serve" is
 * ever started for the same route/path -- two pg_receivewal processes
 * writing into the same directory at once is not a supported
 * configuration.
 */
void ws_capture_stop_primed(pid_t pid);

#endif /* WS_CAPTURE_H */
