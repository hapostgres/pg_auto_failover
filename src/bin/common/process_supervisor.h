/*
 * src/bin/common/process_supervisor.h
 *   A small, generic Service/RestartPolicy child-process supervisor,
 *   shared by any binary in this project that needs to start and
 *   restart-on-death a fixed set of long-lived child processes -- the
 *   same class of problem `pg_autoctl`'s own `supervisor.c` already
 *   solves for its own services (postgres, listener, node-active).
 *
 *   This is a deliberately *decoupled* extraction of that file's own
 *   generic core (the `Service` struct, `RestartPolicy`, the Erlang-
 *   inspired MaxR/MaxT restart-backoff ring buffer, and PID-1-safe
 *   orphan-reaping via a single wildcard `waitpid(-1, ...)`), not a
 *   literal relocation of `pg_autoctl/supervisor.c` itself: that file is
 *   deeply entangled with `pg_autoctl`'s own keeper/monitor/node-spec
 *   machinery (it includes `keeper.h`, `keeper_config.h`, `monitor.h`,
 *   `nodespec.h`, and implements keeper-specific graceful-shutdown
 *   sequencing on top of the generic loop) -- moving it wholesale to
 *   `src/bin/common/` would mean dragging that entire subsystem along
 *   with it, which is not what "shared, decoupled utility" means.
 *   `pg_autoctl` keeps its own `supervisor.c` untouched; this file is
 *   what `pg_walserver`'s embedded pull capturer (`capture.c`) uses
 *   instead, and a future caller with the same generic need (start N
 *   long-lived children, restart them on death, don't crash-loop
 *   forever, be safe as a container's PID 1) can use it too, without
 *   linking any of `pg_autoctl`'s own keeper code.
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
 * http://erlang.org/doc/man/supervisor.html, the same reference pg_
 * autoctl's own supervisor.h cites): if more than PROCESS_SUPERVISOR_
 * MAX_RETRY restarts happen within the last PROCESS_SUPERVISOR_MAX_TIME
 * seconds, this service stops being restarted -- the same MaxR/MaxT
 * values pg_autoctl's own SUPERVISOR_SERVICE_MAX_RETRY/_MAX_TIME already
 * use, reused here rather than picked arbitrarily, per this project's own
 * existing, battle-tested policy for exactly this problem ("a service
 * that keeps dying immediately must not hot-loop the supervisor forever").
 *
 * Deliberately different from pg_autoctl's own supervisor.c in one way:
 * giving up on restarting one service here does NOT bring down every
 * other service (unlike pg_autoctl, where each service is essential to
 * the single node it manages) -- pg_walserver may be serving several
 * independent routes at once, and one route's capturer permanently
 * failing (a truly broken upstream, not a transient blip) should not stop
 * every other route pg_walserver is otherwise serving correctly.
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
