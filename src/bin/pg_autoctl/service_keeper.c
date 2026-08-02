/*
 * src/bin/pg_autoctl/service_keeper.c
 *   The main loop of the pg_autoctl keeper
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "cli_common.h"
#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "parson.h"
#include "pgctl.h"
#include "pidfile.h"
#include "primary_standby.h"
#include "service_keeper.h"
#include "service_postgres_ctl.h"
#include "signals.h"
#include "state.h"
#include "step_socket.h"
#include "string_utils.h"
#include "supervisor.h"
#include "timeline_history.h"

#include "runprogram.h"

static bool keepRunning = true;

/* list of hooks to run at reload time */
KeeperReloadFunction KeeperReloadHooksArray[] = {
	&keeper_reload_configuration,
	&keeper_reload_citus_node_update_hostname_port,
	NULL
};

KeeperReloadFunction *KeeperReloadHooks = KeeperReloadHooksArray;

/* list of hooks to run to update a list of nodes, at node active time */
KeeperNodesArrayRefreshFunction KeeperNodesArrayRefreshArray[] = {
	&keeper_refresh_hba,
	&keeper_refresh_citus_remove_dropped_nodes,
	NULL
};

KeeperNodesArrayRefreshFunction *KeeperRefreshHooks =
	KeeperNodesArrayRefreshArray;


static bool service_keeper_node_active(Keeper *keeper, bool doInit);
static bool keeper_maybe_report_timeline_history(Keeper *keeper);
static void check_for_network_partitions(Keeper *keeper);
static bool is_network_healthy(Keeper *keeper);
static bool in_network_partition(KeeperStateData *keeperState, uint64_t now,
								 int networkPartitionTimeout);
static void keeper_graceful_shutdown(Keeper *keeper);
static bool keeper_shutdown_via_maintenance(Keeper *keeper);
static void keeper_auto_recover_shutdown_maintenance(Keeper *keeper);
static bool keeper_exit_if_previously_dropped(Keeper *keeper);


/*
 * keeper_service_start starts the keeper processes: the node_active main loop
 * and depending on the current state the Postgres instance.
 */
bool
start_keeper(Keeper *keeper)
{
	const char *pidfile = keeper->config.pathnames.pid;

	Service subprocesses[] = {
		{
			SERVICE_NAME_POSTGRES,
			RP_PERMANENT,
			-1,
			&service_postgres_ctl_start
		},
		{
			SERVICE_NAME_KEEPER,
			RP_PERMANENT,
			-1,
			&service_keeper_start,
			(void *) keeper
		}
	};

	int subprocessesCount = sizeof(subprocesses) / sizeof(subprocesses[0]);

	return supervisor_start(subprocesses, subprocessesCount, pidfile);
}


/*
 * keeper_start_node_active_process starts a sub-process that communicates with
 * the monitor to implement the node_active protocol.
 */
bool
service_keeper_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* time to create the node_active sub-process */
	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the node-active process");
			return false;
		}

		case 0:
		{
			/* here we call execv() so we never get back */
			(void) service_keeper_runprogram(keeper);

			/* unexpected */
			log_fatal("BUG: returned from service_keeper_runprogram()");
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			/* fork succeeded, in parent */
			log_debug("pg_autoctl node-active process started in subprocess %d",
					  fpid);
			*pid = fpid;
			return true;
		}
	}
}


/*
 * service_keeper_runprogram runs the node_active protocol service:
 *
 *   $ pg_autoctl do service node-active --pgdata ...
 *
 * This function is intended to be called from the child process after a fork()
 * has been successfully done at the parent process level: it's calling
 * execve() and will never return.
 */
void
service_keeper_runprogram(Keeper *keeper)
{
	char *args[12];
	int argsIndex = 0;

	char command[BUFSIZE];

	/*
	 * use --pgdata option rather than the config.
	 *
	 * On macOS when using /tmp, the file path is then redirected to being
	 * /private/tmp when using realpath(2) as we do in normalize_filename(). So
	 * for that case to be supported, we explicitely re-use whatever PGDATA or
	 * --pgdata was parsed from the main command line to start our sub-process.
	 */
	char *pgdata = keeperOptions.pgSetup.pgdata;

	setenv(PG_AUTOCTL_DEBUG, "1", 1);

	args[argsIndex++] = (char *) pg_autoctl_program;
	args[argsIndex++] = "internal";
	args[argsIndex++] = "service";
	args[argsIndex++] = "node-active";
	args[argsIndex++] = "--pgdata";
	args[argsIndex++] = pgdata;
	args[argsIndex++] = logLevelToString(log_get_level());
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	Program program = { 0 };
	(void) initialize_program(&program, args, false);

	program.capture = false;    /* redirect output, don't capture */
	program.stdOutFd = STDOUT_FILENO;
	program.stdErrFd = STDERR_FILENO;

	/* log the exact command line we're using */
	(void) snprintf_program_command_line(&program, command, BUFSIZE);

	log_info("%s", command);

	(void) execute_program(&program);
}


/*
 * service_keeper_node_active_init initializes the pg_autoctl service for the
 * node_active protocol.
 */
bool
service_keeper_node_active_init(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * Check that the init is finished. This function is called from
	 * cli_service_run when used in the CLI `pg_autoctl run`, and the
	 * function cli_service_run calls into keeper_init(): we know that we could
	 * read a keeper state file.
	 */
	if (file_exists(config->pathnames.init))
	{
		log_warn("The `pg_autoctl create` did not complete, completing now.");

		if (!keeper_pg_init_continue(keeper))
		{
			/* errors have already been logged. */
			return false;
		}
	}

	if (!keeper_init(keeper, config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	(void) keeper_auto_recover_shutdown_maintenance(keeper);

	return true;
}


/*
 * keeper_auto_recover_shutdown_maintenance runs once at node-active service
 * startup, right after the keeper state has been loaded. If the previous
 * run's graceful shutdown got far enough to persist
 * maintenanceEnteredOnShutdown (see keeper_shutdown_via_maintenance()),
 * this node's own SIGTERM handling entered maintenance on its own behalf --
 * as opposed to an operator running `pg_autoctl enable maintenance` -- so it
 * should self-clear here rather than waiting for an explicit
 * `pg_autoctl disable maintenance`.
 *
 * monitor_stop_maintenance() has its own precondition check on the monitor
 * side (the node must currently be in MAINTENANCE_STATE or
 * PREPARE_MAINTENANCE_STATE): calling it when the node has already left
 * maintenance by some other means (an operator already disabled it, or the
 * state was never actually committed on the monitor side because the
 * process was killed between persisting this flag and the
 * start_maintenance() call landing) is expected to be a harmless no-op or
 * error, logged but not treated as fatal here. Either way the flag is
 * cleared and persisted immediately, so this only ever attempts the
 * auto-recovery once per shutdown.
 */
static void
keeper_auto_recover_shutdown_maintenance(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);

	if (!keeperState->maintenanceEnteredOnShutdown ||
		keeper->config.monitorDisabled)
	{
		return;
	}

	log_info("This node entered maintenance as part of its own graceful "
			 "shutdown; automatically disabling maintenance now");

	bool mayRetry = false;

	if (!monitor_stop_maintenance(&(keeper->monitor),
								  keeperState->current_node_id,
								  &mayRetry))
	{
		log_warn("Failed to automatically disable maintenance for this "
				 "node; if it is still in maintenance, run "
				 "`pg_autoctl disable maintenance` manually");
	}

	keeperState->maintenanceEnteredOnShutdown = false;
	(void) keeper_store_state(keeper);
}


/*
 * keeper_node_active_shutdown_loop sends node_active reports to the monitor
 * every second for up to KEEPER_SHUTDOWN_LOOP_MAX_SECS, confirming Postgres
 * has stopped.
 *
 * This is the fallback path for a plain SIGTERM shutdown, used when
 * keeper_shutdown_via_maintenance() could not be used (not currently a
 * primary, monitor disabled, or start_maintenance() itself failed, e.g. no
 * candidate is available to take over). The caller, keeper_graceful_
 * shutdown(), calls ensure_postgres_service_is_stopped() before this loop
 * runs: unlike before, the Postgres-controller sibling process is not
 * directly signalled by the supervisor on a plain SIGTERM (see
 * supervisor_stop_subprocesses()), so nothing else would stop Postgres on
 * its own if we didn't.
 *
 * When SIGTERM reaches the postmaster, process_pm_shutdown_request()
 * (src/backend/postmaster/postmaster.c) sets Shutdown = FastShutdown and
 * calls UpdatePMState(PM_STOP_BACKENDS).  After that transition,
 * canAcceptConnections() (same file) returns CAC_SHUTDOWN for every new
 * connection attempt, so the primary stops accepting writes immediately —
 * before a single backend has rolled back.  Only the final checkpoint that
 * follows can be slow.
 *
 * By continuing to call node_active with the current pgIsRunning value here,
 * we ensure the monitor learns the primary is going away within one second of
 * the shutdown starting, and can begin failover right away rather than waiting
 * for a health-check timeout. In the ordinary case Postgres is already down
 * by the time this loop starts, so its first iteration reports that and
 * returns immediately; the loop only runs longer if the stop is still in
 * flight or slow to confirm, or if reporting itself fails.
 *
 * The 1-second cadence only applies while Postgres is still going down: once
 * pgIsRunning is observed false, there is no more urgency, so the loop backs
 * off to a KEEPER_SHUTDOWN_LOOP_STOPPED_REPORT_INTERVAL_SECS-second retry
 * cadence for the rest of its time budget. That slow phase only runs at all
 * when the report carrying pgIsRunning=false itself failed to reach the
 * monitor (a transient connection issue, say) -- it exists as insurance for
 * that case, not as a normal path.
 *
 * That insurance is bounded to KEEPER_SHUTDOWN_STOPPED_REPORT_MAX_ATTEMPTS
 * attempts once Postgres is confirmed stopped, rather than running for the
 * full KEEPER_SHUTDOWN_LOOP_MAX_SECS budget: a genuinely unreachable monitor
 * (as opposed to a momentary blip) means every attempt pays a full
 * connection-timeout's worth of time (which can itself be several seconds),
 * and Postgres being down locally is already the main thing this function
 * exists to ensure -- there is no reason to hold up the rest of the shutdown
 * for many multiples of that timeout just to keep trying to tell a monitor
 * that is not there to listen.
 */
static void
keeper_node_active_shutdown_loop(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	time_t start = time(NULL);
	int stoppedReportAttempts = 0;

	log_info("Graceful shutdown: reporting node state to monitor "
			 "while PostgreSQL stops (up to %d seconds)",
			 KEEPER_SHUTDOWN_LOOP_MAX_SECS);

	while (time(NULL) - start < KEEPER_SHUTDOWN_LOOP_MAX_SECS)
	{
		/* escalated signal: exit without further reporting */
		if (asked_to_quit || asked_to_stop_fast)
		{
			break;
		}

		(void) keeper_update_pg_state(keeper, LOG_DEBUG);
		bool reported = service_keeper_node_active(keeper, false);

		if (!postgres->pgIsRunning)
		{
			if (reported)
			{
				log_info("PostgreSQL has stopped; "
						 "final node_active report sent to monitor");
				break;
			}

			if (++stoppedReportAttempts >= KEEPER_SHUTDOWN_STOPPED_REPORT_MAX_ATTEMPTS)
			{
				log_warn("PostgreSQL has stopped, but the final node_active "
						 "report could not be delivered after %d attempts; "
						 "exiting anyway",
						 stoppedReportAttempts);
				break;
			}
		}

		int sleepSecs = postgres->pgIsRunning
						? 1
						: KEEPER_SHUTDOWN_LOOP_STOPPED_REPORT_INTERVAL_SECS;

		pg_usleep(sleepSecs * 1000000L);
	}
}


/*
 * keeper_shutdown_via_maintenance implements a graceful shutdown by calling
 * start_maintenance() on the node's own behalf -- the same monitor call
 * `pg_autoctl enable maintenance [--allow-failover]` already uses -- then
 * driving the ordinary FSM towards maintenance with the same
 * keeper_fsm_step() primitive used by `pg_autoctl manual fsm step`.
 *
 * This covers both roles start_maintenance() accepts:
 *
 *  - a primary (PRIMARY_STATE -> prepare_maintenance -> maintenance),
 *    promoting a standby in the process;
 *
 *  - a secondary or catching-up standby (SECONDARY_STATE/CATCHINGUP_STATE ->
 *    [wait_maintenance ->] maintenance), which prompts the primary to drop
 *    it from the replication quorum immediately instead of only noticing
 *    once the monitor's health check times out -- the same rationale as the
 *    primary case, just for the standby side of a graceful stop.
 *
 * start_maintenance() itself decides which of these applies (and whether an
 * intermediate wait_maintenance/prepare_maintenance step happens at all)
 * based on the node's currently reported state; this function only drives
 * whichever FSM path results, via keeper_fsm_step().
 *
 * Routing the shutdown through maintenance means Postgres gets stopped by
 * the ordinary FSM action functions (fsm_stop_postgres_for_primary_
 * maintenance for a primary, fsm_start_maintenance_on_standby for a
 * secondary), via the existing keeper/Postgres-controller state-file
 * protocol (ensure_postgres_service_is_stopped()). No new coordination with
 * the sibling Postgres-controller process is needed. For a primary, it is
 * also excluded from candidate selection from the moment the monitor
 * assigns PREPARE_MAINTENANCE_STATE -- structurally, not by timing luck,
 * unlike the old draining/demote_timeout path (see BuildCandidateList()'s
 * "skip old/new primary unless draining/demoted" exemption, which does not
 * extend to the maintenance states).
 *
 * Returns false only when start_maintenance() itself could not be called at
 * all (e.g. no candidate available for a primary, or for a secondary, the
 * primary isn't currently stable enough to accept the quorum change), in
 * which case the caller falls back to today's shutdown-reporting behaviour.
 * A timeout or an escalated signal arriving mid-transition also returns
 * false, as a safety net: if maintenanceEnteredOnShutdown was already
 * persisted by that point, the flag still drives the auto
 * stop_maintenance() call on next startup regardless of whether this
 * function itself reported success.
 *
 * Once current_role locally reaches MAINTENANCE_STATE, one more
 * keeper_fsm_step() call is made before returning: monitor_node_active()
 * reports the state as of the *start* of each step, before that step's own
 * transition runs, so the step that transitions into maintenance still only
 * reports the prior state (wait_maintenance or prepare_maintenance) to the
 * monitor. Skipping this extra call would exit with the monitor's
 * reportedState never having caught up to "maintenance" -- and
 * stop_maintenance() rejects a node whose reportedState isn't "maintenance"
 * outright, so keeper_auto_recover_shutdown_maintenance() on next startup
 * would fail every time, leaving the node stuck in maintenance until a
 * manual `pg_autoctl disable maintenance`.
 */
static bool
keeper_shutdown_via_maintenance(Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperStateData *keeperState = &(keeper->state);
	bool mayRetry = false;

	if (!monitor_start_maintenance(monitor, keeperState->current_node_id,
								   &mayRetry))
	{
		log_warn("Failed to enter maintenance for a graceful shutdown, "
				 "likely because there is no candidate currently available "
				 "to take over, or the primary is not currently stable");
		return false;
	}

	log_info("Graceful shutdown: requested maintenance (up to %d seconds)",
			 KEEPER_MAINTENANCE_SHUTDOWN_LOOP_MAX_SECS);

	for (int i = 0; i < KEEPER_MAINTENANCE_SHUTDOWN_LOOP_MAX_SECS; i++)
	{
		/* escalated signal: stop driving the FSM, let the caller take over */
		if (asked_to_quit || asked_to_stop_fast)
		{
			break;
		}

		if (!keeper_fsm_step(keeper))
		{
			/* errors have already been logged, try again next second */
			pg_usleep(1000000L);
			continue;
		}

		/*
		 * Flag as soon as we're committed to maintenance, not only once
		 * fully there: a primary always passes through prepare_maintenance
		 * first, and a secondary sometimes passes through wait_maintenance
		 * first, but a secondary that isn't the last quorum member goes
		 * SECONDARY/CATCHINGUP -> MAINTENANCE directly in one step, with no
		 * intermediate state to catch here -- hence also checking
		 * MAINTENANCE_STATE itself, not just the two "in progress" states.
		 */
		if (!keeperState->maintenanceEnteredOnShutdown &&
			(keeperState->current_role == PREPARE_MAINTENANCE_STATE ||
			 keeperState->current_role == WAIT_MAINTENANCE_STATE ||
			 keeperState->current_role == MAINTENANCE_STATE))
		{
			keeperState->maintenanceEnteredOnShutdown = true;
			(void) keeper_store_state(keeper);
		}

		if (keeperState->current_role == MAINTENANCE_STATE)
		{
			log_info("Reached maintenance; PostgreSQL has stopped");

			/*
			 * Report the new current_role to the monitor before exiting
			 * (see the doc comment above): this step's own transition, if
			 * any, is a no-op since assigned_role already equals
			 * current_role at this point. Best-effort: even if this fails,
			 * maintenanceEnteredOnShutdown is already persisted, and the
			 * auto-recovery attempt on next startup logs its own warning
			 * rather than looping here.
			 */
			(void) keeper_fsm_step(keeper);

			return true;
		}

		pg_usleep(1000000L); /* 1 second */
	}

	log_warn("Did not reach maintenance within %d seconds, "
			 "falling back to reporting the shutdown state",
			 KEEPER_MAINTENANCE_SHUTDOWN_LOOP_MAX_SECS);

	return false;
}


/*
 * keeper_graceful_shutdown implements pg_autoctl's response to a plain
 * SIGTERM, once the main node-active loop has exited because asked_to_stop
 * was set (and neither asked_to_stop_fast nor asked_to_quit).
 *
 * Since supervisor_stop_subprocesses() (supervisor.c) now forwards a plain
 * SIGTERM to the node-active service only, the Postgres-controller sibling
 * process is not directly signalled and won't stop Postgres on its own
 * initiative as it used to: it only reacts to the keeper/controller
 * state-file protocol. This function is therefore responsible for making
 * sure Postgres actually stops as part of a graceful shutdown, one way or
 * another, before the node-active process exits.
 */
static void
keeper_graceful_shutdown(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);

	/*
	 * PRIMARY_STATE, SECONDARY_STATE, and CATCHINGUP_STATE are exactly the
	 * roles start_maintenance() accepts (see node_active_protocol.c's
	 * start_maintenance(): IsCurrentState(primary) or reportedState in
	 * {secondary, catchingup}) -- every other current_role would just make
	 * that monitor call fail outright, so there is no point attempting it.
	 */
	bool canAttemptMaintenance =
		(keeperState->current_role == PRIMARY_STATE ||
		 keeperState->current_role == SECONDARY_STATE ||
		 keeperState->current_role == CATCHINGUP_STATE) &&
		!keeper->config.monitorDisabled;

	if (canAttemptMaintenance)
	{
		if (keeper_shutdown_via_maintenance(keeper))
		{
			/*
			 * The FSM's own maintenance action function (fsm_stop_postgres_
			 * for_primary_maintenance or fsm_start_maintenance_on_standby)
			 * already stopped Postgres as part of reaching maintenance.
			 */
			return;
		}
	}

	/*
	 * Whether we're not a primary, the monitor is disabled, or maintenance
	 * could not be used: make sure Postgres actually stops now, using the
	 * same state-file protocol any other FSM transition relies on. This is
	 * a no-op if Postgres is already stopped (e.g. maintenance got far
	 * enough to stop it before timing out above).
	 *
	 * This must happen before keeper_node_active_shutdown_loop() below, not
	 * after: that loop only reports state and waits for pgIsRunning to go
	 * false, and nothing else is going to make that happen on its own
	 * anymore. Stopping Postgres first means the loop's first iteration
	 * typically already observes it down and returns immediately, instead
	 * of always running for its full duration.
	 */
	(void) ensure_postgres_service_is_stopped(&(keeper->postgres));

	(void) keeper_node_active_shutdown_loop(keeper);
}


/*
 * keeper_exit_if_previously_dropped exits the process immediately (does not
 * return) when this node was already dropped from the monitor in a
 * previous run -- e.g. restarted by systemd after "pg_autoctl drop node"
 * ran from a distance. Returns true when it's safe to continue starting up
 * (either the monitor is disabled, in which case there's nothing to check,
 * or the node has not been dropped); returns false on error, with details
 * already logged. Shared by keeper_node_active_loop() and
 * keeper_suspended_loop(), since neither should enter its own main loop
 * without this check.
 */
static bool
keeper_exit_if_previously_dropped(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);

	if (config->monitorDisabled)
	{
		return true;
	}

	bool dropped = false;

	if (!keeper_ensure_node_has_been_dropped(keeper, &dropped))
	{
		/* errors have already been logged */
		return false;
	}

	if (dropped)
	{
		/* signal that it's time to shutdown everything */
		log_fatal("This node with id %lld in formation \"%s\" and group %d "
				  "has been dropped from the monitor",
				  (long long) keeperState->current_node_id,
				  config->formation,
				  config->groupId);

		log_info("To get rid of the configuration file and PGDATA directory, "
				 "run pg_autoctl drop node --pgdata \"%s\" --destroy",
				 config->pgSetup.pgdata);

		exit(EXIT_CODE_FATAL);
	}

	return true;
}


/*
 * keeper_node_active_loop implements the main loop of the keeper, which
 * periodically gets the goal state from the monitor and makes the state
 * transitions.
 */
bool
keeper_node_active_loop(Keeper *keeper, pid_t start_pid)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	LocalPostgresServer *postgres = &(keeper->postgres);

	bool doSleep = false;
	bool couldContactMonitor = false;
	bool firstLoop = true;
	bool doInit = true;
	bool warnedOnCurrentIteration = false;
	bool warnedOnPreviousIteration = false;

	bool nodeHasBeenDroppedFromTheMonitor = false;

	log_debug("pg_autoctl service is starting");

	/* setup our monitor client connection with our notification handler */
	(void) monitor_setup_notifications(monitor,
									   keeperState->current_group,
									   keeperState->current_node_id);

	/*
	 * When pg_autoctl drop node is used from a distance, then this node
	 * transitions to the DROPPED_STATE and shutdown cleanly. Now, if a dropped
	 * node is restarted (by systemd, an interactive user, or another way) we
	 * must realise the situation and refrain from entering our main loop.
	 */
	if (!keeper_exit_if_previously_dropped(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	while (keepRunning)
	{
		bool couldContactMonitorThisRound = false;

		bool needStateChange = false;
		bool transitionFailed = false;

		/*
		 * If we're in a stable state (current state and goal state are the
		 * same, and this didn't change in the previous loop), then we can
		 * sleep for a while. As the monitor notifies every state change, we
		 * can also interrupt our sleep as soon as we get the hint.
		 */
		if (doSleep && !config->monitorDisabled)
		{
			int timeoutMs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000;

			bool groupStateHasChanged = false;

			/* establish a connection for notifications if none present */
			(void) pgsql_prepare_to_wait(&(monitor->notificationClient));
			(void) monitor_wait_for_state_change(monitor,
												 config->formation,
												 keeperState->current_group,
												 keeperState->current_node_id,
												 timeoutMs,
												 &groupStateHasChanged);

			/* when no state change has been notified, close the connection */
			if (!groupStateHasChanged &&
				monitor->notificationClient.connectionStatementType ==
				PGSQL_CONNECTION_MULTI_STATEMENT)
			{
				pgsql_finish(&(monitor->notificationClient));
			}
		}
		else if (doSleep && config->monitorDisabled)
		{
			int timeoutUs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000 * 1000;

			pg_usleep(timeoutUs);
		}

		doSleep = true;

		/*
		 * Handle signals.
		 *
		 * When asked to STOP, we always finish the current transaction before
		 * doing so, which means we only check if asked_to_stop at the
		 * beginning of the loop.
		 *
		 * We have several places where it's safe to check if SIGQUIT has been
		 * signaled to us and from where we can immediately exit whatever we're
		 * doing. It's important to avoid e.g. leaving state.new files behind.
		 */
		if (asked_to_reload || firstLoop)
		{
			(void) keeper_call_reload_hooks(keeper, firstLoop, doInit);
		}

		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			break;
		}

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(config->pathnames.pid, start_pid);

		CHECK_FOR_FAST_SHUTDOWN;

		/*
		 * Read the current state. While we could preserve the state in memory,
		 * re-reading the file simplifies recovery from failures. For example,
		 * if we fail to write the state file after making a transition, then
		 * we should not tell the monitor that the transition succeeded, because
		 * a subsequent crash of the keeper would cause the states to become
		 * inconsistent. By re-reading the file, we make sure the state on disk
		 * on the keeper is consistent with the state on the monitor
		 *
		 * Also, when --disable-monitor is used, then we get our assigned state
		 * by reading the state file, which is edited by an external process.
		 */
		if (!keeper_load_state(keeper))
		{
			log_error("Failed to read keeper state file, retrying...");
			CHECK_FOR_FAST_SHUTDOWN;
			continue;
		}

		if (firstLoop)
		{
			log_info("pg_autoctl service is running, "
					 "current state is \"%s\"",
					 NodeStateToString(keeperState->current_role));
		}

		/*
		 * Check for any changes in the local PostgreSQL instance, and update
		 * our in-memory values for the replication WAL lag and sync_state.
		 */
		if (!keeper_update_pg_state(keeper, LOG_WARN))
		{
			warnedOnCurrentIteration = true;
			log_warn("Failed to update the keeper's state from the local "
					 "PostgreSQL instance.");
		}
		else if (warnedOnPreviousIteration)
		{
			log_info("Updated the keeper's state from the local "
					 "PostgreSQL instance, which is %s",
					 postgres->pgIsRunning ? "running" : "not running");
		}

		CHECK_FOR_FAST_SHUTDOWN;

		/*
		 * If the monitor is disabled, read the list of other nodes from our
		 * file on-disk at config->pathnames.nodes. The following command can
		 * be used to fill-in that file:
		 *
		 *  $ pg_autoctl do fsm nodes set nodes.json
		 */
		if (config->monitorDisabled)
		{
			/* force cache invalidation when reaching WAIT_STANDBY */
			bool forceCacheInvalidation =
				keeperState->current_role == WAIT_STANDBY_STATE;

			/* maybe update our cached list of other nodes */
			if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
			{
				/* we will try again... */
				log_warn("Failed to update our list of other nodes");
				continue;
			}
		}
		/*
		 * If the monitor is not disabled, call the node_active function on the
		 * monitor and update the keeper data structure accordingy, refreshing
		 * our cache of other nodes if needed.
		 */
		else
		{
			couldContactMonitorThisRound =
				service_keeper_node_active(keeper, doInit);

			if (!couldContactMonitor &&
				couldContactMonitorThisRound &&
				!firstLoop)
			{
				/*
				 * Last message the user saw in the output is the following,
				 * and so we should say that we're back to the expected
				 * situation:
				 *
				 * Failed to get the goal state from the monitor
				 */
				log_info("Successfully got the goal state from the monitor");
			}

			couldContactMonitor = couldContactMonitorThisRound;

			/*
			 * Check for a new local timeline and publish it to the monitor,
			 * unconditionally of the current FSM state: an uneventful
			 * secondary quietly follows timeline switches through ordinary
			 * streaming replication, with no fsm_* transition function ever
			 * running, so this can't be hooked off specific transitions --
			 * it has to poll every tick like this. Cheap: only touches the
			 * filesystem when postgresSetup.control.timeline_id (already
			 * refreshed above by keeper_update_pg_state) has advanced past
			 * what we last published.
			 */
			if (couldContactMonitorThisRound)
			{
				(void) keeper_maybe_report_timeline_history(keeper);
			}
		}

		if (keeperState->assigned_role != keeperState->current_role)
		{
			needStateChange = true;

			if (couldContactMonitor)
			{
				log_info("Monitor assigned new state \"%s\"",
						 NodeStateToString(keeperState->assigned_role));
			}
			else
			{
				/* if network is not healthy we might self-assign a state */
				log_info("Reaching new state \"%s\"",
						 NodeStateToString(keeperState->assigned_role));
			}
		}

		CHECK_FOR_FAST_SHUTDOWN;

		/*
		 * If we see that PostgreSQL is not running when we know it should be,
		 * the least we can do is start PostgreSQL again. Same if PostgreSQL is
		 * running and we are DEMOTED, or in another one of those states where
		 * the monitor asked us to stop serving queries, in order to ensure
		 * consistency.
		 *
		 * Only enfore current state when we have a recent enough version of
		 * it, meaning that we could contact the monitor.
		 *
		 * We need to prevent the keeper from restarting PostgreSQL at boot
		 * time when meanwhile the Monitor did set our goal_state to DEMOTED
		 * because the other node has been promoted, which could happen if this
		 * node was rebooting for a long enough time.
		 */
		if (needStateChange)
		{
			/*
			 * First, ensure the current state (make sure Postgres is running
			 * if it should, or Postgres is stopped if it should not run).
			 *
			 * The transition function we call next might depend on our
			 * assumption that Postgres is running in the current state.
			 */
			if (keeper_should_ensure_current_state_before_transition(keeper))
			{
				if (!keeper_ensure_current_state(keeper))
				{
					/*
					 * We don't take care of the warnedOnCurrentIteration here
					 * because the real thing that should happen is the
					 * transition to the next state. That's what we keep track
					 * of with "transitionFailed".
					 */
					log_warn(
						"pg_autoctl failed to ensure current state \"%s\": "
						"PostgreSQL %s running",
						NodeStateToString(keeperState->current_role),
						postgres->pgIsRunning ? "is" : "is not");
				}
			}

			if (!keeper_fsm_reach_assigned_state(keeper))
			{
				log_error("Failed to transition to state \"%s\", retrying... ",
						  NodeStateToString(keeperState->assigned_role));

				transitionFailed = true;
			}
		}
		else if (couldContactMonitor || config->monitorDisabled)
		{
			if (!keeper_ensure_current_state(keeper))
			{
				warnedOnCurrentIteration = true;
				log_warn("pg_autoctl failed to ensure current state \"%s\": "
						 "PostgreSQL %s running",
						 NodeStateToString(keeperState->current_role),
						 postgres->pgIsRunning ? "is" : "is not");
			}
			else if (warnedOnPreviousIteration)
			{
				log_info("pg_autoctl managed to ensure current state \"%s\": "
						 "PostgreSQL %s running",
						 NodeStateToString(keeperState->current_role),
						 postgres->pgIsRunning ? "is" : "is not");
			}
		}

		/* now is a good time to make sure we're closing our connections */
		pgsql_finish(&(postgres->sqlClient));

		CHECK_FOR_FAST_SHUTDOWN;

		/*
		 * Write the current (changed) state to disk.
		 *
		 * When using a monitor, even if a transition failed, we still write
		 * the state file to update timestamps used for the network partition
		 * checks.
		 *
		 * When the monitor is disabled, only write the state to disk when we
		 * just successfully implemented a state change.
		 */
		if (!config->monitorDisabled || (needStateChange && !transitionFailed))
		{
			if (!keeper_store_state(keeper))
			{
				transitionFailed = true;
			}
		}

		/*
		 * If the node has been dropped, we exit the process... after having
		 * done at least another round where we could contact the monitor to
		 * report that we reached the assigned state.
		 */
		if ((couldContactMonitor || config->monitorDisabled) &&
			keeperState->current_role == DROPPED_STATE &&
			keeperState->current_role == keeperState->assigned_role)
		{
			if (nodeHasBeenDroppedFromTheMonitor)
			{
				keepRunning = false;
			}
			else
			{
				nodeHasBeenDroppedFromTheMonitor = true;
			}
		}

		if ((needStateChange ||
			 (!config->monitorDisabled &&
			  monitor_has_received_notifications(monitor))) &&
			!transitionFailed)
		{
			/* cycle faster if we made a state transition */
			doSleep = false;
		}

		if (asked_to_stop || asked_to_stop_fast)
		{
			keepRunning = false;
		}

		if (firstLoop)
		{
			firstLoop = false;
		}

		/* if we failed to contact the monitor, we must re-try the init steps */
		if (doInit && couldContactMonitorThisRound)
		{
			doInit = false;
		}

		/*
		 * On the first loop, we might have reload-time actions to implement
		 * before and after having contacted the monitor. For instance,
		 * contacting the monitor might show that we're not a primary anymore
		 * after having been DEMOTED during a failover, while this node was
		 * rebooting or something.
		 *
		 * So in some cases, we want to do two rounds of start-up reload:
		 *
		 *   reload-hook(firstLoop => true, doInit => true)
		 *   reload-hook(firstLoop => true, doInit => false)
		 *
		 * Later SIGHUP signal processing will trigger a call to our reload
		 * hooks with both firstLoop and doInit false, and that's handled
		 * earlier in this loop.
		 */
		if (firstLoop)
		{
			(void) keeper_call_reload_hooks(keeper, firstLoop, doInit);
		}

		/* advance the warnings "counters" */
		if (warnedOnPreviousIteration)
		{
			warnedOnPreviousIteration = false;
		}

		if (warnedOnCurrentIteration)
		{
			warnedOnPreviousIteration = true;
			warnedOnCurrentIteration = false;
		}
	}

	/*
	 * Graceful SIGTERM shutdown: route a primary through maintenance so a
	 * standby can take over immediately, or otherwise make sure Postgres
	 * stops as part of our own exit.  Skip on SIGINT/SIGQUIT which request
	 * immediate exit.
	 */
	if (asked_to_stop && !asked_to_stop_fast && !asked_to_quit)
	{
		(void) keeper_graceful_shutdown(keeper);
	}

	/* One last check that we do not have any connections open */
	pgsql_finish(&(keeper->monitor.pgsql));
	pgsql_finish(&(monitor->notificationClient));

	if (nodeHasBeenDroppedFromTheMonitor)
	{
		/* signal that it's time to shutdown everything */
		exit(EXIT_CODE_DROPPED);
	}

	return true;
}


/*
 * keeper_suspended_loop implements the main loop of the keeper when the
 * node is suspended (PG_AUTOCTL_SUSPENDED), in place of
 * keeper_node_active_loop(): instead of ticking on its own, this node
 * blocks on a small Unix-domain control socket and only reports to the
 * monitor or attempts a transition when explicitly told to via
 * "pg_autoctl manual fsm step[ report|advance]" (see step_socket.c). This
 * gives precise, gdb-step-like external control over the keeper's FSM --
 * used by pgaftest's "suspended" node modifier to freeze a node at a
 * specific reported state, and available for an operator driving manual
 * recovery one transition at a time.
 */
bool
keeper_suspended_loop(Keeper *keeper, pid_t start_pid)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);

	bool doSleep = false;
	bool firstLoop = true;

	int stepListenFd = -1;
	char stepSocketPath[MAXPGPATH] = { 0 };

	log_debug("pg_autoctl service is starting suspended");

	if (!step_socket_listen(config->pathnames.pid,
							stepSocketPath, sizeof(stepSocketPath),
							&stepListenFd))
	{
		log_fatal("Failed to create the pg_autoctl suspended-node control "
				  "socket, see above for details");
		return false;
	}

	log_info("pg_autoctl is suspended: waiting for "
			 "\"pg_autoctl manual fsm step\" commands on \"%s\" instead of "
			 "ticking automatically", stepSocketPath);

	/*
	 * When pg_autoctl drop node is used from a distance, then this node
	 * transitions to the DROPPED_STATE and shutdown cleanly. Now, if a dropped
	 * node is restarted (by systemd, an interactive user, or another way) we
	 * must realise the situation and refrain from entering our main loop.
	 */
	if (!keeper_exit_if_previously_dropped(keeper))
	{
		/* errors have already been logged */
		return false;
	}

	while (keepRunning)
	{
		bool stepCommandReceived = false;
		int stepClientFd = -1;
		char stepCommand[NAMEDATALEN] = { 0 };

		/*
		 * We never tick on our own: instead of sleeping, we block (with a
		 * timeout, so signals are still handled promptly) on our control
		 * socket for the next externally-issued "step" command. Skipped on
		 * the very first pass through the loop so that first-loop-only
		 * reload actions below run immediately at startup.
		 */
		if (doSleep)
		{
			int timeoutMs = PG_AUTOCTL_KEEPER_SLEEP_TIME * 1000;

			stepCommandReceived =
				step_socket_wait_for_command(stepListenFd, timeoutMs, &stepClientFd,
											 stepCommand, sizeof(stepCommand));
		}

		doSleep = true;

		/*
		 * Handle signals.
		 *
		 * When asked to STOP, we always finish the current transaction before
		 * doing so, which means we only check if asked_to_stop at the
		 * beginning of the loop.
		 *
		 * We have several places where it's safe to check if SIGQUIT has been
		 * signaled to us and from where we can immediately exit whatever we're
		 * doing. It's important to avoid e.g. leaving state.new files behind.
		 */
		if (asked_to_reload || firstLoop)
		{
			(void) keeper_call_reload_hooks(keeper, firstLoop, false);
		}

		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			break;
		}

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(config->pathnames.pid, start_pid);

		CHECK_FOR_FAST_SHUTDOWN;

		if (firstLoop)
		{
			firstLoop = false;
		}

		if (!stepCommandReceived)
		{
			continue;
		}

		/*
		 * Unlike the autonomous tick body (service_keeper_node_active(),
		 * called from keeper_node_active_loop()), keeper_fsm_step() never
		 * refreshes our cached list of other nodes -- it wasn't written
		 * to need to, since its only other caller is the one-shot
		 * "pg_autoctl manual fsm step" CLI, where a fresh keeper_init()
		 * per invocation already populates that cache from scratch each
		 * time. This long-lived process only calls keeper_init() once,
		 * so without an explicit refresh here that cache would stay
		 * pinned to however many peers existed at suspended-node startup --
		 * silently stalling replication-slot and HBA maintenance for
		 * any peer that joined afterwards. A failure here is not fatal:
		 * fall through to keeper_fsm_step() regardless, exactly as a
		 * failed refresh in autonomous mode just retries next tick.
		 */
		bool forceCacheInvalidation = false;

		if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
		{
			log_warn("Failed to update our list of other nodes, "
					 "stepping the FSM anyway");
		}

		NodeState oldRole = keeperState->current_role;
		bool stepOk = false;

		/*
		 * REPORT and ADVANCE split keeper_fsm_step()'s own combined
		 * "report to the monitor, then immediately attempt whatever
		 * transition it just assigned" into two independently-issuable
		 * commands -- see keeper_fsm_step_report/_advance's own
		 * comments (fsm.c) for why a pgaftest spec needs that
		 * separation to freeze a node's own reportedState while a
		 * *different* node's report bumps this one's goalState via a
		 * MonitorFSM[] fan-out, something keeper_fsm_step's atomic
		 * shape makes unobservable.
		 */
		if (streq(stepCommand, STEP_SOCKET_COMMAND_REPORT))
		{
			stepOk = keeper_fsm_step_report(keeper);
		}
		else if (streq(stepCommand, STEP_SOCKET_COMMAND_ADVANCE))
		{
			stepOk = keeper_fsm_step_advance(keeper);
		}
		else
		{
			stepOk = keeper_fsm_step(keeper);
		}

		if (stepOk)
		{
			NodeState newRole =
				streq(stepCommand, STEP_SOCKET_COMMAND_REPORT)
				? keeperState->assigned_role
				: keeperState->current_role;

			(void) step_socket_respond_ok(stepClientFd,
										  NodeStateToString(oldRole),
										  NodeStateToString(newRole));
		}
		else
		{
			(void) step_socket_respond_error(stepClientFd,
											 "failed to step the keeper's FSM, "
											 "see the pg_autoctl logs for details");
		}

		close(stepClientFd);
	}

	/*
	 * Graceful SIGTERM shutdown: route a primary through maintenance so a
	 * standby can take over immediately, or otherwise make sure Postgres
	 * stops as part of our own exit.  Skip on SIGINT/SIGQUIT which request
	 * immediate exit.
	 */
	if (asked_to_stop && !asked_to_stop_fast && !asked_to_quit)
	{
		(void) keeper_graceful_shutdown(keeper);
	}

	(void) step_socket_close(stepListenFd, stepSocketPath);

	return true;
}


/*
 * keeper_node_active calls the node_active function on the monitor, and when
 * it could contact the monitor it also updates our copy of the list of other
 * nodes currenty in the group (keeper->otherNodes).
 *
 * keeper_node_active returns true if it could successfully connect to the
 * monitor, and false otherwise. When it returns false, it also checks for
 * network partitions and set the goal state to DEMOTE_TIMEOUT_STATE when
 * needed.
 */
static bool
service_keeper_node_active(Keeper *keeper, bool doInit)
{
	KeeperStateData *keeperState = &(keeper->state);

	MonitorAssignedState assignedState = { 0 };

	uint64_t now = time(NULL);

	/*
	 * Report the current state to the monitor and get the assigned state.
	 */
	if (!keeper_node_active(keeper, doInit, &assignedState))
	{
		log_error("Failed to get the goal state from the monitor");

		/*
		 * Check whether we're likely to be in a network partition.
		 * That will cause the assigned_role to become demoted.
		 */
		(void) check_for_network_partitions(keeper);

		return false;
	}

	/*
	 * We could contact the monitor, update our internal state.
	 */
	keeperState->last_monitor_contact = now;
	keeperState->assigned_role = assignedState.state;

	if (keeperState->assigned_role != keeperState->current_role)
	{
		log_debug("keeper_node_active: %s ➜ %s",
				  NodeStateToString(keeperState->current_role),
				  NodeStateToString(keeperState->assigned_role));
	}

	/* maybe update our cached list of other nodes */
	if (keeperState->current_role == DROPPED_STATE &&
		keeperState->current_role == keeperState->assigned_role)
	{
		return true;
	}

	bool forceCacheInvalidation = false;

	if (!keeper_refresh_other_nodes(keeper, forceCacheInvalidation))
	{
		/*
		 * We have a new MD5 but failed to update our list, try again next
		 * round, the monitor might be restarting or something.
		 */
		log_error("Failed to update our list of other nodes");
		return false;
	}

	/*
	 * Also update the groupId and replication slot name in the
	 * configuration file, if the monitor's own view of who we are has
	 * drifted from ours.
	 */
	if (!keeper_maybe_update_group_and_slot(keeper, &assignedState))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
}


/*
 * keeper_maybe_report_timeline_history checks the local node's current
 * timeline (already refreshed this tick by keeper_update_pg_state(), from
 * pg_control -- no extra syscall here) against the highest timeline this
 * process has already published, and reports the local timeline history to
 * the monitor when it has advanced.
 *
 * lastPublishedTLI is deliberately a plain in-memory watermark, not
 * persisted state: on every fresh process start it's zero again, so the
 * first tick after any pg_autoctl restart always re-publishes -- cheap
 * (ON CONFLICT DO NOTHING makes the insert a no-op when nothing changed),
 * and it means we don't need to worry about the watermark itself getting
 * out of sync with what the monitor actually has on file.
 */
static bool
keeper_maybe_report_timeline_history(Keeper *keeper)
{
	static uint32_t lastPublishedTLI = 0;
	static IdentifySystem system = { 0 };

	LocalPostgresServer *postgres = &(keeper->postgres);
	uint32_t currentTLI = postgres->postgresSetup.control.timeline_id;

	if (currentTLI == 0 || currentTLI <= lastPublishedTLI)
	{
		return true;
	}

	if (!keeper_fetch_local_timeline_history(&(postgres->postgresSetup),
											 currentTLI,
											 &system))
	{
		log_warn("Failed to read the local timeline history for timeline %d",
				 currentTLI);
		return false;
	}

	char *historyJSON = timeline_history_to_json(&system);

	if (historyJSON == NULL)
	{
		log_warn("Failed to encode the local timeline history as JSON");
		return false;
	}

	bool reported =
		monitor_report_timeline_history(&(keeper->monitor),
										keeper->state.current_node_id,
										historyJSON);

	json_free_serialized_string(historyJSON);

	if (reported)
	{
		lastPublishedTLI = currentTLI;
	}
	else
	{
		log_warn("Failed to report the local timeline history "
				 "for timeline %d",
				 currentTLI);
	}

	return reported;
}


/*
 * check_for_network_partitions checks whether we're likely to be in a network
 * partition. That will cause the assigned_role to become demoted.
 */
static void
check_for_network_partitions(Keeper *keeper)
{
	KeeperStateData *keeperState = &(keeper->state);

	if (keeperState->current_role == PRIMARY_STATE)
	{
		log_warn("Checking for network partitions...");

		if (!is_network_healthy(keeper))
		{
			keeperState->assigned_role = DEMOTE_TIMEOUT_STATE;

			log_info("Network in not healthy, switching to state %s",
					 NodeStateToString(keeperState->assigned_role));
		}
		else
		{
			log_info("Network is healthy");
		}
	}
}


/*
 * is_network_healthy returns false if the keeper appears to be in a
 * network partition, which it assumes to be the case if it cannot
 * communicate with neither the monitor, nor the secondary for at least
 * network_partition_timeout seconds.
 *
 * On the other side of the network partition, the monitor and the secondary
 * may proceed with a failover once the network partition timeout has passed,
 * since they are sure the primary is down at that point.
 */
static bool
is_network_healthy(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	LocalPostgresServer *postgres = &(keeper->postgres);
	int networkPartitionTimeout = config->network_partition_timeout;
	uint64_t now = time(NULL);
	bool hasReplica = false;

	if (keeperState->current_role != PRIMARY_STATE)
	{
		/*
		 * Fail-over may only occur if we're currently the primary, so
		 * we don't need to check for network partitions in other states.
		 */
		return true;
	}

	if (primary_has_replica(postgres, PG_AUTOCTL_REPLICA_USERNAME, &hasReplica) &&
		hasReplica)
	{
		keeperState->last_secondary_contact = now;
		log_warn("We lost the monitor, but still have a standby: "
				 "we're not in a network partition, continuing.");
		return true;
	}

	if (!in_network_partition(keeperState, now, networkPartitionTimeout))
	{
		/* still had recent contact with monitor and/or secondary */
		return true;
	}

	log_info("Failed to contact the monitor or standby in %d seconds, "
			 "at %d seconds we shut down PostgreSQL to prevent split brain issues",
			 (int) (now - keeperState->last_monitor_contact),
			 networkPartitionTimeout);

	return false;
}


/*
 * in_network_partition determines if we're in a network partition by applying
 * the configured network_partition_timeout to current known values. Updating
 * the state before calling this function is advised.
 */
static bool
in_network_partition(KeeperStateData *keeperState, uint64_t now,
					 int networkPartitionTimeout)
{
	uint64_t monitor_contact_lag = (now - keeperState->last_monitor_contact);
	uint64_t secondary_contact_lag = (now - keeperState->last_secondary_contact);

	return keeperState->last_monitor_contact > 0 &&
		   keeperState->last_secondary_contact > 0 &&
		   networkPartitionTimeout < monitor_contact_lag &&
		   networkPartitionTimeout < secondary_contact_lag;
}
