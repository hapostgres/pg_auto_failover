/*
 * src/bin/common/process_supervisor.h
 *   A small, generic Service/RestartPolicy child-process supervisor,
 *   shared by any binary in this project that needs to start and
 *   restart-on-death a fixed set of long-lived child processes.
 *
 *   This file is genuinely shared, and genuinely consumed, by both
 *   `pg_walserver` (its embedded pull capturer, `capture.c`, uses the
 *   whole `ProcessSupervisor`/`process_supervisor_*()` API directly) and
 *   `pg_autoctl` (`pg_autoctl/supervisor.c` delegates the two pieces of
 *   its own service supervision that are genuinely generic -- rather
 *   than pg_autoctl-specific business logic -- to the functions declared
 *   here):
 *
 *     - the Erlang-inspired MaxR/MaxT restart-backoff ring buffer
 *       (`ProcessRestartCounters` and the `process_restart_counters_*()`
 *       functions), and
 *
 *     - PID-1-safe orphan-reaping classification, i.e. deciding whether
 *       an unrecognised dead child's pid is an orphaned grandchild
 *       reparented to us by the kernel (expected, log at INFO) or a
 *       genuine bug (log at ERROR) -- see
 *       `process_supervisor_log_unknown_pid()`.
 *
 *   `pg_autoctl/supervisor.c` keeps its own `Supervisor`/`Service`
 *   structs and its own main accept/reap/restart loop: that loop is
 *   deeply entangled with pg_autoctl-specific business rules that do not
 *   belong in a generic, shared facility -- the node-spec file watcher
 *   for `pg_autoctl node run <file>`'s mutable-settings-reload feature,
 *   the keeper-only SIGTERM graceful-shutdown handoff, pg_autoctl's own
 *   three-way `RestartPolicy` (including the TRANSIENT-service-quit-
 *   means-shut-everything-down rule) and its `EXIT_CODE_DROPPED`/
 *   `EXIT_CODE_FATAL` sentinel exit codes. Forcing that business logic
 *   through this file would not make it more "shared" -- there is only
 *   ever one caller for any of it -- it would just make this generic
 *   utility depend on pg_autoctl's own concepts. What genuinely is
 *   shared (the restart-backoff bookkeeping and the orphan-reaping
 *   classification) lives here instead, in one place, used by both.
 *
 *   Every type and function here is prefixed `Process`/`process_` (not
 *   `Service`/`Supervisor`/`supervisor_*`) specifically so this file can
 *   never collide, by name or by symbol, with `pg_autoctl`'s own
 *   `supervisor.h`/`supervisor.c` -- both are linked into the same
 *   `pg_autoctl` binary (this one via `libpgaf_common.a`, its own
 *   directly), so distinct names are required, not just good style.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_PROCESS_SUPERVISOR_H
#define WS_PROCESS_SUPERVISOR_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

/*
 * Restart policy, identical in spirit to pg_autoctl's own RestartPolicy
 * (supervisor.h): PROCESS_RP_PERMANENT is always restarted (subject to the
 * MaxR/MaxT backoff below); PROCESS_RP_TEMPORARY is never restarted, its
 * exit is simply reaped. There is no TRANSIENT policy here (unlike pg_
 * autoctl's own three-way enum) -- nothing in this project's own use of
 * this file needs "restart only on abnormal exit, otherwise shut the whole
 * supervisor down", pg_autoctl's own keeper-specific one-shot-init use
 * case; add it back here if a future caller genuinely needs it.
 */
typedef enum
{
	PROCESS_RP_PERMANENT = 0,
	PROCESS_RP_TEMPORARY
} ProcessRestartPolicy;

/*
 * Erlang-inspired restart-intensity tracking (see
 * http://erlang.org/doc/man/supervisor.html): if more than
 * PROCESS_SUPERVISOR_MAX_RETRY restarts happen within the last
 * PROCESS_SUPERVISOR_MAX_TIME seconds, this service stops being
 * restarted. This is the actual shared implementation of that ring
 * buffer: `pg_autoctl/supervisor.c`'s own `RestartCounters` is a typedef
 * of `ProcessRestartCounters` and its `supervisor_may_restart()` calls
 * `process_restart_counters_may_restart()` directly (its
 * SUPERVISOR_SERVICE_MAX_RETRY/_MAX_TIME are, in turn, defined from
 * these same two constants) -- there is exactly one MaxR/MaxT
 * implementation in this project, not two independently-maintained
 * copies of the same algorithm.
 *
 * Deliberately different from pg_autoctl's own supervisor.c in one way:
 * giving up on restarting one service here does NOT bring down every
 * other service (unlike pg_autoctl, where each service is essential to
 * the single node it manages) -- pg_walserver may be serving several
 * independent routes at once, and one route's capturer permanently
 * failing (a truly broken upstream, not a transient blip) should not stop
 * every other route pg_walserver is otherwise serving correctly. That
 * "what to do once MaxR/MaxT is exceeded" policy decision is made by each
 * caller on top of process_restart_counters_may_restart()'s answer, it is
 * not part of the shared ring-buffer mechanism itself.
 */
#define PROCESS_SUPERVISOR_MAX_RETRY 5
#define PROCESS_SUPERVISOR_MAX_TIME 300 /* seconds */

typedef struct ProcessRestartCounters
{
	int count;                  /* how many restarts, including the first start */
	int position;               /* ring buffer index */
	uint64_t startTime[PROCESS_SUPERVISOR_MAX_RETRY];
} ProcessRestartCounters;

/*
 * process_restart_counters_start records a service's very first start
 * (count = 1, position = 0, startTime[0] = now). Both this file's own
 * process_supervisor_start_all() and pg_autoctl's supervisor_start() call
 * this for every service they start.
 */
void process_restart_counters_start(ProcessRestartCounters *counters,
									uint64_t now);

/*
 * process_restart_counters_record advances the ring buffer to record one
 * more restart at time now. Call this only after
 * process_restart_counters_may_restart() has returned true for the same
 * counters.
 */
void process_restart_counters_record(ProcessRestartCounters *counters,
									 uint64_t now);

/*
 * process_restart_counters_may_restart applies the MaxR/MaxT policy:
 * returns true when another restart is allowed, false when this service
 * has already been restarted PROCESS_SUPERVISOR_MAX_RETRY times within
 * the last PROCESS_SUPERVISOR_MAX_TIME seconds. Purely a computation on
 * counters: it does not log anything itself, so that each caller can keep
 * its own wording (and log level) for "giving up" -- pg_autoctl's own
 * supervisor.c and this file's own process_supervisor_restart_service()
 * each log a different message on a false result.
 */
bool process_restart_counters_may_restart(ProcessRestartCounters *counters);

/*
 * process_supervisor_log_unknown_pid classifies and logs a dead child
 * pid that matches none of a supervisor's own known services: when
 * running as PID 1 inside a container, the kernel reparents orphaned
 * grandchildren to us and this is expected behaviour (logged at INFO);
 * otherwise it is logged at ERROR, since some subprocess-tracking
 * bookkeeping is missing a case.
 */
void process_supervisor_log_unknown_pid(pid_t pid);

/*
 * One supervised child: a name (for logging), a restart policy, its
 * current pid (-1 when not running, "gave up" state included), a start
 * function (fork()+exec() is the expected shape, see capture.c's own
 * comment on why: live-upgrade safety, matching pg_autoctl's own
 * service_postgres_ctl_start()-style services), an opaque context handed
 * back to it, and this service's own restart-backoff counters.
 */
typedef struct ProcessService
{
	char name[NAMEDATALEN];
	ProcessRestartPolicy policy;
	pid_t pid;
	bool (*startFunction)(void *context, pid_t *pid);
	void *context;
	ProcessRestartCounters restartCounters;
	bool gaveUp;                /* MaxR/MaxT exceeded: no longer restarted */
} ProcessService;

typedef struct ProcessSupervisor
{
	ProcessService *services;
	int serviceCount;
} ProcessSupervisor;

/* binds services[0..serviceCount) to supervisor; does not start them yet */
void process_supervisor_init(ProcessSupervisor *supervisor,
							 ProcessService *services, int serviceCount);

/*
 * process_supervisor_start_all calls every service's own startFunction()
 * once, in order, stopping and returning false at the first failure (with
 * every service started so far left running -- the caller decides what to
 * do next, exactly like ws_capture_start_all()'s own per-route "log and
 * skip this one" callers already do for a single misconfigured route).
 */
bool process_supervisor_start_all(ProcessSupervisor *supervisor);

/*
 * process_supervisor_tick drains every currently-exited child with a
 * single wildcard waitpid(-1, WNOHANG) loop -- this MUST be the only
 * waitpid(-1, ...) call site in a process that uses this supervisor:
 * two independent wildcard reapers racing for the same exited child's
 * status is exactly the bug this project has already hit twice (pg_
 * autoctl's own supervisor.c vs. a service's own sibling process, and
 * pg_walserver's own accept_loop.c reap_children() vs. this file, before
 * this fix) -- whichever reaper's waitpid() call happens to run first
 * silently consumes the zombie, and the other sees nothing (a second
 * waitpid() on an already-reaped pid returns -1/ECHILD, not the exit
 * status it needed), permanently hiding that child's death.
 *
 * A pid matching one of supervisor's own services is reaped and, per its
 * RestartPolicy and MaxR/MaxT backoff, restarted (or given up on, logged,
 * left stopped). A pid matching neither is handed to otherChildExited
 * (may be NULL) with its raw wait status -- the caller's own, separately
 * tracked children (e.g. accept_loop.c's per-connection children).
 * Anything left over (matching nothing at all) is an orphaned, reparented
 * grandchild -- only possible when running as PID 1 inside a container --
 * logged at INFO, not ERROR, and otherwise ignored, the same PID-1
 * hygiene pg_autoctl's own supervisor.c already provides.
 */
void process_supervisor_tick(ProcessSupervisor *supervisor,
							 bool (*otherChildExited)(void *ctx, pid_t pid,
													  int status),
							 void *otherCtx);

/*
 * process_supervisor_stop_all signals every still-running service with
 * signal (SIGINT/SIGTERM, never SIGKILL as the first try), waits up to
 * timeoutMs for all of them to exit, then escalates to SIGKILL for
 * anything still alive past that.
 */
void process_supervisor_stop_all(ProcessSupervisor *supervisor, int signal,
								 int timeoutMs);

#endif /* WS_PROCESS_SUPERVISOR_H */
