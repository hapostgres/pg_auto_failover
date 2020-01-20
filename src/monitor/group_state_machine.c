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

	/* when there's no other node anymore, not even one */
	if (nodesCount == 1
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

	/*
	 * We separate out the FSM for the primary server, because that one needs
	 * to loop over every other node to take decisions. That induces some
	 * complexity that is best managed in a specialized function.
	 */
	if (IsInPrimaryState(activeNode))
	{
		return ProceedGroupStateForPrimaryNode(activeNode);
	}

	primaryNode = GetPrimaryNodeInGroup(formationId, groupId);

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

	/* Multiple Standby failover is handled in its own function */
	if (IsUnhealthy(primaryNode) && nodesCount > 2)
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
			"Setting goal state of %s:%d to catchingup after %s:%d "
			"converged to wait_primary.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * Remember that here we know we have a single standby node.
	 *
	 * when secondary caught up:
	 *      catchingup -> secondary
	 *  + wait_primary -> primary
	 *
	 * FIXME/REVIEW: when handling multi-standby nodes failover, we might be a
	 * PRIMARY already when there's still a standby in CATCHINGUP and that is
	 * otherwise running fine. So I (dim) have added the state PRIMARY to the
	 * list here, though maybe that warrants another round of review of the
	 * FSM.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_CATCHINGUP) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)) &&
		IsHealthy(activeNode) &&
		WalDifferenceWithin(activeNode, primaryNode, EnableSyncXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to primary and %s:%d to "
			"secondary after %s:%d caught up.",
			primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeName, activeNode->nodePort);

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
			"Setting goal state of %s:%d to draining and %s:%d to "
			"prepare_promotion after %s:%d became unhealthy.",
			primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

		/* keep reading until no more records are available */
		AssignGoalState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION, message);

		/* shut down the primary */
		AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);

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
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the coordinator metadata was updated.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when node is seeing no more writes:
	 *  prepare_promotion -> stop_replication
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to demote_timeout and %s:%d to "
			"stop_replication after %s:%d converged to "
			"prepare_promotion.",
			primaryNode->nodeName, primaryNode->nodePort,
			activeNode->nodeName, activeNode->nodePort,
			activeNode->nodeName, activeNode->nodePort);

		/* perform promotion to stop replication */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		/* wait for possibly-alive primary to kill itself */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTE_TIMEOUT, message);

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
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the demote timeout expired.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

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
			"Setting goal state of %s:%d to wait_primary and %s:%d to "
			"demoted after the coordinator metadata was updated.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

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
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to catchingup after it "
			"converged to demotion and %s:%d converged to wait_primary.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  report_lsn -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to secondary after %s:%d converged "
			"to primary.",
			activeNode->nodeName, activeNode->nodePort,
			primaryNode->nodeName, primaryNode->nodePort);

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
					"Setting goal state of %s:%d to wait_primary after %s:%d "
					"joined.", primaryNode->nodeName, primaryNode->nodePort,
					otherNode->nodeName, otherNode->nodePort);

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
					"Setting goal state of %s:%d to join_primary after %s:%d "
					"joined.", primaryNode->nodeName, primaryNode->nodePort,
					otherNode->nodeName, otherNode->nodePort);

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
					"Setting goal state of %s:%d to catchingup "
					"after it became unhealthy.",
					otherNode->nodeName, otherNode->nodePort);

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
					"Setting goal state of %s:%d to wait_primary "
					"now that none of the standbys are healthy anymore.",
					primaryNode->nodeName, primaryNode->nodePort);

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
			"Setting goal state of %s:%d to primary "
			"after it applied replication properties change.",
			primaryNode->nodeName, primaryNode->nodePort);

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
	// AutoFailoverFormation *formation = GetFormation(activeNode->formationId);
	List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
	AutoFailoverNode *candidateNode = NULL;
	ListCell *nodeCell = NULL;

	int candidateCount = 0;
	int reportedLSNCount = 0;

	/*
	 * Done with the single standby code path, now we have several standby
	 * nodes that might all be candidate for failover, or just some of them.
	 * First, have them all report the most recent LSN they managed to receive.
	 */
	reportedLSNCount = 0;

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
			continue;
		}

		/*
		 * Skip nodes that are not failover candidates (not in SECONDARY or
		 * REPORT_LSN state).
		 *
		 * XXX: what about goalState instead? or in addition to?
		 */
		if (node->reportedState != REPLICATION_STATE_SECONDARY
			&& node->reportedState != REPLICATION_STATE_REPORT_LSN)
		{
			continue;
		}

		/* count how many standby nodes have reached REPORT_LSN */
		if (IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
		{
			++candidateCount;
			++reportedLSNCount;
		}
		else if (node->goalState != REPLICATION_STATE_REPORT_LSN)
		{
			char message[BUFSIZE];

			++candidateCount;

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of %s:%d to report_lsn "
				"to find the failover candidate",
				node->nodeName, node->nodePort);

			AssignGoalState(node, REPLICATION_STATE_REPORT_LSN, message);
		}
	}

	/*
	 * If we can failover, make sure the primary is being demoted before doing
	 * anything else.
	 */
	if (candidateNode != NULL || candidateCount > 0)
	{
		/* shut down the primary, known unhealthy (see pre-conditions) */
		if (IsInPrimaryState(primaryNode))
		{
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state %s:%d to draining after it became unhealthy.",
				primaryNode->nodeName, primaryNode->nodePort);

			AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);
		}
	}

	/*
	 * If a failover is in progress, continue driving it.
	 */
	if (candidateNode != NULL)
	{
		return ProceedWithMSFailover(activeNode, candidateNode);
	}

	/*
	 * We reach this code when we don't have an healthy primary anymore, it's
	 * been demoted or is draining now. Most probably it's dead. We have
	 * collected the last received LSN from all the standby nodes in SECONDARY
	 * state, and now we need to select one of the failover candidates.
	 *
	 * The selection is based on candidatePriority. If the candidate with the
	 * higher priority doesn't have the most recent LSN, we have it fetch the
	 * missing WAL bits from one of the standby which did receive them.
	 */
	if (reportedLSNCount == candidateCount)
	{
		/* build the list of failover candidate nodes, ordered by priority */
		List *candidateNodesGroupList =
			GroupListCandidates(standbyNodesGroupList);

		/* find the standby node that has the most advanced LSN */
		AutoFailoverNode *mostAdvancedNode =
			FindMostAdvancedStandby(standbyNodesGroupList);

		AutoFailoverNode *selectedNode = NULL;

		/*
		 * Select the node to be promoted: we can pick any candidate with the
		 * max priority, so we pick the one with the most recent LSN among
		 * those of maxPriority.
		 */
		foreach(nodeCell, candidateNodesGroupList)
		{
			candidateNode = (AutoFailoverNode *) lfirst(nodeCell);

			if (IsHealthy(candidateNode)
				&& WalDifferenceWithin(candidateNode,
									   primaryNode,
									   PromoteXlogThreshold))
			{
				int cPriority = candidateNode->candidatePriority;
				XLogRecPtr cLSN = candidateNode->reportedLSN;

				if (selectedNode == NULL)
				{
					selectedNode = candidateNode;
				}
				else if (cPriority == selectedNode->candidatePriority
						 && cLSN > selectedNode->reportedLSN)
				{
					selectedNode = candidateNode;
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
		 * We might have selected a node to fail over to: start the failover.
		 */
		if (selectedNode != NULL)
		{
			char message[BUFSIZE];

			/* do we have to fetch some missing WAL? */
			if (selectedNode->reportedLSN == mostAdvancedNode->reportedLSN)
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of %s:%d to prepare_promotion "
					"after %s:%d became unhealthy.",
					selectedNode->nodeName, selectedNode->nodePort,
					primaryNode->nodeName, primaryNode->nodePort);

				AssignGoalState(selectedNode,
								REPLICATION_STATE_PREPARE_PROMOTION,
								message);

				/* leave the other nodes in ReportLSN state for now */
				return true;
			}

			/* so the candidate does not have the most recent WAL */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of %s:%d to wait_forward "
				"and goal state of %s:%d to wait_cascade "
				"after %s:%d became unhealthy.",
				selectedNode->nodeName, selectedNode->nodePort,
				mostAdvancedNode->nodeName, mostAdvancedNode->nodePort,
				primaryNode->nodeName, primaryNode->nodePort);

			AssignGoalState(selectedNode,
							REPLICATION_STATE_WAIT_FORWARD, message);

			AssignGoalState(mostAdvancedNode,
							REPLICATION_STATE_WAIT_CASCADE, message);

			return true;
		}
		else
		{
			/* should we maybe ereport() with an hint? */
			return false;
		}
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
			"Setting goal state of %s:%d to fast_forward "
			"after %s:%d converged to wait_cascade.",
			candidateNode->nodeName, candidateNode->nodePort,
			activeNode->nodeName, activeNode->nodePort);

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
		IsCurrentState(candidateNode, REPLICATION_STATE_STOP_REPLICATION))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to secondary "
			"after %s:%d converged to stop_replication.",
			activeNode->nodeName, activeNode->nodePort,
			candidateNode->nodeName, candidateNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

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
			"Setting goal state of %s:%d to prepare_promotion",
			activeNode->nodeName, activeNode->nodePort);

		AssignGoalState(activeNode,
						REPLICATION_STATE_PREPARE_PROMOTION,
						message);

		return true;
	}

	/*
	 * When the activeNode is "just" another standby, it's time to follow the
	 * new primary as soon as our candidate reaches stop_replication.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		IsCurrentState(candidateNode, REPLICATION_STATE_STOP_REPLICATION))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to secondary "
			"after %s:%d converged to stop_replication.",
			activeNode->nodeName, activeNode->nodePort,
			candidateNode->nodeName, candidateNode->nodePort);

		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		return true;
	}

	/* when we have a candidate, we don't go through finding a candidate */
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
