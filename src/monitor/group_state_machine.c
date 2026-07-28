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
#include "metadata.h"
#include "node_metadata.h"
#include "notifications.h"
#include "replication_state.h"
#include "timeline_history.h"
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
static bool ProceedGroupStateForMSFailover(GroupStateContext *ctx,
										   AutoFailoverNode *primaryNode);
static bool ProceedWithMSFailover(AutoFailoverNode *activeNode,
								  AutoFailoverNode *candidateNode);

static bool BuildCandidateList(GroupStateContext *ctx,
							   List *standbyNodesGroupList,
							   CandidateList *candidateList);

static AutoFailoverNode * SelectFailoverCandidateNode(GroupStateContext *ctx,
													  CandidateList *candidateList,
													  AutoFailoverNode *primaryNode);

static bool PromoteSelectedNode(AutoFailoverNode *selectedNode,
								AutoFailoverNode *primaryNode,
								CandidateList *candidateList);

static void AssignGoalState(AutoFailoverNode *pgAutoFailoverNode,
							ReplicationState state, char *description);
static bool WalDifferenceWithin(AutoFailoverNode *secondaryNode,
								AutoFailoverNode *primaryNode,
								int64 delta);

/*
 * ---------------------------------------------------------------------
 * Declarative dispatch for ProceedGroupStateFromContext(): its own
 * sequential if-chain, and the sequential if-chain that used to be a
 * separate ProceedGroupStateForPrimaryNode() function, are both replaced by
 * one table of MonitorFSMTransition rows (MonitorFSM[] below), matched
 * first-match-wins by RuleMatches(). ProceedGroupStateForMSFailover() and
 * everything it calls (BuildCandidateList, SelectFailoverCandidateNode,
 * PromoteSelectedNode, ProceedWithMSFailover, WalSourceNodesAreAllUnhealthy)
 * stays hand-written C exactly as before, reached from the table via
 * extraAction -- the candidate-selection algorithm (priority sort, LSN
 * comparison, WAL-fetch orchestration) doesn't reduce to declarative
 * conditions any more cleanly than it did before this change.
 * ---------------------------------------------------------------------
 */

typedef enum BoolPattern
{
	BOOL_ANY = 0,
	BOOL_FALSE,
	BOOL_TRUE
} BoolPattern;

static bool
MatchBoolPattern(bool actual, BoolPattern pattern)
{
	switch (pattern)
	{
		case BOOL_FALSE:
		{
			return !actual;
		}

		case BOOL_TRUE:
		{
			return actual;
		}

		case BOOL_ANY:
		default:
		{
			return true;
		}
	}
}


typedef enum NodeStatePatternKind
{
	NODE_STATE_ANY = 0,
	NODE_STATE_STABLE,
	NODE_STATE_NOT_STABLE,
	NODE_STATE_REPORTED,
	NODE_STATE_ASSIGNED,
	NODE_STATE_NOT_ASSIGNED,
	NODE_STATE_TRANSITIONING
} NodeStatePatternKind;

/* Fixed-size (not pointer-based): a pointer initialized from the address of
 * a compound literal nested inside another compound literal is not a
 * constant expression by the C standard -- clang accepts it as an extension
 * in file-scope initializers, but gcc (used by the project's Debian/CI
 * build) rejects it with "initializer element is not constant". A fixed-size
 * value member sidesteps the whole address-of-compound-literal question:
 * no STATES() call in this file passes more than 3 states.
 */
typedef struct ReplicationStateSet
{
	ReplicationState states[4];
	int count;
} ReplicationStateSet;

#define STATES_NARG_(_1, _2, _3, _4, N, ...) N
#define STATES_NARG(...) STATES_NARG_(__VA_ARGS__, 4, 3, 2, 1)

/* Builds a { states[4], count } pair inline -- no NONE-terminated array to
 * remember to close, no separate _STATES[] declaration to name. Deliberately
 * NOT wrapped in a "(ReplicationStateSet) { ... }" compound-literal cast:
 * every use is as a designated-initializer value nested inside another
 * static aggregate (a MonitorFSMTransition table row, or one of the named
 * FSM_* pattern constants below), and a bare brace-list is a plain
 * sub-object initializer there -- fully portable ISO C. A compound-literal
 * cast would make it a distinct object in its own right, and gcc (unlike
 * clang) rejects using one of those, even by value, to initialize part of
 * another object with static storage duration ("initializer element is not
 * constant") -- see the note on ReplicationStateSet above for the same
 * problem one level down.
 */
#define STATES(...) \
	{ \
		 .states = { __VA_ARGS__ }, \
		 .count = STATES_NARG(__VA_ARGS__) \
	}

typedef struct NodeStatePattern
{
	NodeStatePatternKind kind;
	ReplicationStateSet reportedStates;
	ReplicationStateSet assignedStates;
} NodeStatePattern;

static bool
MatchStateSet(ReplicationState actual, ReplicationStateSet declared)
{
	for (int i = 0; i < declared.count; i++)
	{
		if (declared.states[i] == actual)
		{
			return true;
		}
	}
	return false;
}


/* Same reasoning as STATES() above: no compound-literal cast, since every
 * use is nested inside another static aggregate's designated initializer
 * (e.g. ".statePattern = FSM_STATE(x)" inside a MonitorFSMTransition row). */
#define FSM_STATE(x) \
	{ .kind = NODE_STATE_STABLE, .reportedStates = STATES(x) }

/* group_state_machine.c:504-523/1059-1106 -- three IsCurrentState(primaryNode, X) ORed */
static const NodeStatePattern FSM_PRIMARY_OR_WAIT_OR_JOIN = {
	.kind = NODE_STATE_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY,
							  REPLICATION_STATE_JOIN_PRIMARY,
							  REPLICATION_STATE_PRIMARY),
};

/* WAIT_PRIMARY/JOIN_PRIMARY only, not PRIMARY -- a distinct, narrower set from the one above */
static const NodeStatePattern FSM_WAIT_OR_JOIN_PRIMARY = {
	.kind = NODE_STATE_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY,
							  REPLICATION_STATE_JOIN_PRIMARY),
};

/* the "primary role" states inside ProceedGroupStateForPrimaryNode -- a different
 * three-element set from FSM_PRIMARY_OR_WAIT_OR_JOIN above (no JOIN_PRIMARY, has APPLY_SETTINGS) */
static const NodeStatePattern FSM_PRIMARY_ROLE_STATES = {
	.kind = NODE_STATE_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_PRIMARY,
							  REPLICATION_STATE_WAIT_PRIMARY,
							  REPLICATION_STATE_APPLY_SETTINGS),
};

/* same "primary role" scope, minus WAIT_PRIMARY -- a narrower enumerated STABLE set */
static const NodeStatePattern FSM_PRIMARY_OR_APPLY_SETTINGS_ONLY = {
	.kind = NODE_STATE_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_PRIMARY,
							  REPLICATION_STATE_APPLY_SETTINGS),
};

/* reported WAIT_PRIMARY, goal in {WAIT_PRIMARY, PRIMARY} -- join_secondary's cascade row */
static const NodeStatePattern FSM_WAIT_PRIMARY_TRANSITIONING_TO_PRIMARY = {
	.kind = NODE_STATE_TRANSITIONING,
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY),
	.assignedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY, REPLICATION_STATE_PRIMARY),
};

/* reported in {WAIT_PRIMARY,JOIN_PRIMARY}, goal PRIMARY -- demoted->catchingup, first disjunct */
static const NodeStatePattern FSM_WAIT_OR_JOIN_PRIMARY_TRANSITIONING_TO_PRIMARY = {
	.kind = NODE_STATE_TRANSITIONING,
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY, REPLICATION_STATE_JOIN_PRIMARY),
	.assignedStates = STATES(REPLICATION_STATE_PRIMARY),
};

/* goalState != WAIT_PRIMARY, reportedState irrelevant -- wait_maintenance's second row */
static const NodeStatePattern FSM_NOT_ASSIGNED_WAIT_PRIMARY = {
	.kind = NODE_STATE_NOT_ASSIGNED,
	.assignedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY),
};

/* !IsCurrentState(node, WAIT_PRIMARY): not converged to wait_primary, for any reason */
static const NodeStatePattern FSM_NOT_STABLE_WAIT_PRIMARY = {
	.kind = NODE_STATE_NOT_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY),
};

/* !IsCurrentState(node, SINGLE) */
static const NodeStatePattern FSM_NOT_STABLE_SINGLE = {
	.kind = NODE_STATE_NOT_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_SINGLE),
};

/* goalState == DROPPED, reportedState irrelevant */
static const NodeStatePattern FSM_DROPPED_GOAL = {
	.kind = NODE_STATE_ASSIGNED,
	.assignedStates = STATES(REPLICATION_STATE_DROPPED),
};

/* reportedState == DEMOTE_TIMEOUT, goalState irrelevant. NOT FSM_STATE(DEMOTE_TIMEOUT), which
 * would also require goalState == DEMOTE_TIMEOUT -- the opposite of what this self-fence guard
 * needs: it must catch a node whose goalState is still whatever was assigned before the
 * self-fence fired (see the real comment on this guard, preserved below). */
static const NodeStatePattern FSM_REPORTED_DEMOTE_TIMEOUT = {
	.kind = NODE_STATE_REPORTED,
	.reportedStates = STATES(REPLICATION_STATE_DEMOTE_TIMEOUT),
};

/* reportedState in {REPORT_LSN, FAST_FORWARD}, converged -- "continue an already started
 * failover" guard, the direct (non-cascading) entry into ProceedGroupStateForMSFailover */
static const NodeStatePattern FSM_REPORT_LSN_OR_FAST_FORWARD = {
	.kind = NODE_STATE_STABLE,
	.reportedStates = STATES(REPLICATION_STATE_REPORT_LSN, REPLICATION_STATE_FAST_FORWARD),
};


static bool
NodeStateMatchesPattern(const AutoFailoverNode *node, const NodeStatePattern *pattern)
{
	if (node == NULL)
	{
		/* No node this round (no primary). Only NODE_STATE_ANY can still match -- every other
		 * kind needs a real reportedState/goalState to compare, which a nonexistent node simply
		 * doesn't have. Existence itself is checked separately via NodeStatusPattern.exists. */
		return pattern->kind == NODE_STATE_ANY;
	}

	ReplicationState reported = node->reportedState;
	ReplicationState goal = node->goalState;

	switch (pattern->kind)
	{
		case NODE_STATE_ANY:
		{
			return true;
		}

		case NODE_STATE_STABLE:
		{
			return (reported == goal) && MatchStateSet(reported, pattern->reportedStates);
		}

		case NODE_STATE_NOT_STABLE:
		{
			return !((reported == goal) && MatchStateSet(reported, pattern->reportedStates));
		}

		case NODE_STATE_REPORTED:
		{
			return MatchStateSet(reported, pattern->reportedStates);
		}

		case NODE_STATE_ASSIGNED:
		{
			return MatchStateSet(goal, pattern->assignedStates);
		}

		case NODE_STATE_NOT_ASSIGNED:
		{
			return !MatchStateSet(goal, pattern->assignedStates);
		}

		case NODE_STATE_TRANSITIONING:
		{
			return MatchStateSet(reported, pattern->reportedStates) &&
				   MatchStateSet(goal, pattern->assignedStates);
		}

		default:
		{
			return false;
		}
	}
}


/*
 * NodeStatus: every per-node fact this table's conditions need, computed once
 * per role (activeNode, primaryNode) at the top of dispatch by BuildNodeStatus().
 */
typedef struct NodeStatus
{
	AutoFailoverNode *node;
	GroupStateContext *ctx;
	bool isHealthy;
	bool isUnhealthy;
	bool candidateEligible;
	bool isCitusWorkerGroup;
	bool replicationQuorum;
	bool isComparableToReferenceTli;
} NodeStatus;

typedef struct NodeStatusPattern
{
	BoolPattern exists;
	NodeStatePattern statePattern;
	BoolPattern isHealthy;
	BoolPattern isUnhealthy;
	BoolPattern candidateEligible;
	BoolPattern isInPrimaryState;
	BoolPattern isInMaintenance;
	BoolPattern drainTimeExpired;
	BoolPattern isCitusWorkerGroup;
	BoolPattern replicationQuorum;
	BoolPattern isComparableToReferenceTli;
	BoolPattern unreachableFromDemoteTimeout;
} NodeStatusPattern;

static void
BuildNodeStatus(GroupStateContext *ctx, AutoFailoverNode *node, NodeStatus *status)
{
	memset(status, 0, sizeof(NodeStatus));
	status->node = node;
	status->ctx = ctx;

	if (node == NULL)
	{
		/* NodeIsUnhealthy(NULL, ctx) returns true -- a nonexistent node being "unhealthy" is
		 * exactly the semantics the original if-chain relies on. */
		status->isUnhealthy = true;
		return;
	}

	status->isHealthy = NodeIsHealthy(node, ctx);
	status->isUnhealthy = NodeIsUnhealthy(node, ctx);
	status->candidateEligible = node->candidatePriority > 0;
	status->isCitusWorkerGroup = IsCitusFormation(ctx->formation) && node->groupId > 0;
	status->replicationQuorum = node->replicationQuorum;
}


/*
 * isInPrimaryState/isInMaintenance/drainTimeExpired/unreachableFromDemoteTimeout
 * are deliberately NOT cached in NodeStatus and computed live here from
 * status->node instead: unlike isHealthy/isUnhealthy/candidateEligible
 * (health/priority facts that can't change mid-dispatch), these four are
 * pure functions of node->goalState/reportedState, and a matched row's
 * extraAction can reassign the very node being matched (e.g. DRAINING on
 * primaryNode) in the SAME node_active() call, before dispatch continues to
 * a later row -- exactly like NodeStateMatchesPattern below, which reads
 * status->node's fields live for the same reason. Caching these as
 * snapshot booleans (an earlier version of this code did) left later rows
 * in the same call matching against a stale "still in primary state" fact
 * even after primaryNode had just been moved to DRAINING -- confirmed by
 * concurrent_health_check_and_report, which requires the "secondary ->
 * prepare_promotion" row to correctly stop matching once primaryNode is no
 * longer IsInPrimaryState().
 */
static bool
NodeMatchesPattern(const NodeStatus *status, const NodeStatusPattern *pattern)
{
	bool unreachableFromDemoteTimeout =
		status->node != NULL &&
		status->node->goalState != REPLICATION_STATE_DEMOTE_TIMEOUT &&
		status->node->goalState != REPLICATION_STATE_DEMOTED &&
		status->node->goalState != REPLICATION_STATE_PRIMARY &&
		status->node->goalState != REPLICATION_STATE_SINGLE;

	return MatchBoolPattern(status->node != NULL, pattern->exists) &&
		   NodeStateMatchesPattern(status->node, &pattern->statePattern) &&
		   MatchBoolPattern(status->isHealthy, pattern->isHealthy) &&
		   MatchBoolPattern(status->isUnhealthy, pattern->isUnhealthy) &&
		   MatchBoolPattern(status->candidateEligible, pattern->candidateEligible) &&
		   MatchBoolPattern(IsInPrimaryState(status->node), pattern->isInPrimaryState) &&
		   MatchBoolPattern(IsInMaintenance(status->node), pattern->isInMaintenance) &&
		   MatchBoolPattern(NodeIsDrainTimeExpired(status->node, status->ctx),
							 pattern->drainTimeExpired) &&
		   MatchBoolPattern(status->isCitusWorkerGroup, pattern->isCitusWorkerGroup) &&
		   MatchBoolPattern(status->replicationQuorum, pattern->replicationQuorum) &&
		   MatchBoolPattern(status->isComparableToReferenceTli,
							 pattern->isComparableToReferenceTli) &&
		   MatchBoolPattern(unreachableFromDemoteTimeout,
							 pattern->unreachableFromDemoteTimeout);
}


/*
 * NodeActiveContext: group-level facts, computed once per dispatch call
 * alongside the two NodeStatus roles above.
 */
typedef struct NodeActiveContext
{
	NodeStatus activeNode;
	NodeStatus primaryNode;

	bool groupHasExactlyOneNode;
	bool groupHasMoreThanTwoNodes;
	bool anyOtherNodeWaitingStandby;

	bool numberSyncStandbysIsZero;
	bool replicationQuorumCountIsZero;
	bool secondaryNodesCountIsZero;
	bool secondaryQuorumNodesCountIsZero;
	bool atLeastOneHealthyCandidate;

	bool walWithinPromoteThreshold;
	bool walWithinSyncThreshold;
	bool activeAndPrimaryTliMatch;

	bool primaryIsWaitPrimaryPresumedDead;
	bool failoverInProgress;
	bool replicationStallExceeded;
} NodeActiveContext;

typedef struct NodeActiveContextPattern
{
	BoolPattern groupHasExactlyOneNode;
	BoolPattern groupHasMoreThanTwoNodes;
	BoolPattern anyOtherNodeWaitingStandby;

	BoolPattern numberSyncStandbysIsZero;
	BoolPattern replicationQuorumCountIsZero;
	BoolPattern secondaryNodesCountIsZero;
	BoolPattern secondaryQuorumNodesCountIsZero;
	BoolPattern atLeastOneHealthyCandidate;

	BoolPattern walWithinPromoteThreshold;
	BoolPattern walWithinSyncThreshold;
	BoolPattern activeAndPrimaryTliMatch;

	BoolPattern primaryIsWaitPrimaryPresumedDead;
	BoolPattern failoverInProgress;
	BoolPattern replicationStallExceeded;
} NodeActiveContextPattern;


typedef enum GoalStateAssignmentKind
{
	GOAL_STATE_NONE = 0,
	GOAL_STATE_SET
} GoalStateAssignmentKind;

typedef struct GoalStateAssignment
{
	GoalStateAssignmentKind kind;
	ReplicationState state;
} GoalStateAssignment;

/* No compound-literal cast -- see STATES()'s comment: every use nests inside
 * another static aggregate's designated initializer. */
#define GOAL(x) { .kind = GOAL_STATE_SET, .state = (x) }

/*
 * A row's extraAction runs before its own activeNodeAssignedState/
 * otherNodeAssignedState are applied (matching the original if-chain's
 * order). When a row's real-source counterpart falls through to more of the
 * function after its own assignment (the MS-failover cascade, the
 * join_secondary -> nested primary pass), the action itself performs one
 * bounded, explicitly-named nested search+dispatch over MonitorFSM[] --
 * see ActionRunMultiStandbyFailoverCascade and ActionRunPrimaryNodeTransition
 * below -- rather than signaling the top-level driver to keep scanning.
 * There is deliberately no generic "continue dispatch" flag here: an earlier
 * version of this file had extraAction return bool for exactly that purpose,
 * and it reproduced a real bug (a sibling row in the same "family" matching
 * a second time after the intended row declined -- see
 * ActionRunMultiStandbyFailoverCascade's comment) that a bounded, named jump
 * cannot have, because it can only ever land on one specific row family, not
 * wander into whichever row happens to be next.
 */
typedef void (*MonitorExtraActionFunction) (GroupStateContext *ctx,
											 NodeActiveContext *nac,
											 char *message);

typedef struct MonitorFSMTransition
{
	NodeStatusPattern activeNode;
	NodeStatusPattern primaryNode;
	NodeActiveContextPattern conditions;

	GoalStateAssignment activeNodeAssignedState;
	GoalStateAssignment otherNodeAssignedState;  /* target: nac->primaryNode.node */

	MonitorExtraActionFunction extraAction;

	const char *comment;
} MonitorFSMTransition;

/*
 * MonitorFSM[] is one array, not several: see its own definition far below
 * for why ("One array, not three" in the design doc this table implements).
 * Forward-declared here so the extraActions defined above it (which each
 * perform one bounded, named nested search over it) can reference it by
 * name; the boundary constants below are forward-declared the same way, for
 * the same reason -- both actions and the top-level driver need them.
 *
 * These three indices partition MonitorFSM[] into the sections the real
 * if-chain's control flow actually has:
 *
 *   [0, MonitorFSM_FromContextStart)         the six checks
 *                                            ProceedGroupStateFromContext()
 *                                            runs before its one real branch
 *                                            point (IsInPrimaryState(activeNode)),
 *                                            regardless of which way that
 *                                            branch goes.
 *   [MonitorFSM_FromContextStart,             the rest of
 *    MonitorFSM_PrimaryNodeSectionStart)      ProceedGroupStateFromContext(),
 *                                            reached only when activeNode is
 *                                            NOT currently primary-role.
 *   [MonitorFSM_MSFailoverClusterStart,       the sub-range of the row above
 *    MonitorFSM_PrimaryNodeSectionStart)      that ActionRunMultiStandby
 *                                            FailoverCascade resumes into
 *                                            when ProceedGroupStateForMSFailover()
 *                                            declines, mirroring the real
 *                                            source's fallthrough to
 *                                            "whatever is textually next".
 *   [MonitorFSM_PrimaryNodeSectionStart,      ProceedGroupStateForPrimaryNode()'s
 *    MonitorFSM_SIZE)                        own rows, reached either directly
 *                                            by the top-level driver (activeNode
 *                                            already primary-role) or via
 *                                            ActionRunPrimaryNodeTransition's
 *                                            nested pass on primaryNode
 *                                            (join_secondary's cascade row).
 *
 * Kept as plain hardcoded integers, exactly as the design doc's own
 * placeholders are -- recomputed by hand whenever a row is added, removed,
 * or moved across a boundary. A wrong value here fails loudly and
 * immediately (either a compile-time out-of-bounds slice that scans zero
 * rows and never matches, or a row from the wrong section matching
 * unexpectedly) rather than silently: the regress/isolation suite this
 * table is checked against covers every one of these boundaries already.
 */
static const MonitorFSMTransition MonitorFSM[];

#define MonitorFSM_FromContextStart        6
#define MonitorFSM_MSFailoverClusterStart  9
#define MonitorFSM_PrimaryNodeSectionStart 37
#define MonitorFSM_SIZE                    48

/* Forward-declared for the same reason as MonitorFSM[] above: used by
 * extraActions (ActionRunPrimaryNodeTransition) defined before its real
 * definition further down. */
static void BuildForPrimaryNodeNodeActiveContext(GroupStateContext *ctx,
												 AutoFailoverNode *primaryNode,
												 NodeActiveContext *nac);


static bool
RuleMatches(const NodeActiveContext *nac, const MonitorFSMTransition *rule)
{
	const NodeActiveContextPattern *cond = &rule->conditions;

	return NodeMatchesPattern(&nac->activeNode, &rule->activeNode) &&
		   NodeMatchesPattern(&nac->primaryNode, &rule->primaryNode) &&

		   MatchBoolPattern(nac->groupHasExactlyOneNode, cond->groupHasExactlyOneNode) &&
		   MatchBoolPattern(nac->groupHasMoreThanTwoNodes, cond->groupHasMoreThanTwoNodes) &&
		   MatchBoolPattern(nac->anyOtherNodeWaitingStandby, cond->anyOtherNodeWaitingStandby) &&
		   MatchBoolPattern(nac->numberSyncStandbysIsZero, cond->numberSyncStandbysIsZero) &&
		   MatchBoolPattern(nac->replicationQuorumCountIsZero,
							 cond->replicationQuorumCountIsZero) &&
		   MatchBoolPattern(nac->secondaryNodesCountIsZero, cond->secondaryNodesCountIsZero) &&
		   MatchBoolPattern(nac->secondaryQuorumNodesCountIsZero,
							 cond->secondaryQuorumNodesCountIsZero) &&
		   MatchBoolPattern(nac->atLeastOneHealthyCandidate, cond->atLeastOneHealthyCandidate) &&
		   MatchBoolPattern(nac->walWithinPromoteThreshold, cond->walWithinPromoteThreshold) &&
		   MatchBoolPattern(nac->walWithinSyncThreshold, cond->walWithinSyncThreshold) &&
		   MatchBoolPattern(nac->activeAndPrimaryTliMatch, cond->activeAndPrimaryTliMatch) &&
		   MatchBoolPattern(nac->primaryIsWaitPrimaryPresumedDead,
							 cond->primaryIsWaitPrimaryPresumedDead) &&
		   MatchBoolPattern(nac->failoverInProgress, cond->failoverInProgress) &&
		   MatchBoolPattern(nac->replicationStallExceeded, cond->replicationStallExceeded);
}


static int
FindMatchingMonitorFSMRuleIndexFrom(const MonitorFSMTransition table[], int tableSize,
									 int startIndex, const NodeActiveContext *nac)
{
	for (int i = startIndex; i < tableSize; i++)
	{
		if (RuleMatches(nac, &table[i]))
		{
			return i;
		}
	}
	return -1;
}


/*
 * AssignDeclaredGoalState asserts that a rule only ever assigns a state it
 * actually declared: drift between a row's own assignment slots and what it
 * assigns fails loudly (an Assert) instead of silently rotting.
 */
static void
AssignDeclaredGoalState(const MonitorFSMTransition *rule, AutoFailoverNode *node,
						 ReplicationState state, char *message)
{
#ifdef USE_ASSERT_CHECKING
	bool declared =
		(rule->activeNodeAssignedState.kind == GOAL_STATE_SET &&
		 rule->activeNodeAssignedState.state == state) ||
		(rule->otherNodeAssignedState.kind == GOAL_STATE_SET &&
		 rule->otherNodeAssignedState.state == state);

	Assert(declared);
#endif

	AssignGoalState(node, state, message);
}


static void
DispatchMonitorFSMRule(GroupStateContext *ctx, NodeActiveContext *nac,
						const MonitorFSMTransition *rule)
{
	char message[BUFSIZE] = { 0 };

	if (rule->comment != NULL)
	{
		snprintf(message, BUFSIZE, "%s", rule->comment);
	}

	if (rule->extraAction != NULL)
	{
		rule->extraAction(ctx, nac, message);
	}

	if (rule->activeNodeAssignedState.kind == GOAL_STATE_SET)
	{
		AssignDeclaredGoalState(rule, nac->activeNode.node,
								 rule->activeNodeAssignedState.state, message);
	}

	if (rule->otherNodeAssignedState.kind == GOAL_STATE_SET)
	{
		AssignDeclaredGoalState(rule, nac->primaryNode.node,
								 rule->otherNodeAssignedState.state, message);
	}
}


/*
 * FindAndDispatchMonitorFSMRule bounds a search over MonitorFSM[] to
 * [startIndex, endIndex) and dispatches the first match, if any -- the one
 * building block every call site in this file needs: the top-level driver's
 * own two straight-line lookups (early checks, then either the primary-role
 * section or the rest of ProceedGroupStateFromContext()'s rows -- see its
 * comment for why two, not the design doc's one), plus the two extraActions
 * that each perform one further, separate bounded nested search of their own
 * when their row's own cascade declines: ActionRunMultiStandbyFailoverCascade
 * and ActionRunPrimaryNodeTransition below. Returns whether a row matched, so
 * callers that need to distinguish "matched and handled" from "nothing in
 * this range applied" can.
 */
static bool
FindAndDispatchMonitorFSMRule(GroupStateContext *ctx, NodeActiveContext *nac,
							  int startIndex, int endIndex)
{
	int index = FindMatchingMonitorFSMRuleIndexFrom(MonitorFSM, endIndex, startIndex, nac);

	if (index < 0)
	{
		return false;
	}

	DispatchMonitorFSMRule(ctx, nac, &MonitorFSM[index]);

	return true;
}


/* GUC variables */
int EnableSyncXlogThreshold = DEFAULT_XLOG_SEG_SIZE;
int PromoteXlogThreshold = DEFAULT_XLOG_SEG_SIZE;
int ReplicationStallTimeoutMs = 10 * 1000;


/*
 * BuildGroupStateContext loads everything from the database that the
 * node_active FSM needs, capturing a single timestamp snapshot and copying
 * the current GUC values.  Call this once at the top of NodeActive() and pass
 * the resulting context to ProceedGroupStateFromContext().
 *
 * Returns false (and raises an ereport ERROR) when the formation cannot be
 * found.
 */
bool
BuildGroupStateContext(GroupStateContext *ctx, AutoFailoverNode *activeNode)
{
	ctx->formationId = activeNode->formationId;
	ctx->groupId = activeNode->groupId;
	ctx->activeNode = activeNode;
	ctx->formation = GetFormation(activeNode->formationId);
	ctx->groupNodeList =
		AutoFailoverNodeGroup(activeNode->formationId, activeNode->groupId);
	ctx->groupNodeCount = list_length(ctx->groupNodeList);
	ctx->now = GetCurrentTimestamp();
	ctx->unhealthyTimeoutMs = UnhealthyTimeoutMs;
	ctx->drainTimeoutMs = DrainTimeoutMs;
	ctx->startupGracePeriodMs = StartupGracePeriodMs;
	ctx->replicationStallTimeoutMs = ReplicationStallTimeoutMs;

	if (ctx->formation == NULL)
	{
		ereport(ERROR,
				(errmsg("Formation for %s could not be found",
						activeNode->formationId)));
	}

	return true;
}


/*
 * ProceedGroupState proceeds the state machines of the group of which
 * the given node is part.  It builds a GroupStateContext from the database and
 * delegates to ProceedGroupStateFromContext.
 */
bool
ProceedGroupState(AutoFailoverNode *activeNode)
{
	GroupStateContext ctx;

	BuildGroupStateContext(&ctx, activeNode);

	return ProceedGroupStateFromContext(&ctx);
}


/*
 * OtherNodeIsDueForCatchingUp is shared between the count computation in
 * BuildForPrimaryNodeNodeActiveContext() and the fan-out assignment in
 * ActionCatchupUnhealthySecondaries() below, so the two can never drift
 * apart on which nodes they mean by "unhealthy secondary" -- both need to
 * agree, since the counts drive which row matches and the fan-out is that
 * row's own side effect.
 */
static bool
OtherNodeIsDueForCatchingUp(GroupStateContext *ctx, AutoFailoverNode *otherNode)
{
	return otherNode->goalState == REPLICATION_STATE_SECONDARY &&
		   otherNode->reportedState != REPLICATION_STATE_REPORT_LSN &&
		   otherNode->reportedState != REPLICATION_STATE_JOIN_SECONDARY &&
		   NodeIsUnhealthy(otherNode, ctx);
}


static void
ActionRemoveDroppedNode(GroupStateContext *ctx, NodeActiveContext *nac, char *message)
{
	RemoveAutoFailoverNode(ctx->activeNode);
}


/*
 * ActionRunMultiStandbyFailoverCascade implements the whole
 * nodesCount>2-unhealthy-primary block as a single extraAction: the DRAINING/
 * MAINTENANCE/nothing if/else-if decision, followed unconditionally by
 * ProceedGroupStateForMSFailover(). The real source never `return`s after
 * assigning DRAINING/MAINTENANCE to the primary -- it always falls through to
 * try ProceedGroupStateForMSFailover next, in the SAME outer if-block, and if
 * THAT declines (returns false), falls through further still to the rest of
 * ProceedGroupStateFromContext's own if-chain (the report_lsn/prepare_
 * promotion/stop_replication/... rows, for this SAME activeNode).
 *
 * This has to be ONE row/action, not three separate declarative rows sharing
 * this action (as an earlier version of this file had it): once dispatch
 * continues past a declined row, it keeps scanning forward and a later,
 * broader row matching the same outer "nodesCount>2, primary unhealthy"
 * condition (the catch-all "neither DRAINING nor MAINTENANCE applies" case)
 * would match too and re-invoke ProceedGroupStateForMSFailover a *second*
 * time in the same node_active() call -- something the original single-pass
 * if/else-if structure never does. Confirmed by concurrent_second_primary_
 * death_report and concurrent_health_check_and_report, which got stuck (the
 * former) or produced a spurious second cascade invocation changing the
 * outcome (the latter) until this was folded into a single row/action pair.
 *
 * When ProceedGroupStateForMSFailover() declines, the fallthrough to "the
 * rest of ProceedGroupStateFromContext" is a single bounded nested search
 * from MonitorFSM_MSFailoverClusterStart, not a flag back to the top-level
 * driver: FindAndDispatchMonitorFSMRule's own internal loop already finds
 * whichever row is the correct next match, however many rows down that is,
 * in one call -- no repeated re-dispatch needed to walk past intervening
 * non-matches.
 */
static void
ActionRunMultiStandbyFailoverCascade(GroupStateContext *ctx, NodeActiveContext *nac,
									  char *message)
{
	AutoFailoverNode *primaryNode = nac->primaryNode.node;

	List *candidateNodesList =
		AutoFailoverOtherNodesListInState(primaryNode, REPLICATION_STATE_SECONDARY);
	int candidatesCount = CountHealthyCandidates(candidateNodesList);

	if (IsInPrimaryState(primaryNode) &&
		!IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY) &&
		candidatesCount >= 1)
	{
		char drainingMessage[BUFSIZE] = { 0 };

		snprintf(drainingMessage, BUFSIZE,
				 "Setting goal state of " NODE_FORMAT
				 " to draining after it became unhealthy.",
				 NODE_FORMAT_ARGS(primaryNode));

		AssignGoalState(primaryNode, REPLICATION_STATE_DRAINING, drainingMessage);
	}
	else if (IsCurrentState(primaryNode, REPLICATION_STATE_PREPARE_MAINTENANCE))
	{
		char maintenanceMessage[BUFSIZE] = { 0 };

		snprintf(maintenanceMessage, BUFSIZE,
				 "Setting goal state of " NODE_FORMAT
				 " to maintenance after it converged to prepare_maintenance.",
				 NODE_FORMAT_ARGS(primaryNode));

		AssignGoalState(primaryNode, REPLICATION_STATE_MAINTENANCE, maintenanceMessage);
	}

	if (!ProceedGroupStateForMSFailover(ctx, primaryNode))
	{
		(void) FindAndDispatchMonitorFSMRule(ctx, nac, MonitorFSM_MSFailoverClusterStart,
											 MonitorFSM_PrimaryNodeSectionStart);
	}
}


/*
 * ActionRunPlainMSFailoverCascade is the "continue an already-started
 * failover" call site: activeNode itself is REPORT_LSN or FAST_FORWARD, and
 * the real source just `return`s ProceedGroupStateForMSFailover()'s result
 * directly, with no DRAINING/MAINTENANCE decision attached and no further
 * fallthrough either way -- so its return value is simply discarded here.
 */
static void
ActionRunPlainMSFailoverCascade(GroupStateContext *ctx, NodeActiveContext *nac, char *message)
{
	(void) ProceedGroupStateForMSFailover(ctx, nac->primaryNode.node);
}


/*
 * ActionRunPrimaryNodeTransition is the join_secondary cascade: the real
 * source's tail call into ProceedGroupStateForPrimaryNode(ctx, primaryNode)
 * substitutes primaryNode for activeNode and re-enters that function's own
 * dispatch from scratch -- modeled here as one bounded nested search over
 * MonitorFSM[]'s ForPrimaryNode section, built with primaryNode playing the
 * activeNode role (see BuildForPrimaryNodeNodeActiveContext).
 */
static void
ActionRunPrimaryNodeTransition(GroupStateContext *ctx, NodeActiveContext *nac, char *message)
{
	NodeActiveContext primaryNac;

	BuildForPrimaryNodeNodeActiveContext(ctx, nac->primaryNode.node, &primaryNac);

	(void) FindAndDispatchMonitorFSMRule(ctx, &primaryNac, MonitorFSM_PrimaryNodeSectionStart,
										 MonitorFSM_SIZE);
}


static void
ActionCatchupUnhealthySecondaries(GroupStateContext *ctx, NodeActiveContext *nac, char *message)
{
	AutoFailoverNode *primaryNode = nac->activeNode.node;
	List *otherNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
	ListCell *nodeCell = NULL;

	foreach(nodeCell, otherNodesGroupList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (OtherNodeIsDueForCatchingUp(ctx, otherNode))
		{
			char otherMessage[BUFSIZE] = { 0 };

			snprintf(otherMessage, BUFSIZE,
					 "Setting goal state of " NODE_FORMAT
					 " to catchingup after it became unhealthy.",
					 NODE_FORMAT_ARGS(otherNode));

			AssignGoalState(otherNode, REPLICATION_STATE_CATCHINGUP, otherMessage);
		}
	}
}


/*
 * BuildFromContextNodeActiveContext computes every fact MonitorFSM_FromContext
 * needs, mirroring exactly what the original ProceedGroupStateFromContext()
 * if-chain read inline. primaryNode may be NULL (failover already in
 * progress, primary removed).
 */
static void
BuildFromContextNodeActiveContext(GroupStateContext *ctx, AutoFailoverNode *primaryNode,
								  NodeActiveContext *nac)
{
	AutoFailoverNode *activeNode = ctx->activeNode;

	memset(nac, 0, sizeof(NodeActiveContext));

	BuildNodeStatus(ctx, activeNode, &nac->activeNode);
	BuildNodeStatus(ctx, primaryNode, &nac->primaryNode);

	/* isComparableToReferenceTli defaults to true (row :328 doesn't fire) -- a node that hasn't
	 * reported a timeline yet (reportedTLI == 0) has nothing to check, same as the original. */
	nac->activeNode.isComparableToReferenceTli = true;
	if (activeNode->reportedTLI > 0)
	{
		int referenceTli = 0;
		List *comparableNodeList =
			FilterNodesByTimelineAncestry(ctx->groupNodeList, ctx->formationId,
										  ctx->groupId, &referenceTli);

		if (referenceTli > 0)
		{
			bool comparable = false;
			ListCell *cell = NULL;

			foreach(cell, comparableNodeList)
			{
				AutoFailoverNode *node = (AutoFailoverNode *) lfirst(cell);

				if (node->nodeId == activeNode->nodeId)
				{
					comparable = true;
					break;
				}
			}

			nac->activeNode.isComparableToReferenceTli = comparable;
		}
	}

	nac->groupHasExactlyOneNode = (ctx->groupNodeCount == 1);
	nac->groupHasMoreThanTwoNodes = (ctx->groupNodeCount > 2);
	nac->failoverInProgress = IsFailoverInProgress(ctx->groupNodeList);

	nac->activeAndPrimaryTliMatch =
		primaryNode != NULL && activeNode->reportedTLI == primaryNode->reportedTLI;

	nac->walWithinPromoteThreshold =
		WalDifferenceWithin(activeNode, primaryNode, PromoteXlogThreshold);
	nac->walWithinSyncThreshold =
		WalDifferenceWithin(activeNode, primaryNode, EnableSyncXlogThreshold);

	nac->primaryIsWaitPrimaryPresumedDead =
		NodeIsWaitPrimaryPresumedDead(primaryNode, activeNode, ctx);

	nac->replicationStallExceeded =
		primaryNode != NULL &&
		primaryNode->replicationStallSince != 0 &&
		TimestampDifferenceExceeds(primaryNode->replicationStallSince,
								   ctx->now, ctx->replicationStallTimeoutMs);

	if (ctx->groupNodeCount > 2 && nac->primaryNode.isUnhealthy)
	{
		List *candidateNodesList =
			AutoFailoverOtherNodesListInState(primaryNode, REPLICATION_STATE_SECONDARY);

		nac->atLeastOneHealthyCandidate = CountHealthyCandidates(candidateNodesList) >= 1;
	}
}


/*
 * BuildForPrimaryNodeNodeActiveContext computes every fact the
 * ForPrimaryNode section of MonitorFSM[] (from MonitorFSM_PrimaryNodeSectionStart
 * onward) needs, mirroring the counting loop that used to be inline at the
 * top of the old, now-folded-in ProceedGroupStateForPrimaryNode() (the same
 * loop OtherNodeIsDueForCatchingUp's condition drives the fan-out assignment
 * for, in ActionCatchupUnhealthySecondaries above).
 */
static void
BuildForPrimaryNodeNodeActiveContext(GroupStateContext *ctx, AutoFailoverNode *primaryNode,
									 NodeActiveContext *nac)
{
	memset(nac, 0, sizeof(NodeActiveContext));

	BuildNodeStatus(ctx, primaryNode, &nac->activeNode);
	/* .primaryNode role is unused by every row in MonitorFSM[]'s ForPrimaryNode section --
	 * primaryNode IS activeNode here, so every condition is expressed against .activeNode
	 * directly. */

	List *otherNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
	int otherNodesCount = list_length(otherNodesGroupList);

	int replicationQuorumCount = otherNodesCount;
	int secondaryNodesCount = otherNodesCount;
	int secondaryQuorumNodesCount = otherNodesCount;

	ListCell *nodeCell = NULL;

	foreach(nodeCell, otherNodesGroupList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (OtherNodeIsDueForCatchingUp(ctx, otherNode))
		{
			--secondaryNodesCount;
			--secondaryQuorumNodesCount;
		}
		else if (!IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY))
		{
			--secondaryNodesCount;
			--secondaryQuorumNodesCount;
		}
		else if (IsCurrentState(otherNode, REPLICATION_STATE_SECONDARY) &&
				 !otherNode->replicationQuorum)
		{
			--secondaryQuorumNodesCount;
		}

		if (!otherNode->replicationQuorum)
		{
			--replicationQuorumCount;
		}

		if (IsCurrentState(otherNode, REPLICATION_STATE_WAIT_STANDBY))
		{
			nac->anyOtherNodeWaitingStandby = true;
		}
	}

	nac->replicationQuorumCountIsZero = (replicationQuorumCount == 0);
	nac->secondaryNodesCountIsZero = (secondaryNodesCount == 0);
	nac->secondaryQuorumNodesCountIsZero = (secondaryQuorumNodesCount == 0);
	nac->numberSyncStandbysIsZero = (ctx->formation->number_sync_standbys == 0);
	nac->failoverInProgress = IsFailoverInProgress(ctx->groupNodeList);
}


/*
 * MonitorFSM[]: one array, not several -- see the boundary-constant comment
 * above the MonitorFSMTransition typedef for the section layout and why it's
 * a single ordered list rather than one array per real C function. Rows are
 * kept in the exact order the original if-chain(s) checked them in:
 * first-match-wins over this array is a straight extraction, not a
 * behaviour change, exactly as it was over the three separate arrays this
 * replaces.
 *
 * --- [0, MonitorFSM_FromContextStart): the six checks the real if-chain
 * runs BEFORE the IsInPrimaryState(activeNode) early return
 * (group_state_machine.c:284) -- DROPPED, goal==DROPPED, MAINTENANCE, the
 * demote_timeout self-fence, and both "alone in group" rows. These fire
 * regardless of whether activeNode is currently the primary (a primary that
 * just lost its only standby must still reach SINGLE here, before ever
 * redirecting into the ProceedGroupStateForPrimaryNode section) -- confirmed
 * by the drop_node regression test, which failed the first time this table
 * put the primary-state redirect ahead of these six checks instead of after
 * them. None of these six rows reference .primaryNode at all, so they can be
 * matched against a NodeActiveContext built with primaryNode == NULL, before
 * primaryNode is even resolved.
 */
static const MonitorFSMTransition MonitorFSM[] = {
	/* converged to dropped -> remove the node from the catalog entirely */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DROPPED) },
	  .extraAction = ActionRemoveDroppedNode,
	  .comment = "converged to dropped -> remove the node from the catalog" },

	/* goal already dropped (mid-drop, row above hasn't converged yet) -> no-op */
	{ .activeNode = { .statePattern = FSM_DROPPED_GOAL },
	  .comment = "goal already dropped -> no-op" },

	/* converged to maintenance -> no-op, frozen until stop_maintenance() */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_MAINTENANCE) },
	  .comment = "converged to maintenance -> no-op, frozen until stop_maintenance()" },

	/* demote_timeout self-fence re-target (issue #1025) */
	{ .activeNode = { .statePattern = FSM_REPORTED_DEMOTE_TIMEOUT,
					  .unreachableFromDemoteTimeout = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "reported demote_timeout, assigned goal can't reach it -> demoted" },

	/* alone in group, candidate-eligible */
	{ .activeNode = { .statePattern = FSM_NOT_STABLE_SINGLE,
					  .candidateEligible = BOOL_TRUE },
	  .conditions = { .groupHasExactlyOneNode = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SINGLE),
	  .comment = "alone in group, candidate-eligible -> single" },

	/* alone in group, not candidate-eligible */
	{ .activeNode = { .statePattern = FSM_NOT_STABLE_SINGLE,
					  .candidateEligible = BOOL_FALSE },
	  .conditions = { .groupHasExactlyOneNode = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "alone in group, candidatePriority zero -> report_lsn" },

	/* --- [MonitorFSM_FromContextStart, MonitorFSM_PrimaryNodeSectionStart): the rest of
	 * ProceedGroupStateFromContext()'s own sequential if-chain -- everything from the
	 * timeline-fork check (group_state_machine.c:328, right after the
	 * IsInPrimaryState(activeNode) early return) onward. Reached only when activeNode is
	 * NOT currently primary-role. */

	/* converged secondary, reportedTLI not an ancestor of the group's reference timeline */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isComparableToReferenceTli = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "converged secondary, reportedTLI not an ancestor of reference -> catchingup" },

	/* replication stall (#997): primary healthy, no standby past replication_stall_timeout */
	{ .primaryNode = { .isInPrimaryState = BOOL_TRUE,
					   .isHealthy = BOOL_TRUE },
	  .conditions = { .replicationStallExceeded = BOOL_TRUE },
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "primary healthy, no standby past replication_stall_timeout -> wait_primary" },

	/* nodesCount>2, primary unhealthy -- draining/maintenance/nothing decided inside the
	 * action, then the MS-failover cascade unconditionally; must be a single row, see
	 * ActionRunMultiStandbyFailoverCascade's comment for why. */
	{ .primaryNode = { .isUnhealthy = BOOL_TRUE },
	  .conditions = { .groupHasMoreThanTwoNodes = BOOL_TRUE },
	  .extraAction = ActionRunMultiStandbyFailoverCascade,
	  .comment = "nodesCount>2, primary unhealthy -> draining/maintenance + MS-failover cascade" },

	/* report_lsn, primary converged wait/join_primary, healthy */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "report_lsn, primary converged wait/join_primary, healthy -> secondary" },

	/* report_lsn, primary converged primary, healthy */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY),
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "report_lsn, primary converged primary, healthy -> secondary" },

	/* fast_forward done -> prepare_promotion */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_FAST_FORWARD) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .comment = "fast_forward done -> prepare_promotion" },

	/* continue an already-started MS failover -- a direct `return` in the real source, and no
	 * later row in this table matches activeNode in REPORT_LSN/FAST_FORWARD, so it doesn't
	 * matter here whether the extraAction's bool stops dispatch or lets it keep scanning. */
	{ .activeNode = { .statePattern = FSM_REPORT_LSN_OR_FAST_FORWARD },
	  .extraAction = ActionRunPlainMSFailoverCascade,
	  .comment = "report_lsn or fast_forward, continuing an already-started failover -> "
				 "MS-failover cascade" },

	/* wait_standby, primary converged wait/join_primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "wait_standby, primary converged wait/join_primary -> catchingup" },

	/* wait_standby (quorum member), primary converged primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY),
					  .replicationQuorum = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_APPLY_SETTINGS),
	  .comment = "wait_standby (quorum member), primary converged primary -> "
				 "catchingup + apply_settings" },

	/* wait_standby (not a quorum member), primary converged primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY),
					  .replicationQuorum = BOOL_FALSE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "wait_standby (not a quorum member), primary converged primary -> catchingup" },

	/* caught up, same TLI as primary, within sync threshold */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_CATCHINGUP),
					  .isHealthy = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_PRIMARY_OR_WAIT_OR_JOIN },
	  .conditions = { .activeAndPrimaryTliMatch = BOOL_TRUE,
					  .walWithinSyncThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "caught up, same TLI as primary, within sync threshold -> secondary" },

	/* primary fails, already converged wait_primary (no draining edge, issue #1168) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isHealthy = BOOL_TRUE,
					  .candidateEligible = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY),
					   .isUnhealthy = BOOL_TRUE },
	  .conditions = { .walWithinPromoteThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .comment = "primary fails, already converged wait_primary (issue #1168) -> "
				 "secondary -> prepare_promotion only (1 of 2)" },

	/* primary fails, not already wait_primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isHealthy = BOOL_TRUE,
					  .candidateEligible = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_NOT_STABLE_WAIT_PRIMARY,
					   .isInPrimaryState = BOOL_TRUE,
					   .isUnhealthy = BOOL_TRUE },
	  .conditions = { .walWithinPromoteThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DRAINING),
	  .comment = "primary fails, not already wait_primary -> secondary -> prepare_promotion, "
				 "primary -> draining (2 of 2)" },

	/* wait_maintenance, primary converged wait_primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_MAINTENANCE) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "wait_maintenance, primary converged wait_primary -> maintenance" },

	/* wait_maintenance, primary's goal no longer wait_primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_MAINTENANCE) },
	  .primaryNode = { .statePattern = FSM_NOT_ASSIGNED_WAIT_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "wait_maintenance, primary's goal no longer wait_primary -> maintenance" },

	/* prepare_promotion, primary converged prepare_maintenance */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_MAINTENANCE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .comment = "prepare_promotion, primary converged prepare_maintenance -> stop_replication" },

	/* Citus worker, primary present */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "Citus worker prepare_promotion, primary present -> wait_primary + demoted" },

	/* Citus worker, primary removed */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "Citus worker prepare_promotion, primary removed -> wait_primary" },

	/* prepare_promotion, primary present, already converged wait_primary (issue #1168) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_TRUE,
					   .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY),
					   .isInMaintenance = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .comment = "prepare_promotion, primary already converged wait_primary (issue #1168) -> "
				 "stop_replication only (1 of 2)" },

	/* prepare_promotion, primary present, not in maintenance, not already wait_primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_TRUE,
					   .statePattern = FSM_NOT_STABLE_WAIT_PRIMARY,
					   .isInMaintenance = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTE_TIMEOUT),
	  .comment = "prepare_promotion, primary present, not in maintenance -> "
				 "stop_replication + demote_timeout (2 of 2)" },

	/* prepare_promotion, primary removed */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "prepare_promotion, primary removed -> wait_primary" },

	/* stop_replication, primary converged prepare_maintenance */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_MAINTENANCE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "stop_replication, primary converged prepare_maintenance -> "
				 "wait_primary + maintenance" },

	/* stop_replication, primary converged demote_timeout (3-way OR, 1 of 3) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTE_TIMEOUT) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary converged demote_timeout -> "
				 "wait_primary + demoted (1 of 3)" },

	/* stop_replication, primary's drain time expired (3-way OR, 2 of 3) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .drainTimeExpired = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary's drain time expired -> "
				 "wait_primary + demoted (2 of 3)" },

	/* stop_replication, primary's goal is wait_primary but presumed dead (3-way OR, 3 of 3) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .conditions = { .primaryIsWaitPrimaryPresumedDead = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary's goal wait_primary but presumed dead -> "
				 "wait_primary + demoted (3 of 3)" },

	/* Citus worker, primary present */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "Citus worker stop_replication, primary present -> wait_primary + demoted" },

	/* Citus worker, primary removed */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "Citus worker stop_replication, primary removed -> wait_primary" },

	/* demoted, primary reported wait/join_primary with goal primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTED) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY_TRANSITIONING_TO_PRIMARY,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "demoted, primary reported wait/join_primary with goal primary -> catchingup" },

	/* demoted, primary converged wait/join_primary/primary, healthy */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTED) },
	  .primaryNode = { .statePattern = FSM_PRIMARY_OR_WAIT_OR_JOIN,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "demoted, primary converged wait/join_primary/primary, healthy -> catchingup" },

	/* join_secondary, primary reported wait_primary with goal wait/primary -- cascades into a
	 * nested pass on primaryNode */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_SECONDARY) },
	  .primaryNode = { .statePattern = FSM_WAIT_PRIMARY_TRANSITIONING_TO_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .extraAction = ActionRunPrimaryNodeTransition,
	  .comment = "join_secondary, primary reported wait_primary with goal wait/primary -> "
				 "secondary" },

	/* join_secondary, primary converged primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_SECONDARY) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "join_secondary, primary converged primary -> secondary" },

	/* --- [MonitorFSM_PrimaryNodeSectionStart, MonitorFSM_SIZE): the declarative replacement
	 * for ProceedGroupStateForPrimaryNode()'s own sequential if-chain. Here .activeNode maps
	 * to the primaryNode parameter, not a reporting node -- reached either directly by the
	 * top-level driver (activeNode already primary-role) or via ActionRunPrimaryNodeTransition's
	 * nested pass on primaryNode (the join_secondary cascade row above). */

	/* primary alone, another node reached wait_standby */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SINGLE) },
	  .conditions = { .anyOtherNodeWaitingStandby = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "primary alone, another node reached wait_standby -> wait_primary" },

	/* all nodes async, zero secondaries */
	{ .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .conditions = { .replicationQuorumCountIsZero = BOOL_TRUE,
					  .secondaryNodesCountIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "all nodes async, zero secondaries -> wait_primary" },

	/* all nodes async, >=1 secondary */
	{ .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .conditions = { .replicationQuorumCountIsZero = BOOL_TRUE,
					  .secondaryNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "all nodes async, >=1 secondary -> primary" },

	/* converged primary/apply_settings (not wait_primary), no quorum secondaries,
	 * number_sync_standbys=0, no failover in progress (issue #774) */
	{ .activeNode = { .statePattern = FSM_PRIMARY_OR_APPLY_SETTINGS_ONLY },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_TRUE,
					  .failoverInProgress = BOOL_FALSE,
					  .numberSyncStandbysIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "converged primary/apply_settings, no quorum secondaries, no failover in "
				 "progress, number_sync_standbys=0 -> wait_primary" },

	/* same, but number_sync_standbys>0 -> block writes on primary */
	{ .activeNode = { .statePattern = FSM_PRIMARY_OR_APPLY_SETTINGS_ONLY },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_TRUE,
					  .failoverInProgress = BOOL_FALSE,
					  .numberSyncStandbysIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "converged primary/apply_settings, no quorum secondaries, no failover in "
				 "progress, number_sync_standbys>0 -> primary (block writes)" },

	/* wait_primary, >=1 quorum secondary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY) },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "wait_primary, >=1 quorum secondary -> primary" },

	/* apply_settings, both zero */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_TRUE,
					  .secondaryQuorumNodesCountIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "apply_settings, both zero -> wait_primary" },

	/* apply_settings, number_sync_standbys != 0 (1 of 2 disjuncts) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "apply_settings, number_sync_standbys != 0 -> primary (1 of 2 disjuncts)" },

	/* apply_settings, sync_standbys=0 but >=1 quorum secondary (2 of 2) */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_TRUE,
					  .secondaryQuorumNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "apply_settings, sync_standbys=0 but >=1 quorum secondary -> primary (2 of 2)" },

	/* converged primary/wait_primary/apply_settings, no other condition applies */
	{ .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "converged primary/wait_primary/apply_settings, no other condition applies -> "
				 "no-op besides the unhealthy-secondary fan-out" },

	/* backwards-compat: join_primary -> primary */
	{ .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .comment = "backwards-compat: join_primary -> primary" },
};


/*
 * ProceedGroupStateFromContext is the core FSM logic, operating entirely on
 * the pre-built GroupStateContext.  It does not touch the database for reads;
 * writes (AssignGoalState, NotifyStateChange) still go to the DB.
 *
 * This separation lets test code inject a synthetic context and exercise the
 * FSM without a live database connection.
 *
 * Single-shot, three straight-line lookups at most -- matching the design
 * doc's own top-level driver, not a loop: the cascades that need more than
 * one row's worth of assignment in a single call (the MS-failover cascade,
 * the join_secondary -> nested primary pass) get there via a bounded, named
 * nested search inside their own extraAction (see ActionRunMultiStandby
 * FailoverCascade and ActionRunPrimaryNodeTransition), not by this driver
 * looping.
 *
 * Two lookups, not the design doc's one ("startIndex = isInPrimaryState ?
 * MonitorFSM_PrimaryNodeSectionStart : 0"): the six early-check rows must
 * always be tried first, regardless of whether activeNode is already
 * primary-role -- a primary that just lost its only standby must still
 * reach SINGLE via those checks, not get redirected to the
 * ProceedGroupStateForPrimaryNode section first. Jumping straight past them
 * whenever activeNode is already primary-role would skip that case
 * entirely; confirmed by the drop_node regression test, which failed
 * exactly this way the first time this table's ordering got this wrong.
 */
bool
ProceedGroupStateFromContext(GroupStateContext *ctx)
{
	AutoFailoverNode *activeNode = ctx->activeNode;
	char *formationId = ctx->formationId;
	int groupId = ctx->groupId;

	/*
	 * The six early checks run unconditionally, before the IsInPrimaryState
	 * redirect below -- regardless of whether activeNode currently is the
	 * primary. primaryNode isn't resolved yet at this point -- and none of
	 * these six rows reference it -- so NULL is passed and is safe.
	 */
	NodeActiveContext earlyNac;

	BuildFromContextNodeActiveContext(ctx, NULL, &earlyNac);

	if (FindAndDispatchMonitorFSMRule(ctx, &earlyNac, 0, MonitorFSM_FromContextStart))
	{
		return true;
	}

	/*
	 * We separate out the FSM for the primary server, because that one needs
	 * to loop over every other node to take decisions. That induces some
	 * complexity that is best managed with its own NodeActiveContext, built
	 * with primaryNode substituted for activeNode's role (see
	 * BuildForPrimaryNodeNodeActiveContext).
	 *
	 * This early return can't become an ordinary row: it's exactly the
	 * branch point the whole table design has to preserve as an *entry*
	 * decision, not a matched condition.
	 */
	if (IsInPrimaryState(activeNode))
	{
		NodeActiveContext primaryNac;

		BuildForPrimaryNodeNodeActiveContext(ctx, activeNode, &primaryNac);

		return FindAndDispatchMonitorFSMRule(ctx, &primaryNac,
											 MonitorFSM_PrimaryNodeSectionStart,
											 MonitorFSM_SIZE);
	}

	/*
	 * Derive primaryNode from ctx->groupNodeList (already fetched under the
	 * lock NodeActive() holds for the whole call) instead of running a
	 * second, independent AutoFailoverNodeGroup() query -- see
	 * GetPrimaryOrDemotedNodeInGroupFromList()'s comment for why this
	 * matters even though every writer now shares the same lock.
	 */
	AutoFailoverNode *primaryNode =
		GetPrimaryOrDemotedNodeInGroupFromList(ctx->groupNodeList);

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
	if (primaryNode == NULL && !IsFailoverInProgress(ctx->groupNodeList))
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

	NodeActiveContext nac;

	BuildFromContextNodeActiveContext(ctx, primaryNode, &nac);

	return FindAndDispatchMonitorFSMRule(ctx, &nac, MonitorFSM_FromContextStart,
										 MonitorFSM_PrimaryNodeSectionStart);
}


/*
 * WalSourceNodesAreAllUnhealthy returns true when every report_lsn peer that
 * could serve as a WAL source for the given fast_forward candidate is
 * currently unhealthy.  Returns false if at least one source is healthy, or
 * if there are no source nodes at all (unusual; caller handles separately).
 */
static bool
WalSourceNodesAreAllUnhealthy(GroupStateContext *ctx,
							  List *nodesGroupList,
							  AutoFailoverNode *candidateNode)
{
	ListCell *nodeCell = NULL;
	bool foundAnySource = false;

	foreach(nodeCell, nodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->nodeId == candidateNode->nodeId)
		{
			continue;
		}

		if (!IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
		{
			continue;
		}

		foundAnySource = true;

		if (NodeIsHealthy(node, ctx))
		{
			return false;
		}
	}

	return foundAnySource;
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
ProceedGroupStateForMSFailover(GroupStateContext *ctx,
							   AutoFailoverNode *primaryNode)
{
	AutoFailoverNode *activeNode = ctx->activeNode;
	List *nodesGroupList = ctx->groupNodeList;  /* already fetched in context */
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
			/*
			 * Detect a fast_forward candidate whose WAL fetch failed: the
			 * keeper reports back report_lsn while the goal is still
			 * fast_forward.
			 *
			 * When all WAL source nodes (other report_lsn peers) are
			 * unhealthy we cannot make progress without data loss.  Warn the
			 * operator and act based on guard_data_loss:
			 *
			 *  - guard_data_loss=true: reset the candidate goal back to
			 *    report_lsn so the cycle retries automatically if a source
			 *    recovers.
			 *
			 *  - guard_data_loss=false: log and fall through.
			 *    get_most_advanced_standby() filters unhealthy sources out,
			 *    so fsm_fast_forward() will find no upstream, skip the WAL
			 *    fetch, and report fast_forward as its current state.  The
			 *    monitor then assigns prepare_promotion on the next call.
			 */
			if (activeNode->reportedState == REPLICATION_STATE_REPORT_LSN &&
				activeNode->goalState == REPLICATION_STATE_FAST_FORWARD &&
				WalSourceNodesAreAllUnhealthy(ctx, nodesGroupList, activeNode))
			{
				if (GuardDataLoss)
				{
					LogAndNotifyMessage(
						message, BUFSIZE,
						"Failover candidate " NODE_FORMAT
						" is stuck in fast_forward: all WAL source nodes are "
						"unhealthy and pgautofailover.guard_data_loss is true. "
						"Resetting candidate to report_lsn to retry when a "
						"source recovers. Use pg_autoctl perform failover "
						"--allow-data-loss to promote with available WAL.",
						NODE_FORMAT_ARGS(activeNode));

					AssignGoalState(activeNode,
									REPLICATION_STATE_REPORT_LSN,
									message);

					return true;
				}
				else
				{
					LogAndNotifyMessage(
						message, BUFSIZE,
						"Failover candidate " NODE_FORMAT
						" is in fast_forward with all WAL source nodes "
						"unhealthy; pgautofailover.guard_data_loss is false, "
						"will promote with available WAL.",
						NODE_FORMAT_ARGS(activeNode));
				}
			}

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
			NodeIsHealthy(nodeBeingPromoted, ctx))
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
	 *
	 * Before any of that: filter out nodes whose reported timeline has
	 * genuinely diverged from the group's reference lineage (see #683).
	 * They are excluded, not deprioritized -- a diverged node can never
	 * become comparable no matter how long we wait for it, so leaving it
	 * in would risk either comparing incomparable LSNs, or blocking the
	 * whole election on a node that will never resolve on its own.
	 */
	int referenceTli = 0;
	List *comparableNodesGroupList =
		FilterNodesByTimelineAncestry(nodesGroupList,
									  ctx->formationId,
									  ctx->groupId,
									  &referenceTli);

	candidateList.numberSyncStandbys = ctx->formation->number_sync_standbys;

	BuildCandidateList(ctx, comparableNodesGroupList, &candidateList);

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

		if (GuardDataLoss)
		{
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

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Proceeding with failover despite %d unreported quorum node(s): "
			"pgautofailover.guard_data_loss is false. "
			"Committed transactions on missing node(s) may be lost. "
			"activeNode is " NODE_FORMAT " and reported state \"%s\"",
			candidateList.missingNodesCount,
			NODE_FORMAT_ARGS(activeNode),
			ReplicationStateGetName(activeNode->reportedState));
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
	int minCandidates = ctx->formation->number_sync_standbys + 1;

	/* no candidates is a hard pass */
	if (candidateList.candidateCount == 0)
	{
		return false;
	}

	/* not enough candidates to promote and then accept writes, pass */
	if (candidateList.quorumCandidateCount < minCandidates)
	{
		char message[BUFSIZE] = { 0 };

		if (GuardDataLoss)
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Failover still in progress with %d candidates that participate "
				"in the quorum having reported their LSN: %d nodes are required "
				"in the quorum to satisfy number_sync_standbys=%d in "
				"formation \"%s\", activeNode is " NODE_FORMAT
				" and reported state \"%s\"",
				candidateList.quorumCandidateCount,
				minCandidates,
				ctx->formation->number_sync_standbys,
				ctx->formation->formationId,
				NODE_FORMAT_ARGS(activeNode),
				ReplicationStateGetName(activeNode->reportedState));

			return false;
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Proceeding with failover with only %d quorum candidate(s) despite "
			"number_sync_standbys=%d requiring %d: "
			"pgautofailover.guard_data_loss is false. "
			"The new primary may start in wait_primary state with fewer "
			"sync standbys than required. "
			"activeNode is " NODE_FORMAT " and reported state \"%s\"",
			candidateList.quorumCandidateCount,
			ctx->formation->number_sync_standbys,
			minCandidates,
			NODE_FORMAT_ARGS(activeNode),
			ReplicationStateGetName(activeNode->reportedState));
	}

	/* enough candidates to promote and then accept writes, let's do it! */
	{
		/* build the list of most advanced standby nodes, not ordered */
		List *mostAdvancedNodeList =
			ListMostAdvancedStandbyNodes(comparableNodesGroupList);

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
			SelectFailoverCandidateNode(ctx, &candidateList, primaryNode);

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
BuildCandidateList(GroupStateContext *ctx, List *nodesGroupList,
				   CandidateList *candidateList)
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
		if (NodeIsUnhealthy(node, ctx) && !NodeIsReporting(node, ctx))
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
SelectFailoverCandidateNode(GroupStateContext *ctx,
							CandidateList *candidateList,
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
		if (NodeIsUnhealthy(node, ctx))
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

			if (NodeIsHealthy(node, ctx))
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
	 * A candidate was selected from a pool already filtered to the
	 * accepted (or auto-detected) lineage: whatever operator-pinned fork
	 * resolution was in effect has done its job. Mark it resolved so a
	 * future, unrelated fork doesn't inherit a stale pin.
	 */
	ResolveAcceptedTimeline(selectedNode->formationId, selectedNode->groupId);

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
