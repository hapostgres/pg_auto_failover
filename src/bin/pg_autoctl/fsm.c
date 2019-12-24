/*
 * src/bin/pg_autoctl/fsm.c
 *   Finite State Machine implementation for pg_autoctl.
 *
 * The state machine transitions are decided by the pg_auto_failover monitor
 * and implemented on the local Postgres node by the pg_autoctl service. This
 * is the client-side implementation. We refer to this service as the "keeper",
 * it is the local agent that executes the pg_auto_failover decisions.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "keeper.h"
#include "pgctl.h"
#include "fsm.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"


/*
 * Comments displayed in the logs when state changes.
 */
#define COMMENT_INIT_TO_SINGLE \
	"Start as a single node"

#define COMMENT_PRIMARY_TO_SINGLE \
	"Other node was forcibly removed, now single"

#define COMMENT_DEMOTED_TO_SINGLE \
	"Was demoted after a failure, " \
	"but secondary was forcibly removed"

#define COMMENT_LOST_PRIMARY \
	"Primary was forcibly removed"

#define COMMENT_REPLICATION_TO_SINGLE \
	"Went down to force the primary to time out, " \
	"but then it was removed"

#define COMMENT_SINGLE_TO_WAIT_PRIMARY \
	"A new secondary was added"

#define COMMENT_PRIMARY_TO_WAIT_PRIMARY \
	"Secondary became unhealthy"

#define COMMENT_PRIMARY_TO_JOIN_PRIMARY \
	"A new secondary was added"

#define COMMENT_PRIMARY_TO_DRAINING \
	"A failover occurred, stopping writes "

#define COMMENT_PRIMARY_TO_DEMOTED \
	"A failover occurred, no longer primary"

#define COMMENT_DRAINING_TO_DEMOTED \
	"Demoted after a failover, no longer primary"

#define COMMENT_DRAINING_TO_DEMOTE_TIMEOUT \
	"Secondary confirms it’s receiving no more writes"

#define COMMENT_DEMOTE_TIMEOUT_TO_DEMOTED \
	"Demote timeout expired"

#define COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY \
	"Confirmed promotion with the monitor"

#define COMMENT_WAIT_PRIMARY_TO_PRIMARY \
	"A healthy secondary appeared"

#define COMMENT_JOIN_PRIMARY_TO_PRIMARY \
	"A healthy secondary appeared"

#define COMMENT_DEMOTE_TO_PRIMARY \
	"Detected a network partition, " \
	"but monitor didn't do failover"

#define COMMENT_WAIT_STANDBY_TO_CATCHINGUP \
	"The primary is now ready to accept a standby"

#define COMMENT_DEMOTED_TO_CATCHINGUP \
	"A new primary is available. " \
	"First, try to rewind. If that fails, do a pg_basebackup."

#define COMMENT_SECONDARY_TO_CATCHINGUP \
	"Failed to report back to the monitor, " \
	"not eligible for promotion"

#define COMMENT_CATCHINGUP_TO_SECONDARY \
	"Convinced the monitor that I'm up and running, " \
	"and eligible for promotion again"

#define COMMENT_SECONDARY_TO_PREP_PROMOTION \
	"Stop traffic to primary, " \
	"wait for it to finish draining."

#define COMMENT_PROMOTION_TO_STOP_REPLICATION \
	"Prevent against split-brain situations."

#define COMMENT_INIT_TO_WAIT_STANDBY \
	"Start following a primary"

#define COMMENT_SECONDARY_TO_MAINTENANCE \
	"Suspending standby for manual maintenance."

#define COMMENT_MAINTENANCE_TO_CATCHINGUP \
	"Restarting standby after manual maintenance is done."

#define COMMENT_BLOCKED_WRITES \
	"Promoting a Citus Worker standby after having blocked writes " \
	"from the coordinator."

#define COMMENT_PRIMARY_TO_APPLY_SETTINGS \
	"Apply new pg_auto_failover settings (synchronous_standby_names)"

# define COMMENT_APPLY_SETTINGS_TO_PRIMARY \
	"Back to primary state after having applied new pg_auto_failover settings"

#define COMMENT_SECONDARY_TO_REPORT_LSN \
	"Reporting the last write-ahead log location received"

#define COMMENT_REPORT_LSN_TO_PREP_PROMOTION \
	"Stop traffic to primary, " \
	"wait for it to finish draining."

#define COMMENT_REPORT_LSN_TO_WAIT_FORWARD \
	"Preparing to fetch missing WAL bits from another standby before promotion"

#define COMMENT_REPORT_LSN_TO_FAST_FORWARD \
	"Fetching missing WAL bits from another standby before promotion"

#define COMMENT_REPORT_LSN_TO_WAIT_CASCADE \
	"Prepare replication for the failover candidate"

#define COMMENT_FOLLOW_NEW_PRIMARY \
	"Switch replication to the new primary"

#define COMMENT_WAIT_CASCADE_TO_CATCHINGUP \
	"Switch replication to the new primary"

/* *INDENT-OFF* */

/*
 * The full 2-nodes state machine contains states that are expected only when
 * the node is a primary, and some only when the node is a standby. Each node
 * is going to change role in its life-cycle, so having the whole life-cycle in
 * a single FSM makes sense.
 *
 * The FSM is normally driven by an external node, the monitor. See design
 * docs.
 */
KeeperFSMTransition KeeperFSM[] = {
	/*
	 * CURRENT_STATE,   ASSIGNED_STATE,  COMMENT,  TRANSTION_FUNCTION
	 */

	/*
	 * Started as a single, no nothing
	 */
	{ INIT_STATE, SINGLE_STATE, COMMENT_INIT_TO_SINGLE, &fsm_init_primary },


	/*
	 * The previous implementation has a transition from any state to the INIT
	 * state that ensures PostgreSQL is down, but I can't quite figure out what
	 * role the INIT state plays exactly in there.
	 *
	 * {ANY_STATE, INIT_STATE, "Revert to initial state", &fsm_stop_postgres},
	 */

	/*
	 * other node(s) was forcibly removed, now single
	 */
	{ PRIMARY_STATE, SINGLE_STATE, COMMENT_PRIMARY_TO_SINGLE, &fsm_disable_replication },
	{ WAIT_PRIMARY_STATE, SINGLE_STATE, COMMENT_PRIMARY_TO_SINGLE, &fsm_disable_replication },
	{ JOIN_PRIMARY_STATE, SINGLE_STATE, COMMENT_PRIMARY_TO_SINGLE, &fsm_disable_replication },

	/*
	 * failover occurred, primary -> draining/demoted
	 */
	{ PRIMARY_STATE, DRAINING_STATE, COMMENT_PRIMARY_TO_DRAINING, &fsm_stop_postgres },
	{ DRAINING_STATE, DEMOTED_STATE, COMMENT_DRAINING_TO_DEMOTED, &fsm_stop_postgres },
	{ PRIMARY_STATE, DEMOTED_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },
	{ PRIMARY_STATE, DEMOTE_TIMEOUT_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },

	{ JOIN_PRIMARY_STATE, DRAINING_STATE, COMMENT_PRIMARY_TO_DRAINING, &fsm_stop_postgres },
	{ JOIN_PRIMARY_STATE, DEMOTED_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },
	{ JOIN_PRIMARY_STATE, DEMOTE_TIMEOUT_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },

	{ APPLY_SETTINGS_STATE, DRAINING_STATE, COMMENT_PRIMARY_TO_DRAINING, &fsm_stop_postgres },
	{ APPLY_SETTINGS_STATE, DEMOTED_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },
	{ APPLY_SETTINGS_STATE, DEMOTE_TIMEOUT_STATE, COMMENT_PRIMARY_TO_DEMOTED, &fsm_stop_postgres },	

	/*
	 * was demoted, need to be dead now.
	 */
	{ DRAINING_STATE, DEMOTE_TIMEOUT_STATE, COMMENT_DRAINING_TO_DEMOTE_TIMEOUT, &fsm_stop_postgres },
	{ DEMOTE_TIMEOUT_STATE, DEMOTED_STATE, COMMENT_DEMOTE_TIMEOUT_TO_DEMOTED,  &fsm_stop_postgres},

	/*
	 * was demoted after a failure, but standby was forcibly removed
	 */
	{ DEMOTED_STATE, SINGLE_STATE, COMMENT_DEMOTED_TO_SINGLE, &fsm_resume_as_primary },
	{ DEMOTE_TIMEOUT_STATE, SINGLE_STATE, COMMENT_DEMOTED_TO_SINGLE, &fsm_resume_as_primary },
	{ DRAINING_STATE, SINGLE_STATE, COMMENT_DEMOTED_TO_SINGLE, &fsm_resume_as_primary },

	/*
	 * primary was forcibly removed
	 */
	{ SECONDARY_STATE, SINGLE_STATE, COMMENT_LOST_PRIMARY, &fsm_promote_standby },
	{ CATCHINGUP_STATE, SINGLE_STATE, COMMENT_LOST_PRIMARY, &fsm_promote_standby },
	{ PREP_PROMOTION_STATE, SINGLE_STATE, COMMENT_LOST_PRIMARY, &fsm_promote_standby },

	/*
	 * went down to force the primary to time out, but then it was removed
	 */
	{ STOP_REPLICATION_STATE, SINGLE_STATE, COMMENT_REPLICATION_TO_SINGLE, &fsm_promote_standby },

	/*
	 * On the Primary, wait for a standby to be ready: WAIT_PRIMARY
	 */
	{ SINGLE_STATE, WAIT_PRIMARY_STATE, COMMENT_SINGLE_TO_WAIT_PRIMARY, &fsm_prepare_replication },
	{ PRIMARY_STATE, JOIN_PRIMARY_STATE, COMMENT_PRIMARY_TO_JOIN_PRIMARY, &fsm_prepare_replication },
	{ PRIMARY_STATE, WAIT_PRIMARY_STATE, COMMENT_PRIMARY_TO_WAIT_PRIMARY, &fsm_disable_sync_rep },
	{ STOP_REPLICATION_STATE, WAIT_PRIMARY_STATE, COMMENT_STOP_REPLICATION_TO_WAIT_PRIMARY, &fsm_promote_standby_to_primary },

	/*
	 * Situation is getting back to normal on the primary
	 */
	{ WAIT_PRIMARY_STATE, PRIMARY_STATE, COMMENT_WAIT_PRIMARY_TO_PRIMARY, &fsm_enable_sync_rep },
	{ JOIN_PRIMARY_STATE, PRIMARY_STATE, COMMENT_JOIN_PRIMARY_TO_PRIMARY, &fsm_enable_sync_rep },
	{ DEMOTE_TIMEOUT_STATE, PRIMARY_STATE, COMMENT_DEMOTE_TO_PRIMARY, &fsm_start_postgres },

	/*
	 * The primary is now ready to accept a standby, we're the standby
	 */
	{ WAIT_STANDBY_STATE, CATCHINGUP_STATE, COMMENT_WAIT_STANDBY_TO_CATCHINGUP, &fsm_init_standby },
	{ DEMOTED_STATE, CATCHINGUP_STATE, COMMENT_DEMOTED_TO_CATCHINGUP, &fsm_rewind_or_init },
	{ SECONDARY_STATE, CATCHINGUP_STATE, COMMENT_SECONDARY_TO_CATCHINGUP, NULL },

	/*
	 * We're asked to be a standby.
	 */
	{ CATCHINGUP_STATE, SECONDARY_STATE, COMMENT_CATCHINGUP_TO_SECONDARY, NULL },

	/*
	 * The standby is asked to prepare its own promotion
	 */
	{ SECONDARY_STATE, PREP_PROMOTION_STATE, COMMENT_SECONDARY_TO_PREP_PROMOTION, &fsm_prepare_standby_for_promotion },
	{ CATCHINGUP_STATE, PREP_PROMOTION_STATE, COMMENT_SECONDARY_TO_PREP_PROMOTION, &fsm_prepare_standby_for_promotion },

	/*
	 * Forcefully stop replication by stopping the server.
	 */
	{ PREP_PROMOTION_STATE, STOP_REPLICATION_STATE, COMMENT_PROMOTION_TO_STOP_REPLICATION, &fsm_stop_replication },

	/*
	 * finish the promotion
	 */
	{ PREP_PROMOTION_STATE, WAIT_PRIMARY_STATE, COMMENT_BLOCKED_WRITES, &fsm_promote_standby },

	/*
	 * Just wait until primary is ready
	 */
	{ INIT_STATE, WAIT_STANDBY_STATE, COMMENT_INIT_TO_WAIT_STANDBY, NULL },

	/*
	 * In case of maintenance of the standby server, we stop PostgreSQL.
	 */
	{ SECONDARY_STATE, MAINTENANCE_STATE, COMMENT_SECONDARY_TO_MAINTENANCE, &fsm_start_maintenance_on_standby },
	{ CATCHINGUP_STATE, MAINTENANCE_STATE, COMMENT_SECONDARY_TO_MAINTENANCE, &fsm_start_maintenance_on_standby },
	{ MAINTENANCE_STATE, CATCHINGUP_STATE, COMMENT_MAINTENANCE_TO_CATCHINGUP, &fsm_restart_standby },

	/*
	 * Applying new replication/cluster settings (per node replication quorum,
	 * candidate priorities, or per formation number_sync_standbys) means we
	 * have to fetch the new value for synchronous_standby_names from the
	 * monitor.
	 */
	{ PRIMARY_STATE, APPLY_SETTINGS_STATE, COMMENT_PRIMARY_TO_APPLY_SETTINGS, &fsm_apply_settings },
	{ APPLY_SETTINGS_STATE, PRIMARY_STATE, COMMENT_APPLY_SETTINGS_TO_PRIMARY, NULL },
	/*
	 * In case of multiple standbys, failover begins with reporting current LSN
	 */
	{ SECONDARY_STATE, REPORT_LSN_STATE, COMMENT_SECONDARY_TO_REPORT_LSN, &fsm_report_lsn },
	{ REPORT_LSN_STATE, PREP_PROMOTION_STATE, COMMENT_REPORT_LSN_TO_PREP_PROMOTION, &fsm_prepare_standby_for_promotion },
	{ REPORT_LSN_STATE, WAIT_FORWARD_STATE, COMMENT_REPORT_LSN_TO_WAIT_FORWARD, &fsm_wait_forward },
	{ WAIT_FORWARD_STATE, FAST_FORWARD_STATE, COMMENT_REPORT_LSN_TO_FAST_FORWARD, &fsm_fast_forward },
	{ REPORT_LSN_STATE, WAIT_CASCADE_STATE, COMMENT_REPORT_LSN_TO_WAIT_CASCADE, &fsm_prepare_cascade },
	{ REPORT_LSN_STATE, SECONDARY_STATE, COMMENT_FOLLOW_NEW_PRIMARY, &fsm_follow_new_primary },
	{ WAIT_CASCADE_STATE, SECONDARY_STATE, COMMENT_FOLLOW_NEW_PRIMARY, &fsm_follow_new_primary },

	/*
	 * This is the end, my friend.
	 */
	{ NO_STATE, NO_STATE, NULL, NULL },
};


/* *INDENT-ON* */


/*
 * keeper_fsm_step implements the logic to perform a single step
 * of the state machine according to the goal state returned by
 * the monitor.
 */
bool
keeper_fsm_step(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);
	KeeperStateData *keeperState = &(keeper->state);
	Monitor *monitor = &(keeper->monitor);
	LocalPostgresServer *postgres = &(keeper->postgres);
	MonitorAssignedState assignedState = { 0 };

	/*
	 * Update our in-memory representation of PostgreSQL state, ignore errors
	 * as in the main loop: we continue with default WAL lag of -1 and an empty
	 * string for pgsrSyncState.
	 */
	if (!keeper_update_pg_state(keeper))
	{
		log_error("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above for details.");
		return false;
	}

	log_info("Calling node_active for node %s/%d/%d with current state: "
			 "PostgreSQL is running is %s, "
			 "sync_state is \"%s\", "
			 "latest WAL LSN is %s.",
			 config->formation,
			 keeperState->current_node_id,
			 keeperState->current_group,
			 postgres->pgIsRunning ? "true" : "false",
			 postgres->pgsrSyncState,
			 postgres->currentLSN);

	if (!monitor_node_active(monitor,
							 config->formation,
							 config->nodename,
							 config->pgSetup.pgport,
							 keeperState->current_node_id,
							 keeperState->current_group,
							 keeperState->current_role,
							 postgres->pgIsRunning,
							 postgres->currentLSN,
							 postgres->pgsrSyncState,
							 &assignedState))
	{
		log_fatal("Failed to get the goal state from the monitor, "
				  "see above for details");
		return false;
	}

	/*
	 * Assign the new state. We skip writing the state file here since we can
	 * (and should) always get the assigned state from the monitor.
	 */
	keeperState->assigned_role = assignedState.state;

	/* roll the state machine forward */
	if (keeperState->assigned_role != keeperState->current_role)
	{
		if (!keeper_fsm_reach_assigned_state(keeper))
		{
			/* errors have already been logged */
			return false;
		}
	}
	else
	{
		/*
		 * Now that we know if PostgreSQL is running or not, maybe restart it,
		 * or maybe shut it down, depending on what the current state expects.
		 */
		if (!keeper_ensure_current_state(keeper))
		{
			log_warn("pg_autoctl keeper failed to ensure current state \"%s\": "
					 "PostgreSQL %s running",
					 NodeStateToString(keeperState->current_role),
					 postgres->pgIsRunning ? "is" : "is not");
		}
	}

	/* update state file */
	if (!keeper_update_state(keeper, assignedState.nodeId, assignedState.groupId,
							 assignedState.state, true))
	{
		log_error("Failed to write keepers state file, see above for details");
		return false;
	}

	return true;
}


/*
 * keeper_fsm_reach_assigned_state uses the KeeperFSM to drive a transition
 * from keeper->state->current_role to keeper->state->assigned_role, when
 * that's supported.
 */
bool
keeper_fsm_reach_assigned_state(Keeper *keeper)
{
	int transitionIndex = 0;
	KeeperStateData *keeperState = &(keeper->state);
	KeeperFSMTransition transition = KeeperFSM[0];

	if (keeperState->current_role == keeperState->assigned_role)
	{
		log_debug("Current state and Goal state are the same (\"%s\").",
				  NodeStateToString(keeperState->current_role));

		return true;
	}

	while (transition.current != NO_STATE)
	{
		if (state_matches(transition.current, keeperState->current_role) &&
			state_matches(transition.assigned, keeperState->assigned_role))
		{
			bool ret = false;

			log_info("FSM transition from \"%s\" to \"%s\"%s%s",
					 NodeStateToString(transition.current),
					 NodeStateToString(transition.assigned),
					 transition.comment ? ": " : "",
					 transition.comment ? transition.comment : "");

			if (transition.transitionFunction)
			{
				ret = (*transition.transitionFunction)(keeper);

				log_debug("Transition function returned: %s",
						  ret ? "true" : "false");
			}
			else
			{
				ret = true;
				log_debug("No transition function, assigning new state");
			}

			if (ret)
			{
				keeperState->current_role = keeperState->assigned_role;

				log_info("Transition complete: current state is now \"%s\"",
						 NodeStateToString(keeperState->current_role));
			}
			else
			{
				log_error("Failed to transition from state \"%s\" "
						  "to state \"%s\", see above.",
						  NodeStateToString(transition.current),
						  NodeStateToString(transition.assigned));
			}

			return ret;
		}
		transition = KeeperFSM[++transitionIndex];
	}

	/*
	 * we didn't find a transition
	 */
	log_fatal("pg_autoctl does not know how to reach state \"%s\" from \"%s\"",
			  NodeStateToString(keeperState->assigned_role),
			  NodeStateToString(keeperState->current_role));

	return false;
}


/*
 * print_reachable_states shows the list of states we can reach using the FSM
 * transitions from KeeperState.current_role.
 */
void
print_reachable_states(KeeperStateData *keeperState)
{
	int transitionIndex = 0;
	bool header = false;
	KeeperFSMTransition transition = KeeperFSM[0];

	log_debug("print_reachable_states: %s",
			  NodeStateToString(keeperState->current_role));

	while (transition.current != NO_STATE)
	{
		if (state_matches(transition.current, keeperState->current_role))
		{
			if (!header)
			{
				fprintf(stdout,
						"%20s | %20s | %s\n",
						"Current", "Reachable", "Comment");
				fprintf(stdout,
						"%20s-+-%20s-+-%s\n",
						"--------------------",
						"--------------------",
						"--------------------");
				header = true;
			}
			fprintf(stdout,
					"%20s | %20s | %s\n",
					NodeStateToString(transition.current),
					NodeStateToString(transition.assigned),
					transition.comment);
		}
		transition = KeeperFSM[++transitionIndex];
	}
}


/*
 * print_fsm_for_graphviz outputs the program used by graphviz to draw a visual
 * representation of our state machine.
 *
 *   pg_autoctl do fsm gv | dot -Tpng > fsm.png
 */
void
print_fsm_for_graphviz()
{
	KeeperFSMTransition transition = KeeperFSM[0];
	int transitionIndex = 0;

	fprintf(stdout,
			"digraph finite_state_machine\n"
			"{\n"
			"    size=\"12\"\n"
			"    ratio=\"fill\"\n"
			"    node [shape = doubleoctagon, style=filled, color=\"bisque1\"]; init primary secondary; \n"
			"    node [shape = octagon, style=filled color=\"bisque3\"]; \n");

	while (transition.current != NO_STATE)
	{
		fprintf(stdout,
				"    %s -> %s [ label = \"%s\" ];\n",
				NodeStateToString(transition.current),
				NodeStateToString(transition.assigned),
				transition.comment);

		transition = KeeperFSM[++transitionIndex];
	}
	fprintf(stdout, "}\n");
}
