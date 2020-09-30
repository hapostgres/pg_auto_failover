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


/*
 * To communicate with the BuildCandidateList function, it's easier to handle a
 * structure with those bits of information to share:
 */
typedef struct CandidateList
{
	List *candidateNodesGroupList;
	List *mostAdvancedNodesGroupList;
	XLogRecPtr mostAdvancedReportedLSN;
	int candidateCount;
	int missingNodesCount;
} CandidateList;


/* private function forward declarations */
static bool ProceedGroupStateForPrimaryNode(AutoFailoverNode *primaryNode);
static bool ProceedGroupStateForMSFailover(AutoFailoverNode *activeNode,
										   AutoFailoverNode *primaryNode);
static bool ProceedWithMSFailover(AutoFailoverNode *activeNode,
								  AutoFailoverNode *candidateNode);

static bool BuildCandidateList(List *standbyNodesGroupList,
							   CandidateList *candidateList);

static AutoFailoverNode * SelectFailoverCandidateNode(CandidateList *candidateList,
													  AutoFailoverNode *primaryNode);

static bool PromoteSelectedNode(AutoFailoverNode *selectedNode,
								AutoFailoverNode *primaryNode,
								CandidateList *candidateList);

static void AssignGoalState(AutoFailoverNode *pgAutoFailoverNode,
							ReplicationState state, char *description);
static bool IsDrainTimeExpired(AutoFailoverNode *pgAutoFailoverNode);
static bool WalDifferenceWithin(AutoFailoverNode *secondaryNode,
								AutoFailoverNode *primaryNode,
								int64 delta);
static bool IsHealthy(AutoFailoverNode *pgAutoFailoverNode);
static bool IsUnhealthy(AutoFailoverNode *pgAutoFailoverNode);
static bool IsReporting(AutoFailoverNode *pgAutoFailoverNode);

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
	char *formationId = activeNode->formationId;
	int groupId = activeNode->groupId;

	AutoFailoverFormation *formation = GetFormation(formationId);
	AutoFailoverNode *primaryNode = NULL;

	List *nodesGroupList = AutoFailoverNodeGroup(formationId, groupId);
	int nodesCount = list_length(nodesGroupList);

	if (formation == NULL)
	{
		ereport(ERROR,
				(errmsg("Formation for %s could not be found",
						activeNode->formationId)));
	}

	/* when there's no other node anymore, not even one */
	if (nodesCount == 1 &&
		!IsCurrentState(activeNode, REPLICATION_STATE_SINGLE))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to single "
			"as there is no other node.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort);

		/* other node may have been removed */
		AssignGoalState(activeNode, REPLICATION_STATE_SINGLE, message);

		return true;
	}

	/*
	 * We separate out the FSM for the primary server, because that one needs
	 * to loop over every other node to take decisions. That induces some
	 * complexity that is best managed in a specialized function.
	 */
	if (IsInPrimaryState(activeNode))
	{
		return ProceedGroupStateForPrimaryNode(activeNode);
	}

	primaryNode = GetPrimaryOrDemotedNodeInGroup(formationId, groupId);

	/*
	 * We want to have a primaryNode around for most operations, but also need
	 * to support the case that the primaryNode has been dropped manually by a
	 * call to remove_node(). So we have two main cases to think about here:
	 *
	 * - we have two nodes, one of them has been removed, we catch that earlier
	 *   in this function and assign the remaining one with the SINGLE state,
	 *
	 * - we have more than two nodes in total, and the primary has just been
	 *   removed (maybe it was still marked unhealthy and the operator knows it
	 *   won't ever come back so called remove_node() already): in that case in
	 *   remove_node() we set all the other nodes to REPORT_LSN (unless they
	 *   are in MAINTENANCE), and we should be able to make progress with the
	 *   failover without a primary around.
	 *
	 * In all other cases we require a primaryNode to be identified.
	 */
	if (primaryNode == NULL && !IsFailoverInProgress(nodesGroupList))
	{
		ereport(ERROR,
				(errmsg("ProceedGroupState couldn't find the primary node "
						"in formation \"%s\", group %d",
						formationId, groupId),
				 errdetail("activeNode is %s:%d in state %s",
						   activeNode->nodeHost, activeNode->nodePort,
						   ReplicationStateGetName(activeNode->goalState))));
	}

	/* Multiple Standby failover is handled in its own function. */
	if (nodesCount > 2 && IsUnhealthy(primaryNode))
	{
		/* stop replication from the primary and proceed with replacement */
		if (IsInPrimaryState(primaryNode))
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to draining "
				"after it became unhealthy.",
				primaryNode->nodeId,
				primaryNode->nodeHost,
				primaryNode->nodePort);

			AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);
		}

		/*
		 * ProceedGroupStateForMSFailover chooses the failover candidate when
		 * there's more than one standby node around, by applying the
		 * candidatePriority and comparing the reportedLSN. The function also
		 * orchestrate fetching the missing WAL from the failover candidate if
		 * that's needed.
		 *
		 * When ProceedGroupStateForMSFailover returns true, it means it was
		 * successfull in driving the failover to the next step, and we should
		 * stop here. When it return false, it did nothing, and so we want to
		 * apply the common orchestration code for a failover.
		 */
		if (ProceedGroupStateForMSFailover(activeNode, primaryNode))
		{
			return true;
		}
	}

	/*
	 * when report_lsn and the promotion has been done already:
	 *      report_lsn -> secondary
	 *
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"after node %d (%s:%d) got selected as the failover candidate.",
			activeNode->nodeId,
			activeNode->nodeHost,
			activeNode->nodePort,
			primaryNode->nodeId,
			primaryNode->nodeHost,
			primaryNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);
		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/*
	 * when report_lsn and the promotion has been done already:
	 *      report_lsn -> secondary
	 *
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"after node %d (%s:%d) got selected as the failover candidate.",
			activeNode->nodeId,
			activeNode->nodeHost,
			activeNode->nodePort,
			primaryNode->nodeId,
			primaryNode->nodeHost,
			primaryNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		return true;
	}

	/*
	 * When the candidate is done fast forwarding the locally missing WAL bits,
	 * it can be promoted.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_FAST_FORWARD))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to prepare_promotion",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION, message);

		return true;
	}

	/*
	 * There are other cases when we want to continue an already started
	 * failover.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) ||
		IsCurrentState(activeNode, REPLICATION_STATE_FAST_FORWARD))
	{
		return ProceedGroupStateForMSFailover(activeNode, primaryNode);
	}

	/*
	 * when primary node is ready for replication:
	 *  prepare_standby -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_STANDBY) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to catchingup "
			"after node %d (%s:%d) converged to wait_primary.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when secondary caught up:
	 *      catchingup -> secondary
	 *  + wait_primary -> primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_CATCHINGUP) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY)) &&
		IsHealthy(activeNode) &&
		WalDifferenceWithin(activeNode, primaryNode, EnableSyncXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to primary and "
			"node %d (%s:%d) to secondary after node %d (%s:%d) caught up.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort);

		/* node is ready for promotion */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		/* other node can enable synchronous commit */
		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/*
	 * when primary fails:
	 *   secondary -> prepare_promotion
	 * +   primary -> draining
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_SECONDARY) &&
		IsInPrimaryState(primaryNode) &&
		IsUnhealthy(primaryNode) && IsHealthy(activeNode) &&
		WalDifferenceWithin(activeNode, primaryNode, PromoteXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to draining "
			"and node %d (%s:%d) to prepare_promotion "
			"after node %d (%s:%d) became unhealthy.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* keep reading until no more records are available */
		AssignGoalState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION, message);

		/* shut down the primary */
		AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);

		return true;
	}

	/*
	 * when secondary is put to maintenance and there's no standby left
	 *  wait_maintenance -> maintenance
	 *  wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_MAINTENANCE) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to maintenance "
			"after %s:%d converged to wait_primary.",
			activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeHost, primaryNode->nodePort);

		/* secondary reached maintenance */
		AssignGoalState(activeNode, REPLICATION_STATE_MAINTENANCE, message);

		return true;
	}

	/*
	 * when secondary is put to maintenance and we have more standby nodes
	 *  wait_maintenance -> maintenance
	 *  join_primary -> primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_MAINTENANCE) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to maintenance "
			"after %s:%d converged to wait_primary.",
			activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeHost, primaryNode->nodePort);

		/* secondary reached maintenance */
		AssignGoalState(activeNode, REPLICATION_STATE_MAINTENANCE, message);

		/* set the primary back to its normal state (we can failover still) */
		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/*
	 * when primary is put to maintenance
	 *  prepare_promotion -> stop_replication
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PREPARE_MAINTENANCE))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to stop_replication "
			"after %s:%d converged to prepare_maintenance.",
			activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeHost, primaryNode->nodePort);

		/* promote the secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		return true;
	}

	/*
	 * when a worker blocked writes:
	 *   prepare_promotion -> wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to wait_primary "
			"and node %d (%s:%d) to demoted "
			"after the coordinator metadata was updated.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when node is seeing no more writes:
	 *  prepare_promotion -> stop_replication
	 *
	 * refrain from prepare_maintenance -> demote_timeout on the primary, which
	 * might happen here when secondary has reached prepare_promotion before
	 * primary has reached prepare_maintenance.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		primaryNode &&
		!IsInMaintenance(primaryNode))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to demote_timeout "
			"and node %d (%s:%d) to stop_replication "
			"after node %d (%s:%d) converged to prepare_promotion.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort);

		/* perform promotion to stop replication */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		/* wait for possibly-alive primary to kill itself */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTE_TIMEOUT, message);

		return true;
	}

	/*
	 * when primary node has been removed and we are promoting one standby
	 *  prepare_promotion -> stop_replication
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		primaryNode == NULL)
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of and node %d (%s:%d) to wait_primary "
			"after node %d (%s:%d) converged to prepare_promotion.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort);

		/* perform promotion to stop replication */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		return true;
	}

	/*
	 * when primary node is going to maintenance
	 *  stop_replication -> wait_primary
	 *  prepare_maintenance -> maintenance
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PREPARE_MAINTENANCE))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"maintenance.",
			activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeHost, primaryNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* old primary node is now ready for maintenance operations */
		AssignGoalState(primaryNode, REPLICATION_STATE_MAINTENANCE, message);

		return true;
	}

	/*
	 * when drain time expires or primary reports it's drained:
	 *  draining -> demoted
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_DEMOTE_TIMEOUT) ||
		 IsDrainTimeExpired(primaryNode)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to wait_primary "
			"and node %d (%s:%d) to demoted after the demote timeout expired.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when a worker blocked writes:
	 *   stop_replication -> wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to wait_primary "
			"and %d (%s:%d) to demoted "
			"after the coordinator metadata was updated.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  demoted -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to catchingup after it "
			"converged to demotion and node %d (%s:%d) converged to primary.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);
		AssignGoalState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  demoted -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to catchingup after it "
			"converged to demotion and node %d (%s:%d) converged to wait_primary.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  join_secondary -> secondary
	 *
	 * As there's no action to implement on the new selected primary for that
	 * step, we can make progress as soon as we want to.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_JOIN_SECONDARY) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"and node %d (%s:%d) to primary after it converged to wait_primary.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);
		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/*
	 * when a new secondary re-appears after a failover or at a "random" time
	 * in the FSM cycle, and the wait_primary or join_primary node has already
	 * made progress to primary.
	 *
	 *  join_secondary -> secondary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_JOIN_SECONDARY) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"after node %d (%s:%d) converged to primary.",
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		return true;
	}

	return false;
}


/*
 * Group State Machine when a primary node contacts the monitor.
 */
static bool
ProceedGroupStateForPrimaryNode(AutoFailoverNode *primaryNode)
{
	List *otherNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
	int otherNodesCount = list_length(otherNodesGroupList);

	/*
	 * when a first "other" node wants to become standby:
	 *  single -> wait_primary
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_SINGLE))
	{
		ListCell *nodeCell = NULL;

		foreach(nodeCell, otherNodesGroupList)
		{
			AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

			if (IsCurrentState(otherNode, REPLICATION_STATE_WAIT_STANDBY))
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of %d (%s:%d) to wait_primary "
					"after node %d (%s:%d) joined.",
					primaryNode->nodeId,
					primaryNode->nodeHost,
					primaryNode->nodePort,
					otherNode->nodeId,
					otherNode->nodeHost,
					otherNode->nodePort);

				/* prepare replication slot and pg_hba.conf */
				AssignGoalState(primaryNode,
								REPLICATION_STATE_WAIT_PRIMARY,
								message);

				return true;
			}
		}
	}

	/*
	 * when another node wants to become standby:
	 *  primary -> join_primary
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		ListCell *nodeCell = NULL;

		foreach(nodeCell, otherNodesGroupList)
		{
			AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

			if (IsCurrentState(otherNode, REPLICATION_STATE_WAIT_STANDBY))
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to join_primary "
					"after node %d (%s:%d) joined.",
					primaryNode->nodeId,
					primaryNode->nodeHost,
					primaryNode->nodePort,
					otherNode->nodeId,
					otherNode->nodeHost,
					otherNode->nodePort);

				/* prepare replication slot and pg_hba.conf */
				AssignGoalState(primaryNode,
								REPLICATION_STATE_JOIN_PRIMARY,
								message);

				return true;
			}
		}
	}

	/*
	 * when secondary unhealthy:
	 *   secondary ➜ catchingup
	 *     primary ➜ wait_primary
	 *
	 * We only swith the primary to wait_primary when there's no healthy
	 * secondary anymore. In other cases, there's by definition at least one
	 * candidate for failover.
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		int failoverCandidateCount = otherNodesCount;
		ListCell *nodeCell = NULL;
		AutoFailoverFormation *formation =
			GetFormation(primaryNode->formationId);

		foreach(nodeCell, otherNodesGroupList)
		{
			AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

			if (IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY) &&
				IsUnhealthy(otherNode))
			{
				char message[BUFSIZE];

				--failoverCandidateCount;

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to catchingup "
					"after it became unhealthy.",
					otherNode->nodeId, otherNode->nodeHost, otherNode->nodePort);

				/* other node is behind, no longer eligible for promotion */
				AssignGoalState(otherNode,
								REPLICATION_STATE_CATCHINGUP, message);
			}
			else if (otherNode->candidatePriority == 0)
			{
				/* also not a candidate */
				--failoverCandidateCount;
			}

			/*
			 * Disable synchronous replication to maintain availability.
			 *
			 * Note that we implement here a trade-off between availability (of
			 * writes) against durability of the written data. In the case when
			 * there's a single standby in the group, pg_auto_failover choice
			 * is to maintain availability of the service, including writes.
			 *
			 * In the case when the user has setup a replication quorum of 2 or
			 * more, then pg_auto_failover does not get in the way. You get
			 * what you ask for, which is a strong guarantee on durability.
			 *
			 * To have number_sync_standbys == 2, you need to have at least 3
			 * standby servers. To get to a point where writes are not possible
			 * anymore, there needs to be a point in time where 2 of the 3
			 * standby nodes are unavailable. In that case, pg_auto_failover
			 * does not change the configured trade-offs. Writes are blocked
			 * until one of the two defective standby nodes is available again.
			 */
			if (formation->number_sync_standbys == 0 &&
				failoverCandidateCount == 0)
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to wait_primary "
					"now that none of the standbys are healthy anymore.",
					primaryNode->nodeId,
					primaryNode->nodeHost,
					primaryNode->nodePort);

				AssignGoalState(primaryNode,
								REPLICATION_STATE_WAIT_PRIMARY, message);
			}
		}

		return true;
	}

	/*
	 * when a node has changed its replication settings:
	 *     apply_settings ➜ primary
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to primary "
			"after it applied replication properties change.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	/*
	 * when a secondary node has been removed during registration, or when
	 * there's no visible reason to not be a primary rather than either
	 * wait_primary or join_primary
	 *
	 *    join_primary ➜ primary
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY))
	{
		ListCell *nodeCell = NULL;
		bool allSecondariesAreHealthy = true;

		foreach(nodeCell, otherNodesGroupList)
		{
			AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

			allSecondariesAreHealthy =
				allSecondariesAreHealthy &&
				IsCurrentState(otherNode,
							   REPLICATION_STATE_SECONDARY) &&
				IsHealthy(otherNode);

			if (!allSecondariesAreHealthy)
			{
				break;
			}
		}

		if (allSecondariesAreHealthy)
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d \"%s\" (%s:%d) back to primary",
				primaryNode->nodeId,
				primaryNode->nodeName,
				primaryNode->nodeHost,
				primaryNode->nodePort);

			AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

			return true;
		}
	}
	return false;
}


/*
 * ProceedGroupStateForMSFailover implements Group State Machine transition to
 * orchestrate a failover when we have more than one standby.
 *
 * This function is supposed to be called when the following pre-conditions are
 * met:
 *
 *  - the primary node is not healthy
 *  - there's more than one standby node registered in the system
 */
static bool
ProceedGroupStateForMSFailover(AutoFailoverNode *activeNode,
							   AutoFailoverNode *primaryNode)
{
	List *nodesGroupList =
		AutoFailoverNodeGroup(activeNode->formationId, activeNode->groupId);
	CandidateList candidateList = { 0 };

	/*
	 * Done with the single standby code path, now we have several standby
	 * nodes that might all be candidate for failover, or just some of them.
	 *
	 * The first order of business though is to determine if a failover is
	 * currently happening, by looping over all the nodes in case one of them
	 * has already been selected as the failover candidate.
	 */
	AutoFailoverNode *nodeBeingPromoted =
		FindCandidateNodeBeingPromoted(nodesGroupList);

	/*
	 * If a failover is in progress, continue driving it.
	 */
	if (nodeBeingPromoted != NULL)
	{
		elog(LOG, "Found candidate node %d (%s:%d)",
			 nodeBeingPromoted->nodeId,
			 nodeBeingPromoted->nodeHost,
			 nodeBeingPromoted->nodePort);

		return ProceedWithMSFailover(activeNode, nodeBeingPromoted);
	}

	/*
	 * Now, have all our candidates for failover report the most recent LSN
	 * they managed to receive. We build the list of nodes that we consider as
	 * failover candidates into candidateNodesGroupList.
	 *
	 * When every one of the nodes in that list has reported its LSN position,
	 * then we select a node from the just built candidateNodesGroupList to
	 * promote.
	 *
	 * It might well be that in this call to node_active() only a part of the
	 * candidates have reported their LSN position yet. Then we refrain from
	 * selecting any in this round, expecting a future call to node_active() to
	 * be the kicker.
	 *
	 * This design also allows for nodes to concurrently be put to maintenance
	 * or get unhealthy: then the next call to node_active() might build a
	 * different candidateNodesGroupList in which every node has reported their
	 * LSN position, allowing progress to be made.
	 */
	BuildCandidateList(nodesGroupList, &candidateList);

	/*
	 * Time to select a candidate?
	 *
	 * We reach this code when we don't have an healthy primary anymore, it's
	 * been demoted or is draining now. Most probably it's dead.
	 *
	 * Before we enter the selection process, we must have collected the last
	 * received LSN from ALL the standby nodes that are considered as a
	 * candidate (thanks to the FSM transition secondary -> report_lsn), and
	 * now we need to select one of the failover candidates.
	 */
	if (candidateList.missingNodesCount > 0)
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Failover still in progress after %d nodes reported their LSN "
			"and we are waiting for %d nodes to report, "
			"activeNode is %d (%s:%d) and reported state \"%s\"",
			candidateList.candidateCount,
			candidateList.missingNodesCount,
			activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
			ReplicationStateGetName(activeNode->reportedState));

		return false;
	}

	/*
	 * So all the expected candidates did report their LSN, no node is missing.
	 * Let's see about selecting a candidate for failover now, when we do have
	 * candidates.
	 */
	if (candidateList.candidateCount > 0)
	{
		/* build the list of most advanced standby nodes, not ordered */
		List *mostAdvancedNodeList =
			ListMostAdvancedStandbyNodes(nodesGroupList);

		/* select a node to failover to */
		AutoFailoverNode *selectedNode = NULL;

		/*
		 * standbyNodesGroupList contains at least 2 nodes: we're in the
		 * process of selecting a candidate for failover. Then
		 * mostAdvancedNodeList is expected to always contain at least one
		 * node, the one with the most advanced reportedLSN, and maybe it
		 * contains more than one node.
		 */
		if (list_length(mostAdvancedNodeList) > 0)
		{
			AutoFailoverNode *mostAdvancedNode =
				(AutoFailoverNode *) linitial(mostAdvancedNodeList);

			char message[BUFSIZE] = { 0 };

			candidateList.mostAdvancedNodesGroupList = mostAdvancedNodeList;
			candidateList.mostAdvancedReportedLSN = mostAdvancedNode->reportedLSN;

			/* TODO: should we keep that message in the production release? */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"The current most advanced reported LSN is %X/%X, "
				"as reported by node %d (%s:%d) and %d other nodes",
				(uint32) (mostAdvancedNode->reportedLSN >> 32),
				(uint32) mostAdvancedNode->reportedLSN,
				mostAdvancedNode->nodeId,
				mostAdvancedNode->nodeHost,
				mostAdvancedNode->nodePort,
				list_length(mostAdvancedNodeList) - 1);
		}
		else
		{
			ereport(ERROR, (errmsg("BUG: mostAdvancedNodeList is empty")));
		}

		selectedNode =
			SelectFailoverCandidateNode(&candidateList, primaryNode);

		/* we might not have a selected candidate for failover yet */
		if (selectedNode == NULL)
		{
			/*
			 * Publish more information about the process in the monitor event
			 * table. This is a quite complex mechanism here, and it should be
			 * made as easy as possible to analyze and debug.
			 */
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Failover still in progress after all %d candidate nodes "
				"reported their LSN and we failed to select one of them; "
				"activeNode is %d (%s:%d) and reported state \"%s\"",
				candidateList.candidateCount,
				activeNode->nodeId, activeNode->nodeHost, activeNode->nodePort,
				ReplicationStateGetName(activeNode->reportedState));

			return false;
		}

		return PromoteSelectedNode(selectedNode,
								   primaryNode,
								   &candidateList);
	}

	return false;
}


/*
 * BuildCandidateList builds the list of current standby candidates that have
 * already reported their LSN, and sets
 */
static bool
BuildCandidateList(List *nodesGroupList, CandidateList *candidateList)
{
	ListCell *nodeCell = NULL;
	List *candidateNodesGroupList = NIL;

	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);

	foreach(nodeCell, nodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node == NULL)
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: node is NULL")));
			continue;
		}

		/* skip old and new primary nodes (if a selection has been made) */
		if (StateBelongsToPrimary(node->goalState))
		{
			elog(LOG,
				 "Skipping candidate node %d (%s:%d), "
				 "which is a primary (old or new)",
				 node->nodeId, node->nodeHost, node->nodePort);
			continue;
		}

		/*
		 * Skip unhealthy nodes to avoid having to wait for them to report,
		 * unless the node is unhealthy because Postgres is down, but
		 * pg_autoctl is still reporting.
		 */
		if (IsUnhealthy(node) && !IsReporting(node))
		{
			elog(LOG,
				 "Skipping candidate node %d (%s:%d), which is unhealthy",
				 node->nodeId, node->nodeHost, node->nodePort);

			continue;
		}

		/* grab healthy standby nodes which have reached REPORT_LSN */
		if (IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
		{
			candidateNodesGroupList = lappend(candidateNodesGroupList, node);

			continue;
		}

		/* if REPORT LSN is assigned and not reached yet, count that */
		if (node->goalState == REPLICATION_STATE_REPORT_LSN)
		{
			++(candidateList->missingNodesCount);

			continue;
		}

		/*
		 * Nodes in SECONDARY or CATCHINGUP states are candidates due to report
		 * their LSN.
		 */
		if (IsStateIn(node->reportedState, secondaryStates) &&
			IsStateIn(node->goalState, secondaryStates))
		{
			char message[BUFSIZE] = { 0 };

			++(candidateList->missingNodesCount);

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to report_lsn "
				"to find the failover candidate",
				node->nodeId, node->nodeHost, node->nodePort);

			AssignGoalState(node, REPLICATION_STATE_REPORT_LSN, message);

			continue;
		}
	}

	candidateList->candidateNodesGroupList = candidateNodesGroupList;
	candidateList->candidateCount = list_length(candidateNodesGroupList);

	return true;
}


/*
 * ProceedWithMSFailover drives a failover forward when we already have a
 * failover candidate. It might be the first time we just found/elected a
 * candidate, or one subsequent call to node_active() when then failover is
 * already being orchestrated.
 *
 * Here we have choosen a failover candidate, which is either being
 * promoted to being the new primary (when it already had all the most
 * recent WAL, or is done fetching them), or is fetching the most recent
 * WAL it's still missing from another standby node.
 */
static bool
ProceedWithMSFailover(AutoFailoverNode *activeNode,
					  AutoFailoverNode *candidateNode)
{
	Assert(candidateNode != NULL);

	/*
	 * When the activeNode is "just" another standby which did REPORT LSN, we
	 * stop replication as soon as possible, and later follow the new primary,
	 * as soon as it's ready.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		(IsBeingPromoted(candidateNode) ||
		 IsCurrentState(candidateNode, REPLICATION_STATE_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to join_secondary "
			"after node %d (%s:%d) got selected as the failover candidate.",
			activeNode->nodeId,
			activeNode->nodeHost,
			activeNode->nodePort,
			candidateNode->nodeId,
			candidateNode->nodeHost,
			candidateNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_JOIN_SECONDARY, message);

		return true;
	}

	/* when we have a candidate, we don't go through finding a candidate */
	return false;
}


/*
 * SelectFailoverCandidateNode returns the candidate to failover to when we
 * have one already.
 *
 * The selection is based on candidatePriority. If the candidate with the
 * higher priority doesn't have the most recent LSN, we have it fetch the
 * missing WAL bits from one of the standby which did receive them.
 *
 * Before we enter the selection process, we must have collected the last
 * received LSN from ALL the standby nodes that are considered as a candidate
 * (thanks to the FSM transition secondary -> report_lsn), and now we need to
 * select one of the failover candidates.
 *
 * As input we get the candidateNodesGroupList, a filtered list of standby that
 * are known to be a failover candidate from an earlier filtering process. We
 * also get the mostAdvancedNode and the primaryNode so that we can decide on
 * the next step (cascade WALs or promote directly).
 */
static AutoFailoverNode *
SelectFailoverCandidateNode(CandidateList *candidateList,
							AutoFailoverNode *primaryNode)
{
	/* build the list of failover candidate nodes, ordered by priority */
	List *sortedCandidateNodesGroupList =
		GroupListCandidates(candidateList->candidateNodesGroupList);

	/* it's only one of the most advanced nodes, a reference to compare LSN */
	AutoFailoverNode *mostAdvancedNode =
		(AutoFailoverNode *) linitial(candidateList->mostAdvancedNodesGroupList);

	/* the goal in this function is to find this one */
	AutoFailoverNode *selectedNode = NULL;

	ListCell *nodeCell = NULL;

	/*
	 * We refuse to orchestrate a failover that would have us lose more data
	 * than is configured on the monitor. Both when using sync and async
	 * replication we have the same situation that could happen, where the most
	 * advanced standby node in the system is lagging behind the primary and
	 * promoting it would incur data loss.
	 *
	 * In sync replication, that happens when the primary has been waiting for
	 * a large chunk of WAL bytes to be reported. In async, the only difference
	 * is that the primary did not wait.
	 *
	 * In terms of client-side guarantees, it's a big difference. In term of
	 * data durability, it's the same thing.
	 *
	 * For this situation to change, users will have to either re-live the
	 * unhealthy primary or change the
	 * pgautofailover.enable_sync_wal_log_threshold GUC to a larger value and
	 * thus explicitely accept data loss.
	 */
	if (primaryNode &&
		!WalDifferenceWithin(mostAdvancedNode, primaryNode, PromoteXlogThreshold))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"One of the most advanced standby node in the group "
			"is node %d (%s:%d) "
			"with reported LSN %X/%X, which is more than "
			"pgautofailover.enable_sync_wal_log_threshold (%d) behind "
			"the primary node %d (%s:%d), which has reported %X/%X",
			mostAdvancedNode->nodeId,
			mostAdvancedNode->nodeHost,
			mostAdvancedNode->nodePort,
			(uint32) (mostAdvancedNode->reportedLSN >> 32),
			(uint32) mostAdvancedNode->reportedLSN,
			PromoteXlogThreshold,
			primaryNode->nodeId,
			primaryNode->nodeHost,
			primaryNode->nodePort,
			(uint32) (primaryNode->reportedLSN >> 32),
			(uint32) primaryNode->reportedLSN);

		return false;
	}

	/*
	 * Select the node to be promoted: we can pick any candidate with the
	 * max priority, so we pick the one with the most advanced LSN among
	 * those having max(candidate priority).
	 */
	foreach(nodeCell, sortedCandidateNodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		/* all the candidates are now in the REPORT_LSN state */
		if (IsUnhealthy(node))
		{
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Not selecting failover candidate node %d (%s:%d) "
				"because it is unhealthy",
				node->nodeId, node->nodeHost, node->nodePort);

			continue;
		}
		else
		{
			int cPriority = node->candidatePriority;
			XLogRecPtr cLSN = node->reportedLSN;

			if (selectedNode == NULL)
			{
				selectedNode = node;
			}
			else if (cPriority == selectedNode->candidatePriority &&
					 cLSN > selectedNode->reportedLSN)
			{
				selectedNode = node;
			}
			else if (cPriority < selectedNode->candidatePriority)
			{
				/*
				 * Short circuit the loop, as we scan in decreasing
				 * priority order.
				 */
				break;
			}
		}
	}

	/*
	 * Now we may have a selectedNode. We need to check that either it has all
	 * the WAL needed, or that at least one of the nodes with all the WAL
	 * needed is healthy right now.
	 */
	if (selectedNode &&
		selectedNode->reportedLSN < candidateList->mostAdvancedReportedLSN)
	{
		bool someMostAdvancedStandbysAreHealthy = false;

		foreach(nodeCell, candidateList->mostAdvancedNodesGroupList)
		{
			AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

			if (IsHealthy(node))
			{
				someMostAdvancedStandbysAreHealthy = true;
				break;
			}
		}

		if (!someMostAdvancedStandbysAreHealthy)
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"The selected candidate %d (%s:%d) needs to fetch missing "
				"WAL to reach LSN %X/%X (from current reported LSN %X/%X) "
				"and none of the most advanced standby nodes are healthy "
				"at the moment.",
				selectedNode->nodeId,
				selectedNode->nodeHost,
				selectedNode->nodePort,
				(uint32) (mostAdvancedNode->reportedLSN >> 32),
				(uint32) mostAdvancedNode->reportedLSN,
				(uint32) (selectedNode->reportedLSN >> 32),
				(uint32) selectedNode->reportedLSN);

			return NULL;
		}
	}

	return selectedNode;
}


/*
 * PromoteSelectedNode assigns goal state to the selected node to failover to.
 */
static bool
PromoteSelectedNode(AutoFailoverNode *selectedNode,
					AutoFailoverNode *primaryNode,
					CandidateList *candidateList)
{
	/*
	 * Ok so we now may start the failover process, we have selected a
	 * candidate after all nodes reported their LSN. We still have two
	 * possible situations here:
	 *
	 * - if the selected candidate has all the WAL bytes, promote it
	 *   already
	 *
	 * - if the selected candidate is lagging, we ask it to connect to a
	 *   standby that has not been selected and grab missing WAL bytes from
	 *   there
	 *
	 * When the perform_promotion API has been used to promote a specific node
	 * in the system then its candidate priority has been incremented by 100.
	 * Now is the time to reset it.
	 */
	if (selectedNode->candidatePriority > MAX_USER_DEFINED_CANDIDATE_PRIORITY)
	{
		char message[BUFSIZE] = { 0 };

		selectedNode->candidatePriority -= MAX_USER_DEFINED_CANDIDATE_PRIORITY;

		ReportAutoFailoverNodeReplicationSetting(
			selectedNode->nodeId,
			selectedNode->nodeHost,
			selectedNode->nodePort,
			selectedNode->candidatePriority,
			selectedNode->replicationQuorum);

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Updating candidate priority back to %d for node %d \"%s\" (%s:%d)",
			selectedNode->candidatePriority,
			selectedNode->nodeId,
			selectedNode->nodeName,
			selectedNode->nodeHost,
			selectedNode->nodePort);

		NotifyStateChange(selectedNode, message);
	}

	if (selectedNode->reportedLSN == candidateList->mostAdvancedReportedLSN)
	{
		char message[BUFSIZE] = { 0 };

		if (primaryNode)
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to prepare_promotion "
				"after node %d (%s:%d) became unhealthy "
				"and %d nodes reported their LSN position.",
				selectedNode->nodeId,
				selectedNode->nodeHost,
				selectedNode->nodePort,
				primaryNode->nodeId,
				primaryNode->nodeHost,
				primaryNode->nodePort,
				candidateList->candidateCount);
		}
		else
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to prepare_promotion "
				"and %d nodes reported their LSN position.",
				selectedNode->nodeId,
				selectedNode->nodeHost,
				selectedNode->nodePort,
				candidateList->candidateCount);
		}

		AssignGoalState(selectedNode,
						REPLICATION_STATE_PREPARE_PROMOTION,
						message);

		/* leave the other nodes in ReportLSN state for now */
		return true;
	}
	else
	{
		char message[BUFSIZE] = { 0 };

		if (primaryNode)
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to fast_forward "
				"after node %d (%s:%d) became unhealthy "
				"and %d nodes reported their LSN position.",
				selectedNode->nodeId,
				selectedNode->nodeHost,
				selectedNode->nodePort,
				primaryNode->nodeId,
				primaryNode->nodeHost,
				primaryNode->nodePort,
				candidateList->candidateCount);
		}
		else
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to fast_forward "
				"and %d nodes reported their LSN position.",
				selectedNode->nodeId,
				selectedNode->nodeHost,
				selectedNode->nodePort,
				candidateList->candidateCount);
		}

		AssignGoalState(selectedNode,
						REPLICATION_STATE_FAST_FORWARD, message);

		return true;
	}
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
		SetNodeGoalState(pgAutoFailoverNode, state, description);
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

	return pgAutoFailoverNode->health == NODE_HEALTH_GOOD &&
		   pgAutoFailoverNode->pgIsRunning == true;
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
 * IsReporting returns whether the given node has reported recently, within the
 * UnhealthyTimeoutMs interval.
 */
static bool
IsReporting(AutoFailoverNode *pgAutoFailoverNode)
{
	TimestampTz now = GetCurrentTimestamp();

	if (pgAutoFailoverNode == NULL)
	{
		return false;
	}

	if (TimestampDifferenceExceeds(pgAutoFailoverNode->reportTime,
								   now,
								   UnhealthyTimeoutMs))
	{
		return false;
	}

	return true;
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

	if (pgAutoFailoverNode == NULL ||
		pgAutoFailoverNode->goalState != REPLICATION_STATE_DEMOTE_TIMEOUT)
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
