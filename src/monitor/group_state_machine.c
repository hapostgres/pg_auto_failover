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
	int numberSyncStandbys;
	List *candidateNodesGroupList;
	List *mostAdvancedNodesGroupList;
	XLogRecPtr mostAdvancedReportedLSN;
	int candidateCount;
	int quorumCandidateCount;
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
static bool WalDifferenceWithin(AutoFailoverNode *secondaryNode,
								AutoFailoverNode *primaryNode,
								int64 delta);

/* GUC variables */
int EnableSyncXlogThreshold = DEFAULT_XLOG_SEG_SIZE;
int PromoteXlogThreshold = DEFAULT_XLOG_SEG_SIZE;


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

	List *nodesGroupList = AutoFailoverNodeGroup(formationId, groupId);
	int nodesCount = list_length(nodesGroupList);

	if (formation == NULL)
	{
		ereport(ERROR,
				(errmsg("Formation for %s could not be found",
						activeNode->formationId)));
	}

	/*
	 * If the active node just reached the DROPPED state, proceed to remove it
	 * from the pgautofailover.node table.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DROPPED))
	{
		char message[BUFSIZE] = { 0 };

		/* time to actually remove the current node */
		RemoveAutoFailoverNode(activeNode);

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Removing " NODE_FORMAT " from formation \"%s\" and group %d",
			NODE_FORMAT_ARGS(activeNode),
			activeNode->formationId,
			activeNode->groupId);

		return true;
	}

	/* node reports secondary/dropped */
	if (activeNode->goalState == REPLICATION_STATE_DROPPED)
	{
		return true;
	}

	/*
	 * A node in "maintenance" state can only get out of maintenance through an
	 * explicit call to stop_maintenance(), the FSM will not assign a new state
	 * to a node that is currently in maintenance.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_MAINTENANCE))
	{
		return true;
	}

	/*
	 * A node that is alone in its group should be SINGLE.
	 *
	 * Exception arises when it used to be other nodes in the group, and the
	 * only node left has Candidate Priority of zero. In that case the setup is
	 * clear, it can't allow writes, so it can't be SINGLE. In that case, it
	 * should be REPORT_LSN, waiting for either a change of settings, or the
	 * introduction of a new node.
	 */
	if (nodesCount == 1 &&
		!IsCurrentState(activeNode, REPLICATION_STATE_SINGLE) &&
		activeNode->candidatePriority > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to single as there is no other node.",
			NODE_FORMAT_ARGS(activeNode));

		/* other node may have been removed */
		AssignGoalState(activeNode, REPLICATION_STATE_SINGLE, message);

		return true;
	}
	else if (nodesCount == 1 &&
			 !IsCurrentState(activeNode, REPLICATION_STATE_SINGLE) &&
			 activeNode->candidatePriority == 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to report_lsn as there is no other node"
			" and candidate priority is %d.",
			NODE_FORMAT_ARGS(activeNode),
			activeNode->candidatePriority);

		/* other node may have been removed */
		AssignGoalState(activeNode, REPLICATION_STATE_REPORT_LSN, message);

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

	AutoFailoverNode *primaryNode =
		GetPrimaryOrDemotedNodeInGroup(formationId, groupId);

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
				 errdetail("activeNode is " NODE_FORMAT
						   " in state %s",
						   NODE_FORMAT_ARGS(activeNode),
						   ReplicationStateGetName(activeNode->goalState))));
	}

	/* Multiple Standby failover is handled in its own function. */
	if (nodesCount > 2 && IsUnhealthy(primaryNode))
	{
		/*
		 * The WAIT_PRIMARY state encodes the fact that we know there is no
		 * failover candidate, so there's no point in orchestrating a failover,
		 * even though the primary node is currently not available.
		 *
		 * To be in the WAIT_PRIMARY means that the other nodes are all either
		 * unhealty or with candidate priority set to zero.
		 *
		 * Otherwise stop replication from the primary and proceed with
		 * candidate election for primary replacement, whenever we have at
		 * least one candidates for failover.
		 */
		List *candidateNodesList =
			AutoFailoverOtherNodesListInState(primaryNode,
											  REPLICATION_STATE_SECONDARY);

		int candidatesCount = CountHealthyCandidates(candidateNodesList);

		if (IsInPrimaryState(primaryNode) &&
			!IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) &&
			candidatesCount >= 1)
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to draining after it became unhealthy.",
				NODE_FORMAT_ARGS(primaryNode));

			AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);
		}

		/*
		 * In a multiple standby system we can assign maintenance as soon as
		 * prepare_maintenance has been reached, at the same time than an
		 * election is triggered. This also allows the operator to disable
		 * maintenance on the old-primary and have it join the election.
		 */
		else if (IsCurrentState(primaryNode, REPLICATION_STATE_PREPARE_MAINTENANCE))
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to maintenance after it converged to prepare_maintenance.",
				NODE_FORMAT_ARGS(primaryNode));

			AssignGoalState(primaryNode, REPLICATION_STATE_MAINTENANCE, message);
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
	 *
	 * Let the main primary loop account for allSecondariesAreHealthy and only
	 * then decide to assign PRIMARY to the primaryNode.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)) &&
		IsHealthy(primaryNode))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to secondary after " NODE_FORMAT
			" converged to %s and has been marked healthy.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode),
			ReplicationStateGetName(primaryNode->reportedState));

		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		return true;
	}

	/*
	 * when report_lsn and the promotion has been done already:
	 *      report_lsn -> secondary
	 *
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_REPORT_LSN) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) &&
		IsHealthy(primaryNode))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to secondary after " NODE_FORMAT
			" got selected as the failover candidate.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

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
			"Setting goal state of " NODE_FORMAT
			" to prepare_promotion",
			NODE_FORMAT_ARGS(activeNode));

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
	 *  wait_standby -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_STANDBY) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup after " NODE_FORMAT
			" converged to %s.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode),
			ReplicationStateGetName(primaryNode->reportedState));

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when primary node is ready for replication:
	 *  wait_standby -> catchingup
	 *  primary -> apply_settings
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_STANDBY) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) &&
		activeNode->replicationQuorum)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup and " NODE_FORMAT
			" to %s to edit synchronous_standby_names.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode),
			ReplicationStateGetName(primaryNode->reportedState));

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		/* edit synchronous_standby_names to add the new standby now */
		AssignGoalState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS, message);

		return true;
	}

	/*
	 * when primary node is ready for replication:
	 *  wait_standby -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_STANDBY) &&
		IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) &&
		!activeNode->replicationQuorum)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup.",
			NODE_FORMAT_ARGS(activeNode));

		/* start replication */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when secondary caught up:
	 *      catchingup -> secondary
	 *  + wait_primary -> primary
	 *
	 * When we have multiple standby nodes and one of them is joining, or
	 * re-joining after maintenance, we have to edit the replication setting
	 * synchronous_standby_names on the primary. The transition from another
	 * state to PRIMARY includes that edit. If the primary already is in the
	 * primary state, we assign APPLY_SETTINGS to it to make sure its
	 * repication settings are updated now.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_CATCHINGUP) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY)) &&
		IsHealthy(activeNode) &&
		activeNode->reportedTLI == primaryNode->reportedTLI &&
		WalDifferenceWithin(activeNode, primaryNode, EnableSyncXlogThreshold))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to secondary after it caught up.",
			NODE_FORMAT_ARGS(activeNode));

		/* node is ready for promotion */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

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
		activeNode->candidatePriority > 0 &&
		WalDifferenceWithin(activeNode, primaryNode, PromoteXlogThreshold))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to draining and " NODE_FORMAT
			" to prepare_promotion "
			"after " NODE_FORMAT
			" became unhealthy.",
			NODE_FORMAT_ARGS(primaryNode),
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

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
			"Setting goal state of " NODE_FORMAT
			" to maintenance after " NODE_FORMAT
			" converged to wait_primary.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* secondary reached maintenance */
		AssignGoalState(activeNode, REPLICATION_STATE_MAINTENANCE, message);

		return true;
	}

	/*
	 * when secondary is in wait_maintenance state and goal state of primary is
	 * not wait_primary anymore, e.g. another node joined and made it primary
	 * again or it got demoted. Then we don't need to wait anymore and we can
	 * transition directly to maintenance.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_WAIT_MAINTENANCE) &&
		primaryNode->goalState != REPLICATION_STATE_WAIT_PRIMARY)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to maintenance after " NODE_FORMAT
			" got assigned %s as goal state.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode),
			ReplicationStateGetName(primaryNode->goalState));

		/* secondary reached maintenance */
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
			"Setting goal state of " NODE_FORMAT
			" to stop_replication after " NODE_FORMAT
			" converged to prepare_maintenance.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* promote the secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_STOP_REPLICATION, message);

		return true;
	}

	/*
	 * when a worker blocked writes:
	 *   prepare_promotion -> wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		primaryNode &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to wait_primary and " NODE_FORMAT
			" to demoted after the coordinator metadata was updated.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when a worker blocked writes and the primary has been removed:
	 *   prepare_promotion -> wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_PREPARE_PROMOTION) &&
		primaryNode == NULL &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to wait_primary after the coordinator metadata was updated.",
			NODE_FORMAT_ARGS(activeNode));

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

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
			"Setting goal state of " NODE_FORMAT
			" to demote_timeout and " NODE_FORMAT
			" to stop_replication after " NODE_FORMAT
			" converged to prepare_promotion.",
			NODE_FORMAT_ARGS(primaryNode),
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(activeNode));

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
			"Setting goal state of " NODE_FORMAT
			" to wait_primary after " NODE_FORMAT
			" converged to prepare_promotion.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(activeNode));

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
			"Setting goal state of " NODE_FORMAT
			" to wait_primary and " NODE_FORMAT
			" to maintenance.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

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
			"Setting goal state of " NODE_FORMAT
			" to wait_primary and " NODE_FORMAT
			" to demoted after the demote timeout expired.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

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
		primaryNode &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to wait_primary and " NODE_FORMAT
			" to demoted after the coordinator metadata was updated.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		/* done draining, node is presumed dead */
		AssignGoalState(primaryNode, REPLICATION_STATE_DEMOTED, message);

		return true;
	}

	/*
	 * when a worker blocked writes, and the primary has been dropped:
	 *   stop_replication -> wait_primary
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_STOP_REPLICATION) &&
		primaryNode == NULL &&
		IsCitusFormation(formation) && activeNode->groupId > 0)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to wait_primary after the coordinator metadata was updated.",
			NODE_FORMAT_ARGS(activeNode));

		/* node is now taking writes */
		AssignGoalState(activeNode, REPLICATION_STATE_WAIT_PRIMARY, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  demoted -> catchingup
	 *
	 * We accept to move from demoted to catching up as soon as the primary
	 * node is has reported either wait_primary or join_primary, and even when
	 * it's already transitioning to primary, thanks to another standby
	 * concurrently making progress.
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsHealthy(primaryNode) &&
		((primaryNode->reportedState == REPLICATION_STATE_WAIT_PRIMARY ||
		  primaryNode->reportedState == REPLICATION_STATE_JOIN_PRIMARY) &&
		 primaryNode->goalState == REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup after it converged to demotion and " NODE_FORMAT
			" converged to primary.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_CATCHINGUP, message);

		return true;
	}

	/*
	 * when a new primary is ready:
	 *  demoted -> catchingup
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_DEMOTED) &&
		IsHealthy(primaryNode) &&
		(IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		 IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY)))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup after it converged to demotion and " NODE_FORMAT
			" converged to %s.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode),
			ReplicationStateGetName(primaryNode->reportedState));

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
	 *
	 * The primary could be in one of those states:
	 *  - wait_primary/wait_primary
	 *  - wait_primary/primary
	 *
	 * This transition also happens when a former primary node has been
	 * demoted, and a multiple standbys has taken effect, we have a new primary
	 * being promoted, and several standby nodes following the new primary.
	 *
	 */
	if (IsCurrentState(activeNode, REPLICATION_STATE_JOIN_SECONDARY) &&
		primaryNode->reportedState == REPLICATION_STATE_WAIT_PRIMARY &&
		(primaryNode->goalState == REPLICATION_STATE_WAIT_PRIMARY ||
		 primaryNode->goalState == REPLICATION_STATE_PRIMARY))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to secondary after " NODE_FORMAT
			" converged to wait_primary.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

		/* it's safe to rejoin as a secondary */
		AssignGoalState(activeNode, REPLICATION_STATE_SECONDARY, message);

		/* compute next step for the primary depending on node settings */
		return ProceedGroupStateForPrimaryNode(primaryNode);
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
			"Setting goal state of " NODE_FORMAT
			" to secondary after " NODE_FORMAT
			" converged to primary.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(primaryNode));

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
					"Setting goal state of " NODE_FORMAT
					" to wait_primary after " NODE_FORMAT
					" joined.",
					NODE_FORMAT_ARGS(primaryNode),
					NODE_FORMAT_ARGS(otherNode));

				/* prepare replication slot and pg_hba.conf */
				AssignGoalState(primaryNode,
								REPLICATION_STATE_WAIT_PRIMARY,
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
	 *
	 * Also we might lose a standby node while already in WAIT_PRIMARY, when
	 * all the left standby nodes are assigned a candidatePriority of zero.
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) ||
		IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) ||
		IsCurrentState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS))
	{
		/*
		 * We count our nodes in different ways, because of special cases we
		 * want to be able to address. We want to distinguish nodes that are in
		 * the replication quorum, nodes that are secondary, and nodes that are
		 * secondary but do not participate in the quorum.
		 *
		 * - replicationQuorumCount is the count of nodes with
		 *   replicationQuorum true, whether or not those nodes are currently
		 *   in the SECONDARY state.
		 *
		 * - secondaryNodesCount is the count of nodes that are currently in
		 *   the SECONDARY state.
		 *
		 * - secondaryQuorumNodesCount is the count of nodes that are both
		 *   setup to participate in the replication quorum and also currently
		 *   in the SECONDARY state.
		 */
		int replicationQuorumCount = otherNodesCount;
		int secondaryNodesCount = otherNodesCount;
		int secondaryQuorumNodesCount = otherNodesCount;

		AutoFailoverFormation *formation =
			GetFormation(primaryNode->formationId);
		ListCell *nodeCell = NULL;

		foreach(nodeCell, otherNodesGroupList)
		{
			AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

			/*
			 * We force secondary nodes to catching-up even if the node is on
			 * its way to being a secondary... unless it is currently in the
			 * reportLSN or join_secondary state, because in those states
			 * Postgres is stopped, waiting for the new primary to be
			 * available.
			 */
			if (otherNode->goalState == REPLICATION_STATE_SECONDARY &&
				otherNode->reportedState != REPLICATION_STATE_REPORT_LSN &&
				otherNode->reportedState != REPLICATION_STATE_JOIN_SECONDARY &&
				IsUnhealthy(otherNode))
			{
				char message[BUFSIZE];

				--secondaryNodesCount;
				--secondaryQuorumNodesCount;

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of " NODE_FORMAT
					" to catchingup after it became unhealthy.",
					NODE_FORMAT_ARGS(otherNode));

				/* other node is behind, no longer eligible for promotion */
				AssignGoalState(otherNode,
								REPLICATION_STATE_CATCHINGUP, message);
			}
			else if (!IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY))
			{
				--secondaryNodesCount;
				--secondaryQuorumNodesCount;
			}

			/* at this point we are left with nodes in SECONDARY state */
			else if (IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY) &&
					 !otherNode->replicationQuorum)
			{
				--secondaryQuorumNodesCount;
			}

			/* now separately count nodes setup with replication quorum */
			if (!otherNode->replicationQuorum)
			{
				--replicationQuorumCount;
			}
		}

		/*
		 * Special case first: when given a setup where all the nodes are async
		 * (replicationQuorumCount == 0) we allow the "primary" state in almost
		 * all cases, knowing that synchronous_standby_names is still going to
		 * be computed as ''.
		 *
		 * That said, if we don't have a single node in the SECONDARY state, we
		 * still want to switch to WAIT_PRIMARY to show that something
		 * unexpected is happening.
		 */
		if (replicationQuorumCount == 0)
		{
			Assert(formation->number_sync_standbys == 0);

			ReplicationState primaryGoalState =
				secondaryNodesCount == 0
				? REPLICATION_STATE_WAIT_PRIMARY
				: REPLICATION_STATE_PRIMARY;

			if (primaryNode->goalState != primaryGoalState)
			{
				char message[BUFSIZE] = { 0 };

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of " NODE_FORMAT
					" to %s because none of the secondary nodes"
					" are healthy at the moment.",
					NODE_FORMAT_ARGS(primaryNode),
					ReplicationStateGetName(primaryGoalState));

				AssignGoalState(primaryNode, primaryGoalState, message);

				return true;
			}

			/* when all nodes are async, we're done here */
			return true;
		}

		/*
		 * Disable synchronous replication to maintain availability.
		 *
		 * Note that we implement here a trade-off between availability (of
		 * writes) against durability of the written data. In the case when
		 * there's a single standby in the group, pg_auto_failover choice is to
		 * maintain availability of the service, including writes.
		 *
		 * In the case when the user has setup a replication quorum of 1 or
		 * more, then pg_auto_failover does not get in the way. You get what
		 * you ask for, which is a strong guarantee on durability.
		 *
		 * To have number_sync_standbys == 1, you need to have at least 2
		 * standby servers. To get to a point where writes are not possible
		 * anymore, there needs to be a point in time where 2 of the 2 standby
		 * nodes are unavailable. In that case, pg_auto_failover does not
		 * change the configured trade-offs. Writes are blocked until one of
		 * the two defective standby nodes is available again.
		 */
		if (!IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) &&
			secondaryQuorumNodesCount == 0)
		{
			/*
			 * Allow wait_primary when number_sync_standbys = 0, otherwise
			 * block writes on the primary.
			 */
			ReplicationState primaryGoalState =
				formation->number_sync_standbys == 0
				? REPLICATION_STATE_WAIT_PRIMARY
				: REPLICATION_STATE_PRIMARY;

			if (primaryNode->goalState != primaryGoalState)
			{
				char message[BUFSIZE] = { 0 };

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of " NODE_FORMAT
					" to %s because none of the standby nodes in the quorum"
					" are healthy at the moment.",
					NODE_FORMAT_ARGS(primaryNode),
					ReplicationStateGetName(primaryGoalState));

				AssignGoalState(primaryNode, primaryGoalState, message);

				return true;
			}
		}

		/*
		 * when a node is wait_primary and has at least one healthy candidate
		 * secondary
		 *     wait_primary ➜ primary
		 */
		if (IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) &&
			secondaryQuorumNodesCount > 0)
		{
			char message[BUFSIZE] = { 0 };

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to primary now that we have %d healthy "
				" secondary nodes in the quorum.",
				NODE_FORMAT_ARGS(primaryNode),
				secondaryQuorumNodesCount);

			AssignGoalState(primaryNode, REPLICATION_STATE_PRIMARY, message);

			return true;
		}

		/*
		 * when a node has changed its replication settings:
		 *     apply_settings ➜ wait_primary
		 *     apply_settings ➜ primary
		 *
		 * Even when we don't currently have healthy standby nodes to failover
		 * to, if the number_sync_standbys is greater than zero that means the
		 * user wants to block writes on the primary, and we do that by
		 * switching to the primary state after having applied replication
		 * settings. Think
		 *
		 *  $ pg_autoctl set formation number-sync-standbys 1
		 *
		 * during an incident to stop the amount of potential data loss.
		 *
		 */
		if (IsCurrentState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS))
		{
			char message[BUFSIZE] = { 0 };

			ReplicationState primaryGoalState =
				formation->number_sync_standbys == 0 &&
				secondaryQuorumNodesCount == 0
				? REPLICATION_STATE_WAIT_PRIMARY
				: REPLICATION_STATE_PRIMARY;

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to %s after it applied replication properties change.",
				NODE_FORMAT_ARGS(primaryNode),
				ReplicationStateGetName(primaryGoalState));

			AssignGoalState(primaryNode, primaryGoalState, message);

			return true;
		}

		return true;
	}

	/*
	 * We don't use the join_primary state any more, though for backwards
	 * compatibility if a node reports JOIN_PRIMARY well then we assign PRIMARY
	 * to the node. After all it might be that an operator upgrades while a
	 * node is in JOIN_PRIMARY and we certainly want to be able to handle the
	 * situation.
	 */
	if (IsCurrentState(primaryNode, REPLICATION_STATE_JOIN_PRIMARY))
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT " to primary",
			NODE_FORMAT_ARGS(primaryNode));

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
		char message[BUFSIZE] = { 0 };

		List *knownUnreachableStates =
			list_make2_int(REPLICATION_STATE_REPORT_LSN,
						   REPLICATION_STATE_PREPARE_PROMOTION);

		/* activeNode might be the failover candidate, proceed already */
		if (nodeBeingPromoted->nodeId == activeNode->nodeId)
		{
			return ProceedWithMSFailover(activeNode, nodeBeingPromoted);
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Active " NODE_FORMAT
			" found failover candidate " NODE_FORMAT
			" being promoted (currently \"%s\"/\"%s\")",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(nodeBeingPromoted),
			ReplicationStateGetName(nodeBeingPromoted->reportedState),
			ReplicationStateGetName(nodeBeingPromoted->goalState));

		/*
		 * The currently selected node might not be marked healthy at this time
		 * because in REPORT_LSN we shut Postgres down. We still should proceed
		 * with the previously selected node in that case.
		 *
		 * We really need to avoid having two candidates at the same time, and
		 * again, at prepare_promotion point Postgres might not have been
		 * started yet.
		 */
		if (IsStateIn(nodeBeingPromoted->reportedState, knownUnreachableStates) ||
			IsHealthy(nodeBeingPromoted))
		{
			elog(LOG, "Found candidate " NODE_FORMAT,
				 NODE_FORMAT_ARGS(nodeBeingPromoted));

			return ProceedWithMSFailover(activeNode, nodeBeingPromoted);
		}
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
	char *formationId = activeNode->formationId;
	AutoFailoverFormation *formation = GetFormation(formationId);

	candidateList.numberSyncStandbys = formation->number_sync_standbys;

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
			"activeNode is " NODE_FORMAT
			" and reported state \"%s\"",
			candidateList.candidateCount,
			candidateList.missingNodesCount,
			NODE_FORMAT_ARGS(activeNode),
			ReplicationStateGetName(activeNode->reportedState));

		return false;
	}

	/*
	 * So all the expected candidates did report their LSN, no node is missing.
	 * Let's see about selecting a candidate for failover now, when we do have
	 * candidates.
	 *
	 * To start the selection process, we require at least number_sync_standbys
	 * nodes to have reported their LSN and be currently healthy, otherwise we
	 * won't be able to maintain our guarantees: we would end-up with a node in
	 * WAIT_PRIMARY state with all the writes blocked for lack of standby
	 * nodes.
	 */
	int minCandidates = formation->number_sync_standbys + 1;

	/* no candidates is a hard pass */
	if (candidateList.candidateCount == 0)
	{
		return false;
	}

	/* not enough candidates to promote and then accept writes, pass */
	else if (candidateList.quorumCandidateCount < minCandidates)
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Failover still in progress with %d candidates that participate "
			"in the quorum having reported their LSN: %d nodes are required "
			"in the quorum to satisfy number_sync_standbys=%d in "
			"formation \"%s\", activeNode is " NODE_FORMAT
			" and reported state \"%s\"",
			candidateList.quorumCandidateCount,
			minCandidates,
			formation->number_sync_standbys,
			formation->formationId,
			NODE_FORMAT_ARGS(activeNode),
			ReplicationStateGetName(activeNode->reportedState));

		return false;
	}

	/* enough candidates to promote and then accept writes, let's do it! */
	else
	{
		/* build the list of most advanced standby nodes, not ordered */
		List *mostAdvancedNodeList =
			ListMostAdvancedStandbyNodes(nodesGroupList);

		/* select a node to failover to */

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

			LogAndNotifyMessage(
				message, BUFSIZE,
				"The current most advanced reported LSN is %X/%X, "
				"as reported by " NODE_FORMAT
				" and %d other nodes",
				(uint32) (mostAdvancedNode->reportedLSN >> 32),
				(uint32) mostAdvancedNode->reportedLSN,
				NODE_FORMAT_ARGS(mostAdvancedNode),
				list_length(mostAdvancedNodeList) - 1);
		}
		else
		{
			ereport(ERROR, (errmsg("BUG: mostAdvancedNodeList is empty")));
		}

		AutoFailoverNode *selectedNode =
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
				"activeNode is " NODE_FORMAT
				" and reported state \"%s\"",
				candidateList.candidateCount,
				NODE_FORMAT_ARGS(activeNode),
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
 * already reported their LSN, and sets nodes that should be reporting to the
 * REPORT_LSN goal state.
 *
 * A CandidateList keeps track of the list of candidate nodes, the list of most
 * advanced nodes (in terms of LSN positions), and two counters, the count of
 * candidate nodes (that's the length of the first list) and the count of nodes
 * that are due to report their LSN but didn't yet, named the
 * missingNodesCount.
 *
 * Managing the missingNodesCount allows a better message to be printed by the
 * monitor and prevents early failover: when missingNodesCount > 0 then the
 * caller for BuildCandidateList knows to refrain from any decision making.
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

		/*
		 * Skip old and new primary nodes (if a selection has been made).
		 *
		 * When a failover is ongoing, a former primary node that has reached
		 * DRAINING and is reporting should be asked to report their LSN.
		 */
		if ((IsInPrimaryState(node) ||
			 IsBeingDemotedPrimary(node) ||
			 IsDemotedPrimary(node)) &&
			!(IsCurrentState(node, REPLICATION_STATE_DRAINING) ||
			  IsCurrentState(node, REPLICATION_STATE_DEMOTED)))
		{
			elog(LOG,
				 "Skipping candidate " NODE_FORMAT
				 ", which is a primary (old or new)",
				 NODE_FORMAT_ARGS(node));
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
				 "Skipping candidate " NODE_FORMAT ", which is unhealthy",
				 NODE_FORMAT_ARGS(node));

			/*
			 * When a secondary node is now down, and had already reported its
			 * LSN, then it's not "missing": we have its LSN and are able to
			 * continue with the election mechanism.
			 *
			 * Otherwise, we didn't get its LSN and this node might be (one of)
			 * the most advanced LSN. Picking it now might lead to loosing
			 * commited data that was reported to the client connection, if
			 * this node is the only one with the most advanted LSN.
			 *
			 * Only the nodes that participate in the quorum are required to
			 * report their LSN, because only those nodes are waited by
			 * Postgres to report a commit to the client connection.
			 */
			if (node->replicationQuorum &&
				node->reportedState != REPLICATION_STATE_REPORT_LSN)
			{
				++(candidateList->missingNodesCount);
			}

			continue;
		}

		/*
		 * Grab healthy standby nodes which have reached REPORT_LSN.
		 */
		if (IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
		{
			candidateNodesGroupList = lappend(candidateNodesGroupList, node);

			/* when number_sync_standbys is zero, quorum isn't discriminant */
			if (node->replicationQuorum ||
				candidateList->numberSyncStandbys == 0)
			{
				++(candidateList->quorumCandidateCount);
			}

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
		 * their LSN. Also old primary nodes in DEMOTED state are due to report
		 * now. And also old primary nodes in DRAINING state, when the drain
		 * timeout is over, are due to report.
		 *
		 * When a node has been asked to re-join the group after a maintenance
		 * period, and been assigned catching-up but failed to connect to the
		 * primary, and a failover now happens, we need that node to join the
		 * REPORT_LSN crew.
		 *
		 * Finally, another interesting case for us here would be a node that
		 * has been asked to re-join a newly elected primary, but the newly
		 * elected primary has now failed and we're in the election process to
		 * replace it. Then demoted/catchingup has been assigned, but there is
		 * no primary to catch-up to anymore, join the REPORT_LSN crew.
		 */
		if ((IsStateIn(node->reportedState, secondaryStates) &&
			 IsStateIn(node->goalState, secondaryStates)) ||
			(node->reportedState == REPLICATION_STATE_MAINTENANCE &&
			 node->goalState == REPLICATION_STATE_CATCHINGUP) ||
			((IsCurrentState(node, REPLICATION_STATE_DRAINING) ||
			  IsCurrentState(node, REPLICATION_STATE_DEMOTED) ||
			  (node->reportedState == REPLICATION_STATE_DEMOTED &&
			   node->goalState == REPLICATION_STATE_CATCHINGUP))))
		{
			char message[BUFSIZE] = { 0 };

			++(candidateList->missingNodesCount);

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to report_lsn to find the failover candidate",
				NODE_FORMAT_ARGS(node));

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
		CandidateNodeIsReadyToStreamWAL(candidateNode))
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to join_secondary after " NODE_FORMAT
			" got selected as the failover candidate.",
			NODE_FORMAT_ARGS(activeNode),
			NODE_FORMAT_ARGS(candidateNode));

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
	/*
	 * Build the list of failover candidate nodes, ordered by priority.
	 * Nodes with candidatePriority == 0 are skipped in GroupListCandidates.
	 */
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
			"One of the most advanced standby nodes in the group "
			"is " NODE_FORMAT
			"with reported LSN %X/%X, which is more than "
			"pgautofailover.enable_sync_wal_log_threshold (%d) behind "
			"the primary " NODE_FORMAT
			", which has reported %X/%X",
			NODE_FORMAT_ARGS(mostAdvancedNode),
			(uint32) (mostAdvancedNode->reportedLSN >> 32),
			(uint32) mostAdvancedNode->reportedLSN,
			PromoteXlogThreshold,
			NODE_FORMAT_ARGS(primaryNode),
			(uint32) (primaryNode->reportedLSN >> 32),
			(uint32) primaryNode->reportedLSN);

		return NULL;
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
				"Not selecting failover candidate " NODE_FORMAT
				"because it is unhealthy",
				NODE_FORMAT_ARGS(node));

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
				"The selected candidate " NODE_FORMAT
				" needs to fetch missing "
				"WAL to reach LSN %X/%X (from current reported LSN %X/%X) "
				"and none of the most advanced standby nodes are healthy "
				"at the moment.",
				NODE_FORMAT_ARGS(selectedNode),
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
	/* selectedNode can't be NULL here */
	if (selectedNode == NULL)
	{
		ereport(ERROR,
				(errmsg("BUG: selectedNode is NULL in PromoteSelectedNode")));
	}

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

		selectedNode->candidatePriority -= CANDIDATE_PRIORITY_INCREMENT;

		ReportAutoFailoverNodeReplicationSetting(
			selectedNode->nodeId,
			selectedNode->nodeHost,
			selectedNode->nodePort,
			selectedNode->candidatePriority,
			selectedNode->replicationQuorum);

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Updating candidate priority back to %d for " NODE_FORMAT,
			selectedNode->candidatePriority,
			NODE_FORMAT_ARGS(selectedNode));

		NotifyStateChange(selectedNode, message);
	}

	/*
	 * When a failover is performed with all the nodes up and running, we tweak
	 * the priority of the primary in a way that prevents its re-election. Now
	 * that the election is done, it's time to reset the primary priority back
	 * to its former value.
	 *
	 * As the primaryNode parameter might be NULL, we loop over all the
	 * candidates and reset any negative priority found in the list.
	 */
	if (candidateList->candidateNodesGroupList != NULL)
	{
		ListCell *nodeCell = NULL;

		foreach(nodeCell, candidateList->candidateNodesGroupList)
		{
			AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

			if (node == NULL)
			{
				/* shouldn't happen */
				ereport(ERROR, (errmsg("BUG: node is NULL")));
				continue;
			}

			if (node->candidatePriority < 0)
			{
				char message[BUFSIZE] = { 0 };

				node->candidatePriority += CANDIDATE_PRIORITY_INCREMENT;

				ReportAutoFailoverNodeReplicationSetting(
					node->nodeId,
					node->nodeHost,
					node->nodePort,
					node->candidatePriority,
					node->replicationQuorum);

				LogAndNotifyMessage(
					message, BUFSIZE,
					"Updating candidate priority back to %d for " NODE_FORMAT,
					node->candidatePriority,
					NODE_FORMAT_ARGS(node));

				NotifyStateChange(node, message);
			}
		}
	}

	if (selectedNode->reportedLSN == candidateList->mostAdvancedReportedLSN)
	{
		char message[BUFSIZE] = { 0 };

		if (primaryNode)
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to prepare_promotion after " NODE_FORMAT
				" became unhealthy and %d nodes reported their LSN position.",
				NODE_FORMAT_ARGS(selectedNode),
				NODE_FORMAT_ARGS(primaryNode),
				candidateList->candidateCount);
		}
		else
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to prepare_promotion and %d nodes reported their LSN position.",
				NODE_FORMAT_ARGS(selectedNode),
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
				"Setting goal state of " NODE_FORMAT
				" to fast_forward after " NODE_FORMAT
				" became unhealthy and %d nodes reported their LSN position.",
				NODE_FORMAT_ARGS(selectedNode),
				NODE_FORMAT_ARGS(primaryNode),
				candidateList->candidateCount);
		}
		else
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to fast_forward after %d nodes reported their LSN position.",
				NODE_FORMAT_ARGS(selectedNode),
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
 * neither node has reported a relative xlog position.
 *
 * Returns false when the nodes are not on the same reported timeline.
 */
static bool
WalDifferenceWithin(AutoFailoverNode *secondaryNode,
					AutoFailoverNode *otherNode, int64 delta)
{
	if (secondaryNode == NULL || otherNode == NULL)
	{
		return true;
	}

	XLogRecPtr secondaryLsn = secondaryNode->reportedLSN;
	XLogRecPtr otherNodeLsn = otherNode->reportedLSN;

	if (secondaryLsn == 0 || otherNodeLsn == 0)
	{
		/* we don't have any data yet */
		return false;
	}

	int64 walDifference = Abs(otherNodeLsn - secondaryLsn);

	return walDifference <= delta;
}
