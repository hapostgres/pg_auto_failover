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
#include "service.h"
#include "signals.h"
#include "string_utils.h"


static bool keepRunning = true;

static bool is_network_healthy(Keeper *keeper);
static bool in_network_partition(KeeperStateData *keeperState, uint64_t now,
								 int networkPartitionTimeout);
static void reload_configuration(Keeper *keeper);


/*
 * keeper_node_active_loop implements the main loop of the keeper, which
 * periodically gets the goal state from the monitor and makes the state
 * transitions.
 */
bool
keeper_node_active_loop(Keeper *keeper, pid_t start_pid)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	bool doSleep = false;
	bool couldContactMonitor = false;
	bool firstLoop = true;
	bool warnedOnCurrentIteration = false;
	bool warnedOnPreviousIteration = false;

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
		if (asked_to_reload || firstLoop)
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
		if (!keeper_update_pg_state(keeper))
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

		/*
		 * Report the current state to the monitor and get the assigned state.
		 */
		couldContactMonitor =
			monitor_node_active(monitor,
								config->formation,
								config->hostname,
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
			char expectedSlotName[BUFSIZE];

			keeperState->last_monitor_contact = now;
			keeperState->assigned_role = assignedState.state;

			if (keeperState->assigned_role != keeperState->current_role)
			{
				needStateChange = true;

				log_info("Monitor assigned new state \"%s\"",
						 NodeStateToString(keeperState->assigned_role));
			}

			/*
			 * Also update the groupId and replication slot name in the
			 * configuration file.
			 */
			(void) postgres_sprintf_replicationSlotName(assignedState.nodeId,
														expectedSlotName,
														sizeof(expectedSlotName));

			if (assignedState.groupId != config->groupId ||
				strneq(config->replication_slot_name, expectedSlotName))
			{
				if (!keeper_config_set_groupId_and_slot_name(config,
															 assignedState.nodeId,
															 assignedState.groupId))
				{
					log_error("Failed to update the configuration file "
							  "with groupId %d and replication.slot \"%s\"",
							  assignedState.groupId, expectedSlotName);
					return false;
				}

				if (!keeper_ensure_configuration(keeper))
				{
					log_error("Failed to update our Postgres configuration "
							  "after a change of groupId or "
							  "replication slot name, see above for details");
					return false;
				}
			}
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

					log_info("Network in not healthy, switching to state %s",
							 NodeStateToString(keeperState->assigned_role));
				}
				else
				{
					log_info("Network is healthy");
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
		else if (couldContactMonitor)
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

		CHECK_FOR_FAST_SHUTDOWN;

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

	return service_stop(&(keeper->config.pathnames));
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

		bool missingPgdataIsOk = true;
		bool pgIsNotRunningIsOk = true;
		bool monitorDisabledIsOk = false;

		/*
		 * Set the same configuration and state file as the current config.
		 */
		strlcpy(newConfig.pathnames.config, config->pathnames.config, MAXPGPATH);
		strlcpy(newConfig.pathnames.state, config->pathnames.state, MAXPGPATH);

		/* disconnect to the current monitor if we're connected */
		(void) pgsql_finish(&(keeper->monitor.pgsql));

		if (keeper_config_read_file(&newConfig,
									missingPgdataIsOk,
									pgIsNotRunningIsOk,
									monitorDisabledIsOk) &&
			keeper_config_accept_new(config, &newConfig))
		{
			/*
			 * The keeper->config changed, not the keeper->postgres, but the
			 * main loop takes care of updating it at each loop anyway, so we
			 * don't have to take care of that now.
			 */
			log_info("Reloaded the new configuration from \"%s\"",
					 config->pathnames.config);

			/*
			 * The new configuration might impact the Postgres setup, such as
			 * when changing the SSL file paths.
			 */
			if (!keeper_ensure_configuration(keeper))
			{
				log_warn("Failed to reload pg_autoctl configuration, "
						 "see above for details");
			}
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
