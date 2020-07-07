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
static bool ProceedGroupStateForPrimaryNode(AutoFailoverNode *primaryNode);
static bool ProceedGroupStateForMSFailover(AutoFailoverNode *activeNode,
										   AutoFailoverNode *primaryNode);
static bool ProceedWithMSFailover(AutoFailoverNode *activeNode,
								  AutoFailoverNode *candidateNode);
static AutoFailoverNode * SelectFailoverCandidateNode(List *candidateNodesGroupList,
													  AutoFailoverNode *mostAdvancedNode,
													  AutoFailoverNode *primaryNode);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort);

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

	if (primaryNode == NULL)
	{
		/* that's a bug, really, maybe we could use an Assert() instead */
		ereport(ERROR,
				(errmsg("ProceedGroupState couldn't find the primary node "
						"in formation \"%s\", group %d",
						formationId, groupId),
				 errdetail("activeNode is %s:%d in state %s",
						   activeNode->nodeName, activeNode->nodePort,
						   ReplicationStateGetName(activeNode->goalState))));
	}

	/* Multiple Standby failover is handled in its own function. */
	if (nodesCount > 2 && IsUnhealthy(primaryNode))
	{
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
	 * There are other cases when we want to continue an already started
	 * failover.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) ||
		IsCurrentState(activeNode, REPLICATION_STATE_WAIT_FORWARD) ||
		IsCurrentState(activeNode, REPLICATION_STATE_FAST_FORWARD) ||
		IsCurrentState(activeNode, REPLICATION_STATE_WAIT_CASCADE))
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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort);

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
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

		/* keep reading until no more records are available */
		AssignGoalState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION, message);

		/* shut down the primary */
		AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);

		return true;
	}

	/*
	 * when secondary is put to maintenance
	 *  wait_maintenance -> maintenance
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_MAINTENANCE) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to maintenance "
			"after %s:%d converged to wait_primary.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

		/* promote the secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_MAINTENANCE, message);

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
			"Setting goal state of %s:%d to wait_primary "
			"and %s:%d to maintenance.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
		!IsInMaintenance(primaryNode))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to demote_timeout "
			"and node %d (%s:%d) to stop_replication "
			"after node %d (%s:%d) converged to prepare_promotion.",
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort);

		/* perform promotion to stop replication */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		/* wait for possibly-alive primary to kill itself */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTE_TIMEOUT, message);

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
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);
		AssignGoalState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  demoted -> catchingup
	 *
	 * TODO: check that we can join a WAIT_PRIMARY node without having to wait
	 * until it's done registering the new standby node. It might be that our
	 * HBA entry is not there yet, but I think we're good.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to catchingup after it "
			"converged to demotion and node %d (%s:%d) converged to wait_primary.",
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);
		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
	}

	if (IsCurrentState(activeNode, REPLICATION_STATE_JOIN_SECONDARY) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"after node %d (%s:%d) converged to primary.",
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

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
					primaryNode->nodeName,
					primaryNode->nodePort,
					otherNode->nodeId,
					otherNode->nodeName,
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
					primaryNode->nodeName,
					primaryNode->nodePort,
					otherNode->nodeId,
					otherNode->nodeName,
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
					otherNode->nodeId, otherNode->nodeName, otherNode->nodePort);

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
			if (formation->number_sync_standbys == 1 &&
				failoverCandidateCount < formation->number_sync_standbys)
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to wait_primary "
					"now that none of the standbys are healthy anymore.",
					primaryNode->nodeId,
					primaryNode->nodeName,
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
			primaryNode->nodeId, primaryNode->nodeName, primaryNode->nodePort);

		AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

		return true;
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
	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);

	List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
	List *candidateNodesGroupList = NIL;

	AutoFailoverNode *candidateNode = NULL;
	ListCell *nodeCell = NULL;

	int candidateCount = 0;
	int reportedLSNCount = 0;

	/*
	 * Done with the single standby code path, now we have several standby
	 * nodes that might all be candidate for failover, or just some of them.
	 *
	 * The first order of business though is to determine if a failover is
	 * currently happening, by looping over all the nodes in case one of them
	 * has already been selected as the failover candidate.
	 */
	foreach(nodeCell, standbyNodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node == NULL)
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: node is NULL")));
			continue;
		}

		/* we might have a failover ongoing already */
		if (IsBeingPromoted(node))
		{
			candidateNode = node;

			elog(LOG, "Found candidate node %d (%s:%d)",
				 node->nodeId, node->nodeName, node->nodePort);
			break;
		}
	}

	/*
	 * If a failover is in progress, continue driving it.
	 */
	if (candidateNode != NULL)
	{
		/* shut down the primary, known unhealthy (see pre-conditions) */
		if (IsInPrimaryState(primaryNode))
		{
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to draining "
				"after it became unhealthy. We count %d failover candidates",
				primaryNode->nodeId,
				primaryNode->nodeName,
				primaryNode->nodePort,
				candidateCount);

			AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);
		}

		return ProceedWithMSFailover(activeNode, candidateNode);
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
	foreach(nodeCell, standbyNodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node == NULL)
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: node is NULL")));
			continue;
		}

		/* skip unhealthy nodes to avoid having to wait for them to report */
		if (IsUnhealthy(node))
		{
			elog(LOG,
				 "Skipping candidate node %d (%s:%d), which is unhealthy",
				 node->nodeId, node->nodeName, node->nodePort);
			continue;
		}

		/* count how many healthy standby nodes have reached REPORT_LSN */
		if (IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
		{
			++candidateCount;
			++reportedLSNCount;

			candidateNodesGroupList = lappend(candidateNodesGroupList, node);

			continue;
		}

		/* if REPORT LSN is assigned and not reached yet, count that */
		if (node->goalState == REPLICATION_STATE_REPORT_LSN)
		{
			++candidateCount;

			continue;
		}

		/*
		 * Nodes in SECONDARY or CATCHINGUP states are candidates due to report
		 * their LSN.
		 */
		if (IsStateIn(node->reportedState, secondaryStates) &&
			IsStateIn(node->goalState, secondaryStates))
		{
			char message[BUFSIZE];

			++candidateCount;

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to report_lsn "
				"to find the failover candidate",
				node->nodeId, node->nodeName, node->nodePort);

			AssignGoalState(node, REPLICATION_STATE_REPORT_LSN, message);
		}
	}

	/* shut down the primary as soon as possible */
	if (candidateCount > 0)
	{
		/* shut down the primary, known unhealthy (see pre-conditions) */
		if (IsInPrimaryState(primaryNode))
		{
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to draining "
				"after it became unhealthy. We count %d failover candidates",
				primaryNode->nodeId,
				primaryNode->nodeName,
				primaryNode->nodePort,
				candidateCount);

			AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);
		}
	}

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
	 *
	 * We might fail to have reportedLSNCount == candidateCount in the
	 * following cases:
	 *
	 * - the activeNode is the first to report its LSN, we didn't hear from the
	 *   other nodes yet; in that case we want to wait until we hear from them,
	 *   it is expected to happen within the next 5 seconds.
	 *
	 * - a standby that was counted as a candidate is not reporting; most
	 *   probably it's not healthy anymore (or has been put to maintenance
	 *   during the failover process), and the next call to node_active()
	 *   should be able to account for that new global state and make progress:
	 *   the now faulty standby will NOT be counted as candidate anymore.
	 */
	if (candidateCount > 0 && reportedLSNCount == candidateCount)
	{
		/* find the standby node that has the most advanced LSN */
		AutoFailoverNode *mostAdvancedNode =
			FindMostAdvancedStandby(standbyNodesGroupList);

		/* select a node to failover to */
		AutoFailoverNode *selectedNode =
			SelectFailoverCandidateNode(candidateNodesGroupList,
										mostAdvancedNode,
										primaryNode);

		/*
		 * We might have selected a node to fail over to: start the failover.
		 */
		if (selectedNode != NULL)
		{
			/* do we have to fetch some missing WAL? */
			if (selectedNode->reportedLSN == mostAdvancedNode->reportedLSN)
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to prepare_promotion "
					"after node %d (%s:%d) became unhealthy "
					"and %d nodes reported their LSN position.",
					selectedNode->nodeId,
					selectedNode->nodeName,
					selectedNode->nodePort,
					primaryNode->nodeId,
					primaryNode->nodeName,
					primaryNode->nodePort,
					reportedLSNCount);

				AssignGoalState(selectedNode,
								REPLICATION_STATE_PREPARE_PROMOTION,
								message);

				/* leave the other nodes in ReportLSN state for now */
				return true;
			}

			/* so the candidate does not have the most recent WAL */
			else
			{
				char message[BUFSIZE];

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of node %d (%s:%d) to wait_forward "
					"and goal state of node %d (%s:%d) to wait_cascade "
					"after node %d (%s:%d) became unhealthy "
					"and %d nodes reported their LSN position.",
					selectedNode->nodeId,
					selectedNode->nodeName,
					selectedNode->nodePort,
					mostAdvancedNode->nodeId,
					mostAdvancedNode->nodeName,
					mostAdvancedNode->nodePort,
					primaryNode->nodeId,
					primaryNode->nodeName,
					primaryNode->nodePort,
					reportedLSNCount);

				AssignGoalState(selectedNode,
								REPLICATION_STATE_WAIT_FORWARD, message);

				AssignGoalState(mostAdvancedNode,
								REPLICATION_STATE_WAIT_CASCADE, message);

				return true;
			}
		}

		/* we don't have a selected candidate for failover yet */
		else
		{
			/*
			 * Publish more information about the process in the monitor event
			 * table. This is a quite complex mechanism here, and it should be
			 * made as easy as possible to analyze and debug.
			 */
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Failover still in progress after all %d candidate nodes "
				"reported their LSN and we failed to select one of them; "
				"activeNode is %d (%s:%d) and reported state \"%s\"",
				reportedLSNCount,
				activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
				ReplicationStateGetName(activeNode->reportedState));
			return false;
		}
	}

	/* too early: not everybody reported REPORT_LSN yet */
	else
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Failover still in progress after %d nodes reported their LSN "
			"among %d candidates that we are waiting for in total, "
			"activeNode is %d (%s:%d) and reported state \"%s\"",
			reportedLSNCount, candidateCount,
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort,
			ReplicationStateGetName(activeNode->reportedState));
	}

	return false;
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
	List *finishedCascading =
		list_make5_int(REPLICATION_STATE_PREPARE_PROMOTION,
					   REPLICATION_STATE_STOP_REPLICATION,
					   REPLICATION_STATE_WAIT_PRIMARY,
					   REPLICATION_STATE_PRIMARY,
					   REPLICATION_STATE_JOIN_PRIMARY);

	Assert(candidateNode != NULL);

	/*
	 * Everyone reported their LSN, we found a candidate, and it has to fast
	 * forward. First, the node with the most advanced LSN position had to
	 * reach wait_cascade:
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_CASCADE) &&
		IsCurrentState(candidateNode, REPLICATION_STATE_WAIT_FORWARD))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to fast_forward "
			"after node %d (%s:%d) converged to wait_cascade.",
			candidateNode->nodeId,
			candidateNode->nodeName,
			candidateNode->nodePort,
			activeNode->nodeId,
			activeNode->nodeName,
			activeNode->nodePort);

		AssignGoalState(candidateNode,
						REPLICATION_STATE_FAST_FORWARD, message);

		return true;
	}

	/*
	 * The candidate had to fast forward with the activeNode, grabbing WAL bits
	 * that only this node had. Now that's done. The activeNode has to follow
	 * the new primary that's being promoted.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_CASCADE) &&
		IsStateIn(candidateNode->goalState, finishedCascading))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to secondary "
			"after node %d (%s:%d) converged to stop_replication.",
			activeNode->nodeId,
			activeNode->nodeName,
			activeNode->nodePort,
			candidateNode->nodeId,
			candidateNode->nodeName,
			candidateNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_JOIN_SECONDARY, message);

		return true;
	}

	/*
	 * When the candidate is done fast forwarding the locally missing WAL bits,
	 * it can be promoted.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_FAST_FORWARD))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to prepare_promotion",
			activeNode->nodeId, activeNode->nodeName, activeNode->nodePort);

		AssignGoalState(activeNode,
						REPLICATION_STATE_PREPARE_PROMOTION,
						message);

		return true;
	}

	/*
	 * When the activeNode is "just" another standby which did REPORT LSN, we
	 * stop replication as soon as possible, and later follow the new primary,
	 * as soon as it's ready.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		(IsCurrentState(candidateNode, REPLICATION_STATE_PREPARE_PROMOTION) ||
		 IsCurrentState(candidateNode, REPLICATION_STATE_STOP_REPLICATION)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to join_secondary "
			"after node %d (%s:%d) got selected as the failover candidate.",
			activeNode->nodeId,
			activeNode->nodeName,
			activeNode->nodePort,
			candidateNode->nodeId,
			candidateNode->nodeName,
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
 * As input we need two lists of node:
 *
 * - standbyNodesGroupList contains all the standby nodes known in this group,
 *   even when they won't be a candidate for failover (because of a
 *   candidatepriority of zero, or because they are not healthy at this time)
 *
 * - candidateNodesGroupList is a filtered list of standby that are known to be
 *   a failover candidate from an earlier filtering process
 */
static AutoFailoverNode *
SelectFailoverCandidateNode(List *candidateNodesGroupList,
							AutoFailoverNode *mostAdvancedNode,
							AutoFailoverNode *primaryNode)
{
	/* build the list of failover candidate nodes, ordered by priority */
	List *sortedCandidateNodesGroupList =
		GroupListCandidates(candidateNodesGroupList);

	AutoFailoverNode *selectedNode = NULL;

	ListCell *nodeCell = NULL;

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
				node->nodeId, node->nodeName, node->nodePort);

			continue;
		}

		/*
		 * We only select the most advanced standby node when it is within our
		 * acceptable WAL lag threshold. Other candidates are fine because we
		 * are going to apply our "forward lsn" sequence when picking them.
		 */
		else if (node->nodeId == mostAdvancedNode->nodeId &&
				 !WalDifferenceWithin(node, primaryNode, PromoteXlogThreshold))
		{
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Not selecting failover candidate node %d (%s:%d) "
				"with reported LSN %X/%X, which is more than "
				"pgautofailover.enable_sync_wal_log_threshold (%d) behind "
				"the primary node %d (%s:%d), which has reported %X/%X",
				node->nodeId,
				node->nodeName,
				node->nodePort,
				(uint32) (node->reportedLSN >> 32),
				(uint32) node->reportedLSN,
				PromoteXlogThreshold,
				primaryNode->nodeId,
				primaryNode->nodeName,
				primaryNode->nodePort,
				(uint32) (primaryNode->reportedLSN >> 32),
				(uint32) primaryNode->reportedLSN);

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

	return selectedNode;
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
	List *pgIsNotRunningStateList =
		list_make3_int(REPLICATION_STATE_DRAINING,
					   REPLICATION_STATE_DEMOTED,
					   REPLICATION_STATE_DEMOTE_TIMEOUT);

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
	if (!pgAutoFailoverNode->pgIsRunning &&
		IsStateIn(pgAutoFailoverNode->goalState, pgIsNotRunningStateList))
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
