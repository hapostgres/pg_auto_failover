/*
 * src/bin/pg_autoctl/supervisor.h
 *   Utilities to start/stop the pg_autoctl services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <inttypes.h>
#include <signal.h>

#include "nodespec.h"

/*
 * pg_autoctl runs sub-processes as "services", and we need to use the same
 * service names in several places:
 *
 *  - the main pidfile,
 *  - the per-service name for the pidfile is derived from this,
 *  - the pg_autoctl manual service getpid|restart commands
 */
#define SERVICE_NAME_POSTGRES "postgres"
#define SERVICE_NAME_KEEPER "node-active"
#define SERVICE_NAME_MONITOR "listener"

/* an archiver's two top-level halves, supervised together by
 * start_archiver() (service_archiver_run.c) -- see that file's own header
 * comment. "reconciler" in turn supervises one WAL-capture child per
 * (formation, group) membership this archiver holds -- named
 * "archiver-capture-<formation>-<group>" each, not a single fixed name,
 * since there can be any number of them (service_archiver_reconciler.c). */
#define SERVICE_NAME_ARCHIVER_SERVE "archiver-serve"
#define SERVICE_NAME_ARCHIVER_RECONCILER "archiver-reconciler"

/*
 * At pg_autoctl create time we use a transient service to initialize our local
 * node. When using the --run option, the transient service is terminated and
 * we start the permanent service with the name defined above.
 */
#define SERVICE_NAME_KEEPER_INIT "node-init"
#define SERVICE_NAME_MONITOR_INIT "monitor-init"

/*
 * Our supervisor process may retart a service sub-process when it quits,
 * depending on the exit status and the restart policy that has been choosen:
 *
 * - A permanent child process is always restarted.
 *
 * - A temporary child process is never restarted.
 *
 * - A transient child process is restarted only if it terminates abnormally,
 *   that is, with an exit code other EXIT_CODE_QUIT (zero).
 */
typedef enum
{
	RP_PERMANENT = 0,
	RP_TEMPORARY,
	RP_TRANSIENT
} RestartPolicy;


/*
 * Supervisor restart strategy.
 *
 * The idea is to restart processes that have failed, so that we can stay
 * available without external intervention. Sometimes though if the
 * configuration is wrong or the data directory damaged beyond repair or for
 * some reasons, the service can't be restarted.
 *
 * This strategy is inspired by http://erlang.org/doc/man/supervisor.html and
 * http://erlang.org/doc/design_principles/sup_princ.html#maximum-restart-intensity
 *
 *    If more than MaxR number of restarts occur in the last MaxT seconds, the
 *    supervisor terminates all the child processes and then itself. The
 *    termination reason for the supervisor itself in that case will be
 *    shutdown.
 *
 * SUPERVISOR_SERVICE_MAX_RETRY is MaxR, SUPERVISOR_SERVICE_MAX_TIME is MaxT.
 */
#define SUPERVISOR_SERVICE_MAX_RETRY 5
#define SUPERVISOR_SERVICE_MAX_TIME 300 /* in seconds */

/*
 * How long supervisor_remove_service() waits for a signalled service to
 * actually exit before giving up and removing it from supervision anyway.
 */
#define SUPERVISOR_REMOVE_SERVICE_MAX_WAIT_MS 5000

/*
 * We use a "ring buffer" of the MaxR most recent retries.
 *
 * With an array of SUPERVISOR_SERVICE_MAX_RETRY we can track this amount of
 * retries and compare the oldest one with the current time to decide if we are
 * allowed to restart or now, applying MaxT.
 */
typedef struct RestartCounters
{
	int count;                  /* how many restarts including first start */
	int position;               /* array index */
	uint64_t startTime[SUPERVISOR_SERVICE_MAX_RETRY];
}  RestartCounters;

/*
 * The supervisor works with an array of Service entries. Each service defines
 * its behavior thanks to a start function, a stop function, and a reload
 * function. Those are called at different points to adjust to the situation as
 * seen by the supervisor.
 *
 * In particular, services may be started more than once when they fail.
 */
typedef struct Service
{
	char name[NAMEDATALEN];             /* Service name for the user */
	RestartPolicy policy;               /* Should we restart the service? */
	pid_t pid;                          /* Service PID */
	bool (*startFunction)(void *context, pid_t *pid);
	void *context;             /* Service Context (Monitor or Keeper struct) */
	RestartCounters restartCounters;
} Service;


typedef enum
{
	SUPERVISOR_EXIT_ERROR = 0,
	SUPERVISOR_EXIT_CLEAN,
	SUPERVISOR_EXIT_FATAL
} SupervisorExitMode;

typedef struct Supervisor
{
	Service *services;
	int serviceCount;
	char pidfile[MAXPGPATH];
	pid_t pid;
	SupervisorExitMode exitMode;
	bool shutdownSequenceInProgress;
	int shutdownSignal;
	int stoppingLoopCounter;

	/*
	 * Tracking for the keeper-only SIGTERM graceful shutdown (see
	 * supervisor_stop_subprocesses() in supervisor.c): whether the
	 * node-active (keeper) service has exited yet, and the stoppingLoopCounter
	 * value at which the stuck-process escalation should kick in, computed
	 * once when the shutdown sequence begins.
	 */
	bool keeperExited;
	int stuckThresholdLoops;

	/*
	 * Optional node spec watcher.  When pg_autoctl is started via
	 * `pg_autoctl node run <file>`, the supervisor watches the ini file for
	 * changes and converges mutable settings automatically.
	 *
	 * watcher.active is false when not in use (normal create/run path).
	 */
	NodeSpecWatcher watcher;
	NodeSpec watchedSpec;          /* last-applied spec — baseline for diff */

	/*
	 * Optional periodic callback, invoked once per supervisor_loop()
	 * iteration -- the same cadence as the node spec watcher above (as
	 * often as every 100ms when otherwise idle, more often during child
	 * churn). NULL (the default, set via supervisor_start()) is a no-op.
	 * A caller that wants periodic work done at a coarser cadence than
	 * that -- the archiver reconciler's own membership-list polling, for
	 * instance, see service_archiver_reconciler.c -- is expected to
	 * track elapsed wall-clock time itself and mostly return
	 * immediately, the same way service_archiver_serve.c's own
	 * tick-counted routes refresh does at a different layer. Set via
	 * supervisor_start_with_callback() rather than supervisor_start(),
	 * so every existing caller is unaffected.
	 *
	 * This periodic callback is also the intended, and only supported,
	 * way to call supervisor_add_service()/supervisor_remove_service()
	 * below: both realloc `services`, which requires it to already be a
	 * heap-allocated array -- true only for a caller that built its own
	 * initial array that way before calling
	 * supervisor_start_with_callback(), never for the plain stack/static
	 * arrays every ordinary supervisor_start() caller passes in.
	 */
	void (*periodicCallback)(struct Supervisor *supervisor, void *context);
	void *periodicCallbackContext;

	/*
	 * How many currently-tracked services are still expected to
	 * eventually report exiting before supervisor_loop() may return --
	 * decremented as each one permanently exits, incremented when one is
	 * restarted instead. Used to be a plain local variable inside
	 * supervisor_loop() itself; promoted onto the struct so that
	 * supervisor_add_service()/supervisor_remove_service(), called from
	 * outside that function (via the periodic callback above), can keep
	 * it consistent too.
	 */
	int pendingSubprocessCount;
} Supervisor;


bool supervisor_start(Service services[], int serviceCount, const char *pidfile);

bool supervisor_start_with_callback(Service services[], int serviceCount,
									const char *pidfile,
									void (*periodicCallback)(Supervisor *supervisor,
															 void *context),
									void *periodicCallbackContext);

bool supervisor_stop(Supervisor *supervisor);

bool supervisor_find_service_pid(const char *pidfile,
								 const char *serviceName,
								 pid_t *pid);

bool supervisor_add_service(Supervisor *supervisor, Service service);

bool supervisor_remove_service(Supervisor *supervisor, pid_t pid, int signal);


#endif /* SUPERVISOR_H */
