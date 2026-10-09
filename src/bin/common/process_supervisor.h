/*
 * src/bin/common/process_supervisor.h
 *   A small, generic Service/RestartPolicy child-process supervisor,
 *   shared by any binary in this project that needs to start and
 *   restart-on-death a fixed set of long-lived child processes.
 *
 *   This file is genuinely shared, and genuinely consumed, by both
 *   `pg_walserver` (its embedded receivewal worker, `receivewal.c`, uses the
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
 * independent clusters at once, and one cluster's receivewal worker permanently
 * failing (a truly broken upstream, not a transient blip) should not stop
 * every other cluster pg_walserver is otherwise serving correctly. That
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

void process_restart_counters_start(ProcessRestartCounters *counters,
									uint64_t now);

void process_restart_counters_record(ProcessRestartCounters *counters,
									 uint64_t now);

bool process_restart_counters_may_restart(ProcessRestartCounters *counters);

void process_supervisor_log_unknown_pid(pid_t pid);

/*
 * One supervised child: a name (for logging), a restart policy, its
 * current pid (-1 when not running, "gave up" state included), a start
 * function (fork()+exec() is the expected shape, see receivewal.c's own
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

void process_supervisor_init(ProcessSupervisor *supervisor,
							 ProcessService *services, int serviceCount);

bool process_supervisor_start_all(ProcessSupervisor *supervisor);

void process_supervisor_tick(ProcessSupervisor *supervisor,
							 bool (*otherChildExited)(void *ctx, pid_t pid,
													  int status),
							 void *otherCtx);

void process_supervisor_stop_all(ProcessSupervisor *supervisor, int signal,
								 int timeoutMs);

#endif /* WS_PROCESS_SUPERVISOR_H */
