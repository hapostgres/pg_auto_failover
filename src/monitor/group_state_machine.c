/*-------------------------------------------------------------------------
 *
 * src/monitor/group_state_machine.c
 *
 * Implementation of the state machine for fail-over within a group of
 * PostgreSQL nodes.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"

#include "formation_metadata.h"
#include "group_state_machine.h"
#include "node_metadata.h"
#include "notifications.h"
#include "replication_state.h"
#include "version_compat.h"

#include "access/htup_details.h"
#include "catalog/pg_enum.h"
#include "commands/trigger.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"


/* private function forward declarations */
static void AssignGoalState(AutoFailoverNode *pgAutoFailoverNode,
							ReplicationState state, char *description);
static bool IsDrainTimeExpired(AutoFailoverNode *pgAutoFailoverNode);
static bool WalDifferenceWithin(AutoFailoverNode *secondaryNode,
								AutoFailoverNode *primaryNode,
								int64 delta);
static bool IsHealthy(AutoFailoverNode *pgAutoFailoverNode);
static bool IsUnhealthy(AutoFailoverNode *pgAutoFailoverNode);

/* GUC variables */
int EnableSyncXlogThreshold = DEFAULT_XLOG_SEG_SIZE;
int PromoteXlogThreshold = DEFAULT_XLOG_SEG_SIZE;
int DrainTimeoutMs = 30 * 1000;
int UnhealthyTimeoutMs = 20 * 1000;
int StartupGracePeriodMs = 10 * 1000;


/*
 * ProceedGroupState proceeds the state machines of the group of which
 * the given node is part.
 */
bool
ProceedGroupState(AutoFailoverNode *activeNode)
{
	AutoFailoverNode *otherNode = NULL;
	AutoFailoverFormation *formation = GetFormation(activeNode->formationId);

	otherNode = OtherNodeInGroup(activeNode);

	if (otherNode == NULL
		&& !IsCurrentState(activeNode, REPLICATION_STATE_SINGLE))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to single as there is no other "
			"node.",
			activeNode->nodeName, activeNode->nodePort);

		/* other node may have been removed */
		AssignGoalState(activeNode, REPLICATION_STATE_SINGLE, message);

		return true;
	}

	/* single -> wait_primary when another node wants to become standby */
	if (IsCurrentState(activeNode, REPLICATION_STATE_SINGLE) &&
		IsCurrentState(otherNode, REPLICATION_STATE_WAIT_STANDBY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary after %s:%d "
			"joined.", activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* prepare replication slot and pg_hba.conf */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		return true;
	}

	/* prepare_standby -> catchingup when other node is ready for replication */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_STANDBY) &&
		IsCurrentState(otherNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to catchingup after %s:%d "
			"converged to wait_primary.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/* catchingup -> secondary + wait_primary -> primary when secondary caught up */
	if (IsCurrentState(activeNode, REPLICATION_STATE_CATCHINGUP) &&
		IsCurrentState(otherNode, REPLICATION_STATE_WAIT_PRIMARY) &&
		IsHealthy(activeNode) &&
		WalDifferenceWithin(activeNode, otherNode, EnableSyncXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to primary and %s:%d to "
			"secondary after %s:%d caught up.",
			otherNode->nodeName, otherNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeName, activeNode->nodePort);

		/* node is ready for promotion */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		/* other node can enable synchronous commit */
		AssignGoalState(otherNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/* secondary -> catchingup + primary -> wait_primary when secondary unhealthy */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PRIMARY) &&
		IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY) &&
		IsUnhealthy(otherNode))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"catchingup after %s:%d became unhealthy.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* disable synchronous replication to maintain availability */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* other node is behind, no longer eligible for promotion */
		AssignGoalState(otherNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}


	/* secondary -> prepare_promotion + primary -> draining when primary fails */
	if (IsCurrentState(activeNode, REPLICATION_STATE_SECONDARY) &&
		IsCurrentState(otherNode, REPLICATION_STATE_PRIMARY) &&
		IsUnhealthy(otherNode) && IsHealthy(activeNode) &&
		WalDifferenceWithin(activeNode, otherNode, PromoteXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to draining and %s:%d to "
			"prepare_promotion after %s:%d became unhealthy.",
			otherNode->nodeName, otherNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* keep reading until no more records are available */
		AssignGoalState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION, message);

		/* shut down the primary */
		AssignGoalState(otherNode, REPLICATION_STATE_DRAINING, message);

		return true;
	}

	/* prepare_promotion -> wait_primary when a worker blocked writes */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the coordinator metadata was updated.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(otherNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/* prepare_promotion -> stop_replication when node is seeing no more writes */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to demote_timeout and %s:%d to "
			"stop_replication after %s:%d converged to "
			"prepare_promotion.",
			otherNode->nodeName, otherNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeName, activeNode->nodePort);

		/* perform promotion to stop replication */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		/* wait for possibly-alive primary to kill itself */
		AssignGoalState(otherNode, REPLICATION_STATE_DEMOTE_TIMEOUT, message);

		return true;
	}

	/* draining -> demoted when drain time expires or primary reports it's drained */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		(IsCurrentState(otherNode, REPLICATION_STATE_DEMOTE_TIMEOUT) ||
		 IsDrainTimeExpired(otherNode)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the demote timeout expired.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(otherNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/* stop_replication -> wait_primary when a worker blocked writes */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the coordinator metadata was updated.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(otherNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/* demoted -> catchingup when a new primary is ready */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsCurrentState(otherNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to catchingup after it "
			"converged to demotion and %s:%d converged to wait_primary.",
			activeNode->nodeName, activeNode->nodePort,
			otherNode->nodeName, otherNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	return false;
}


/*
 * AssignGoalState assigns a new goal state to a AutoFailover node.
 */
static void
AssignGoalState(AutoFailoverNode *pgAutoFailoverNode,
				ReplicationState state, char *description)
{
	if (pgAutoFailoverNode != NULL)
	{
		pgAutoFailoverNode->goalState = state;

		SetNodeGoalState(pgAutoFailoverNode->nodeName,
						 pgAutoFailoverNode->nodePort, state);

		NotifyStateChange(pgAutoFailoverNode->reportedState,
						  state,
						  pgAutoFailoverNode->formationId,
						  pgAutoFailoverNode->groupId,
						  pgAutoFailoverNode->nodeId,
						  pgAutoFailoverNode->nodeName,
						  pgAutoFailoverNode->nodePort,
						  pgAutoFailoverNode->pgsrSyncState,
						  pgAutoFailoverNode->reportedLSN,
						  pgAutoFailoverNode->candidatePriority,
						  pgAutoFailoverNode->replicationQuorum,
						  description);
	}
}


/*
 * WalDifferenceWithin returns whether the most recently reported relative log
 * position of the given nodes is within the specified bound. Returns false if
 * neither node has reported a relative xlog position
 */
static bool
WalDifferenceWithin(AutoFailoverNode *secondaryNode,
					AutoFailoverNode *otherNode, int64 delta)
{
	int64 walDifference = 0;
	XLogRecPtr secondaryLsn = 0;
	XLogRecPtr otherNodeLsn = 0;


	if (secondaryNode == NULL || otherNode == NULL)
	{
		return true;
	}

	secondaryLsn = secondaryNode->reportedLSN;
	otherNodeLsn = otherNode->reportedLSN;

	if (secondaryLsn == 0 || otherNodeLsn == 0)
	{
		/* we don't have any data yet */
		return false;
	}

	walDifference = Abs(otherNodeLsn - secondaryLsn);

	return walDifference <= delta;
}


/*
 * IsHealthy returns whether the given node is heathly, meaning it succeeds the
 * last health check and its PostgreSQL instance is reported as running by the
 * keeper.
 */
static bool
IsHealthy(AutoFailoverNode *pgAutoFailoverNode)
{
	if (pgAutoFailoverNode == NULL)
	{
		return false;
	}

	return pgAutoFailoverNode->health == NODE_HEALTH_GOOD
		&& pgAutoFailoverNode->pgIsRunning == true;
}

/*
 * IsUnhealthy returns whether the given node is unhealthy, meaning it failed
 * its last health check and has not reported for more than UnhealthyTimeoutMs,
 * and it's PostgreSQL instance has been reporting as running by the keeper.
 */
static bool
IsUnhealthy(AutoFailoverNode *pgAutoFailoverNode)
{
	TimestampTz now = GetCurrentTimestamp();

	if (pgAutoFailoverNode == NULL)
	{
		return true;
	}

	/* if the keeper isn't reporting, trust our Health Checks */
	if (TimestampDifferenceExceeds(pgAutoFailoverNode->reportTime,
								   now,
								   UnhealthyTimeoutMs))
	{
		if (pgAutoFailoverNode->health == NODE_HEALTH_BAD &&
			TimestampDifferenceExceeds(PgStartTime,
									   pgAutoFailoverNode->healthCheckTime,
									   0))
		{
			if (TimestampDifferenceExceeds(PgStartTime,
										   now,
										   StartupGracePeriodMs))
			{
				return true;
			}
		}
	}

	/*
	 * If the keeper reports that PostgreSQL is not running, then the node
	 * isn't Healthy.
	 */
	if (!pgAutoFailoverNode->pgIsRunning)
	{
		return true;
	}

	/* clues show that everything is fine, the node is not unhealthy */
	return false;
}


/*
 * IsDrainTimeExpired returns whether the node should be done according
 * to the drain time-outs.
 */
static bool
IsDrainTimeExpired(AutoFailoverNode *pgAutoFailoverNode)
{
	bool drainTimeExpired = false;
	TimestampTz now = 0;

	if (pgAutoFailoverNode == NULL
		|| pgAutoFailoverNode->goalState != REPLICATION_STATE_DEMOTE_TIMEOUT)
	{
		return false;
	}

	now = GetCurrentTimestamp();
	if (TimestampDifferenceExceeds(pgAutoFailoverNode->stateChangeTime,
								   now,
								   DrainTimeoutMs))
	{
		drainTimeExpired = true;
	}

	return drainTimeExpired;
}
