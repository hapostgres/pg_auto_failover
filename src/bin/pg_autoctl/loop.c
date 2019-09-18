/*
 * src/bin/pg_autoctl/loop.c
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

#include "defaults.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "state.h"
#include "signals.h"


static bool keepRunning = true;

static bool is_network_healthy(Keeper *keeper);
static bool in_network_partition(KeeperStateData *keeperState, uint64_t now,
								 int networkPartitionTimeout);
static void reload_configuration(Keeper *keeper);

/* pid file creation and reading */
static bool create_pidfile(const char *pidfile, pid_t pid);
static bool remove_pidfile(const char *pidfile);

/*
 * keeper_fsm_run implements the main loop of the keeper, which periodically
 * gets the goal state from the monitor and makes the state transitions.
 *
 * The function keeper_service_init() must have been called before entering
 * keeper_fsm_run().
 */
bool
keeper_service_run(Keeper *keeper, pid_t *start_pid)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	bool doSleep = false;
	bool couldContactMonitor = false;
	pid_t pid = *start_pid, checkpid = 0;
	bool firstLoop = true;

	log_debug("pg_autoctl service is starting");

	while (keepRunning)
	{
		MonitorAssignedState assignedState = { 0 };
		bool needStateChange = false;
		bool transitionFailed = false;
		bool reportPgIsRunning = false;
		uint64_t now = time(NULL);

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
		if (asked_to_reload)
		{
			(void) reload_configuration(keeper);
		}

		if (asked_to_stop)
		{
			break;
		}

		if (doSleep)
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}

		doSleep = true;

		/*
		 * Before loading the current state from disk, make sure it's still our
		 * state file. It might happen that the PID file got removed from disk,
		 * then allowing another keeper to run.
		 *
		 * We should then quit in an emergency if our PID file either doesn't
		 * exist anymore, or has been overwritten with another PID, so that we
		 * don't enter a keeper state file war in between several services.
		 */
		if (read_pidfile(config->pathnames.pid, &checkpid))
		{
			if (checkpid != pid)
			{
				log_fatal("Our PID file \"%s\" now contains PID %d, "
						  "instead of expected pid %d. Quitting.",
						  config->pathnames.pid, checkpid, pid);

				exit(EXIT_CODE_QUIT);
			}
		}
		else
		{
			/*
			 * Surrendering seems the less risky option for us now.
			 *
			 * Any other strategy would need to be careful about race
			 * conditions happening when several processes (keeper or others) are
			 * trying to create or remove the pidfile at the same time, possibly in
			 * different orders. Yeah, let's quit.
			 */
			log_fatal("Our PID file disappeared from \"%s\", quitting.",
					  config->pathnames.pid);
			exit(EXIT_CODE_QUIT);
		}

		CHECK_FOR_FAST_SHUTDOWN;

		/*
		 * Read the current state. While we could preserve the state in memory,
		 * re-reading the file simplifies recovery from failures. For example,
		 * if we fail to write the state file after making a transition, then
		 * we should not tell the monitor that the transition succeeded, because
		 * a subsequent crash of the keeper would cause the states to become
		 * inconsistent. By re-reading the file, we make sure the state on disk
		 * on the keeper is consistent with the state on the monitor
		 */
		if (!keeper_load_state(keeper))
		{
			log_error("Failed to read keeper state file, retrying...");
			CHECK_FOR_FAST_SHUTDOWN;
			continue;
		}

		/*
		 * Check for any changes in the local PostgreSQL instance, and update
		 * our in-memory values for the replication WAL lag and sync_state.
		 */
		keeper_update_pg_state(keeper);

		CHECK_FOR_FAST_SHUTDOWN;

		reportPgIsRunning = ReportPgIsRunning(keeper);

		/* We used to output that in INFO every 5s, which is too much chatter */
		log_debug("Calling node_active for node %s/%d/%d with current state: "
				  "%s, "
				  "PostgreSQL %s running, "
				  "sync_state is \"%s\", "
				  "current lsn is \"%s\".",
				  config->formation,
				  keeperState->current_node_id,
				  keeperState->current_group,
				  NodeStateToString(keeperState->current_role),
				  reportPgIsRunning ? "is" : "is not",
				  postgres->pgsrSyncState,
				  postgres->currentLSN);

		if (firstLoop)
		{
			log_info("pg_autoctl service is running");
		}

		/*
		 * Report the current state to the monitor and get the assigned state.
		 */
		couldContactMonitor =
			monitor_node_active(monitor,
								config->formation,
								config->nodename,
								config->pgSetup.pgport,
								keeperState->current_node_id,
								keeperState->current_group,
								keeperState->current_role,
								reportPgIsRunning,
								postgres->currentLSN,
								postgres->pgsrSyncState,
								&assignedState);

		if (couldContactMonitor)
		{
			keeperState->last_monitor_contact = now;
			keeperState->assigned_role = assignedState.state;
		}
		else
		{
			log_error("Failed to get the goal state from the monitor");

			/*
			 * Check whether we're likely to be in a network partition.
			 * That will cause the assigned_role to become demoted.
			 */
			if (keeperState->current_role == PRIMARY_STATE)
			{
				log_warn("Checking for network partitions...");

				if (!is_network_healthy(keeper))
				{
					keeperState->assigned_role = DEMOTE_TIMEOUT_STATE;
				}
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
		if (couldContactMonitor)
		{
			if (!keeper_ensure_current_state(keeper))
			{
				log_warn("pg_autoctl failed to ensure current state \"%s\": "
						 "PostgreSQL %s running",
						 NodeStateToString(keeperState->current_role),
						 postgres->pgIsRunning ? "is" : "is not");
			}
		}

		CHECK_FOR_FAST_SHUTDOWN;

		if (keeperState->assigned_role != keeperState->current_role)
		{
			needStateChange = true;

			if (!keeper_fsm_reach_assigned_state(keeper))
			{
				log_error("Failed to transition to state \"%s\", retrying... ",
						  NodeStateToString(keeperState->assigned_role));

				transitionFailed = true;
			}
		}

		/*
		 * Even if a transition failed, we still write the state file to update
		 * timestamps used for the network partition checks.
		 */
		if (!keeper_store_state(keeper))
		{
			transitionFailed = true;
		}

		if (needStateChange && !transitionFailed)
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
	}

	return keeper_service_stop(keeper);
}


/*
 * keeper_service_init initialises the bits and pieces that the keeper service
 * depend on:
 *
 *  - sets the signal handlers
 *  - check pidfile to see if the service is already running
 *  - creates the pidfile for our service
 *  - clean-up from previous execution
 */
bool
keeper_service_init(Keeper *keeper, pid_t *pid)
{
	KeeperConfig *config = &(keeper->config);

	log_trace("keeper_service_init");

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	/* Check that the keeper service is not already running */
	if (read_pidfile(config->pathnames.pid, pid))
	{
		log_fatal("An instance of this keeper is already running with PID %d, "
				  "as seen in pidfile \"%s\"",
				  *pid, config->pathnames.pid);
		return false;
	}

	/*
	 * Check that the init is finished. This function is called from
	 * cli_service_run when used in the CLI `pg_autoctl run`, and the
	 * function cli_service_run calls into keeper_init(): we know that we could
	 * read a keeper state file.
	 */
	if (!config->monitorDisabled && file_exists(config->pathnames.init))
	{
		log_warn("The `pg_autoctl create` did not complete, completing now.");

		if (!keeper_pg_init_continue(keeper, config))
		{
			/* errors have already been logged. */
			return false;
		}
	}

	/* Ok, we're going to start. Time to create our PID file. */
	*pid = getpid();

	if (!create_pidfile(config->pathnames.pid, *pid))
	{
		log_fatal("Failed to write our PID to \"%s\"", config->pathnames.pid);
		return false;
	}

	return true;
}


/*
 * keeper_service_stop stops the service and removes the pid file.
 */
bool
keeper_service_stop(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	log_info("pg_autoctl service stopping");

	if (!remove_pidfile(config->pathnames.pid))
	{
		log_error("Failed to remove pidfile \"%s\"", config->pathnames.pid);
		return false;
	}
	return true;
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

	log_info("Failed to contact the monitor or standby in %" PRIu64 " seconds, "
			 "at %d seconds we shut down PostgreSQL to prevent split brain issues",
			 keeperState->last_monitor_contact - now, networkPartitionTimeout);

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


/*
 * reload_configuration reads the supposedly new configuration file and
 * integrates accepted new values into the current setup.
 */
static void
reload_configuration(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	if (file_exists(config->pathnames.config))
	{
		KeeperConfig newConfig = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		/*
		 * Set the same configuration and state file as the current config.
		 */
		strlcpy(newConfig.pathnames.config, config->pathnames.config, MAXPGPATH);
		strlcpy(newConfig.pathnames.state, config->pathnames.state, MAXPGPATH);

		if (keeper_config_read_file(&newConfig,
									missing_pgdata_is_ok,
									pg_is_not_running_is_ok)
			&& keeper_config_accept_new(config, &newConfig))
		{
			/*
			 * The keeper->config changed, not the keeper->postgres, but the
			 * main loop takes care of updating it at each loop anyway, so we
			 * don't have to take care of that now.
			 */
			log_info("Reloaded the new configuration from \"%s\"",
					 config->pathnames.config);
		}
		else
		{
			log_warn("Failed to read configuration file \"%s\", "
					 "continuing with the same configuration.",
					 config->pathnames.config);
		}

		/* we're done the the newConfig now */
		keeper_config_destroy(&newConfig);
	}
	else
	{
		log_warn("Configuration file \"%s\" does not exists, "
				 "continuing with the same configuration.",
				 config->pathnames.config);
	}

	/* we're done reloading now. */
	asked_to_reload = 0;
}


/*
 * create_pidfile writes our pid in a file.
 *
 * When running in a background loop, we need a pidFile to add a command line
 * tool that send signals to the process. The pidfile has a single line
 * containing our PID.
 */
static bool
create_pidfile(const char *pidfile, pid_t pid)
{
	char content[BUFSIZE];

	log_trace("create_pidfile(%d): \"%s\"", pid, pidfile);

	sprintf(content, "%d", pid);

	return write_file(content, strlen(content), pidfile);
}


/*
 * read_pidfile read the keeper's pid from a file, and returns true when we
 * could read a PID that belongs to a currently running process.
 */
bool
read_pidfile(const char *pidfile, pid_t *pid)
{
	long fileSize = 0L;
	char *fileContents = NULL;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	if (sscanf(fileContents, "%d", pid) == 1)
	{
		free(fileContents);

		/* is it a stale file? */
		if (kill(*pid, 0) == 0)
		{
			return true;
		}
		else
		{
			log_debug("Failed to signal pid %d: %s", *pid, strerror(errno));
			*pid = 0;

			log_info("Found a stale pidfile at \"%s\"", pidfile);
			log_warn("Removing the stale pid file \"%s\"", pidfile);

			/*
			 * We must return false here, after having determined that the
			 * pidfile belongs to a process that doesn't exist anymore. So we
			 * remove the pidfile and don't take the return value into account
			 * at this point.
			 */
			(void) remove_pidfile(pidfile);

			return false;
		}
	}
	else
	{
		free(fileContents);

		log_debug("Failed to read the PID file \"%s\", removing it", pidfile);
		(void) remove_pidfile(pidfile);

		return false;
	}

	/* no warning, it's cool that the file doesn't exists. */
	return false;
}


/*
 * remove_pidfile removes the keeper's pidfile.
 */
static bool
remove_pidfile(const char *pidfile)
{
	if (remove(pidfile) != 0)
	{
		log_error("Failed to remove keeper's pid file \"%s\": %s",
				  pidfile, strerror(errno));
		return false;
	}
	return true;
}
