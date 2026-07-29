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
#include "utils/tuplestore.h"


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
static bool ProceedWithMSFailover(GroupStateContext *ctx, AutoFailoverNode *activeNode,
								  AutoFailoverNode *candidateNode);

static bool BuildCandidateList(GroupStateContext *ctx,
							   List *standbyNodesGroupList,
							   CandidateList *candidateList);

static AutoFailoverNode * SelectFailoverCandidateNode(GroupStateContext *ctx,
													  CandidateList *candidateList,
													  AutoFailoverNode *primaryNode);

static bool PromoteSelectedNode(GroupStateContext *ctx,
								AutoFailoverNode *selectedNode,
								AutoFailoverNode *primaryNode,
								CandidateList *candidateList);

static void AssignGoalState(AutoFailoverNode *pgAutoFailoverNode,
							ReplicationState state, char *description);
static bool WalDifferenceWithin(AutoFailoverNode *secondaryNode,
								AutoFailoverNode *primaryNode,
								int64 delta);
static void AssertMonitorFSMWellFormed(void);

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
 * extraAction -- the candidate-selection algorithm itself (priority sort,
 * LSN comparison, WAL-fetch orchestration) doesn't reduce to declarative
 * conditions any more cleanly than it did before this change. Only the
 * plain AssignGoalState calls at the tail end of that algorithm --
 * BuildCandidateList's own fan-out to REPORT_LSN, and PromoteSelectedNode's
 * own PREPARE_PROMOTION/FAST_FORWARD choice -- are dispatched through the
 * table too (via TryFanOutReportLsnRow/DispatchMonitorFSMRuleByPos, see
 * their own comments), each falling back to the original hand-written call
 * on no match.
 * ---------------------------------------------------------------------
 */

typedef enum BoolPattern
{
	BOOL_ANY = 0,
	BOOL_FALSE,
	BOOL_TRUE
} BoolPattern;

static bool
BoolMatchesPattern(bool actual, BoolPattern pattern)
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

/* the "primary role" states MONITOR_FSM_SECTION_PRIMARY_NODE's own rows match
 * against (the declarative replacement for the old, now-removed
 * ProceedGroupStateForPrimaryNode()) -- a different three-element set from
 * FSM_PRIMARY_OR_WAIT_OR_JOIN above (no JOIN_PRIMARY, has APPLY_SETTINGS) */
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
	.reportedStates = STATES(REPLICATION_STATE_WAIT_PRIMARY,
							 REPLICATION_STATE_JOIN_PRIMARY),
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
	.reportedStates = STATES(REPLICATION_STATE_REPORT_LSN,
							 REPLICATION_STATE_FAST_FORWARD),
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
			return !((reported == goal) && MatchStateSet(reported,
														 pattern->reportedStates));
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
	BoolPattern isDemotedPrimary;
	BoolPattern canTakeWrites;
	BoolPattern isReadyToStreamWAL;
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
 * isInPrimaryState/isInMaintenance/isDemotedPrimary/canTakeWrites/
 * drainTimeExpired/unreachableFromDemoteTimeout are deliberately NOT cached
 * in NodeStatus and computed live here from status->node instead: unlike
 * isHealthy/isUnhealthy/candidateEligible (health/priority facts that can't
 * change mid-dispatch), these are pure functions of
 * node->goalState/reportedState, and a matched row's extraAction can
 * reassign the very node being matched (e.g. DRAINING on primaryNode) in
 * the SAME node_active() call, before dispatch continues to a later row --
 * exactly like NodeStateMatchesPattern below, which reads status->node's
 * fields live for the same reason. Caching these as snapshot booleans (an
 * earlier version of this code did) left later rows in the same call
 * matching against a stale "still in primary state" fact even after
 * primaryNode had just been moved to DRAINING -- confirmed by
 * concurrent_health_check_and_report, which requires the "secondary ->
 * prepare_promotion" row to correctly stop matching once primaryNode is no
 * longer IsInPrimaryState().
 *
 * canTakeWrites is distinct from isInPrimaryState: it's CanTakeWritesInState
 * (node_metadata.c) applied to goalState alone, with no requirement that
 * reportedState has converged to match -- exactly node_active_protocol.c's
 * RemoveNode() own "bool currentNodeIsPrimary =
 * CanTakeWritesInState(currentNode->goalState);" check, which a mid-
 * transition node (goalState allows writes, reportedState hasn't caught up
 * yet) still satisfies where isInPrimaryState (which additionally requires
 * stability) would not.
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

	return BoolMatchesPattern(status->node != NULL, pattern->exists) &&
		   NodeStateMatchesPattern(status->node, &pattern->statePattern) &&
		   BoolMatchesPattern(status->isHealthy, pattern->isHealthy) &&
		   BoolMatchesPattern(status->isUnhealthy, pattern->isUnhealthy) &&
		   BoolMatchesPattern(status->candidateEligible, pattern->candidateEligible) &&
		   BoolMatchesPattern(IsInPrimaryState(status->node),
							  pattern->isInPrimaryState) &&
		   BoolMatchesPattern(IsInMaintenance(status->node), pattern->isInMaintenance) &&
		   BoolMatchesPattern(IsDemotedPrimary(status->node),
							  pattern->isDemotedPrimary) &&
		   BoolMatchesPattern(status->node != NULL &&
							  CanTakeWritesInState(status->node->goalState),
							  pattern->canTakeWrites) &&
		   BoolMatchesPattern(CandidateNodeIsReadyToStreamWAL(status->node),
							  pattern->isReadyToStreamWAL) &&
		   BoolMatchesPattern(NodeIsDrainTimeExpired(status->node, status->ctx),
							  pattern->drainTimeExpired) &&
		   BoolMatchesPattern(status->isCitusWorkerGroup, pattern->isCitusWorkerGroup) &&
		   BoolMatchesPattern(status->replicationQuorum, pattern->replicationQuorum) &&
		   BoolMatchesPattern(status->isComparableToReferenceTli,
							  pattern->isComparableToReferenceTli) &&
		   BoolMatchesPattern(unreachableFromDemoteTimeout,
							  pattern->unreachableFromDemoteTimeout);
}


/*
 * MonitorApiFunction itself is declared in group_state_machine.h, not here:
 * ProceedGroupStateForApiTrigger() (below the array) is called from
 * node_active_protocol.c/formation_metadata.c with a MonitorApiFunction
 * value, so the enum needs to be visible outside this file the same way
 * MonitorFSMSection already is. ApiTriggerPattern/ApiTriggerKind/
 * MatchApiTrigger/API_TRIGGER() stay private to this file: only the enum
 * itself crosses the module boundary, every caller just picks a tag and
 * hands it to ProceedGroupStateForApiTrigger, never builds a pattern
 * itself.
 *
 * Every builder in this file (BuildFromContextNodeActiveContext,
 * BuildForPrimaryNodeNodeActiveContext) memset()s its NodeActiveContext to
 * zero before filling it in, so apiFunction defaults to API_FUNCTION_NONE
 * unless a caller explicitly sets it -- exactly the design doc's "zero
 * changes" guarantee: no row written before this mechanism existed changes
 * meaning just because the field now exists.
 */

typedef enum ApiTriggerKind
{
	/*
	 * The safe default for an omitted .conditions.apiTrigger field: matches
	 * only an ordinary node_active() heartbeat call. Every row written
	 * before this mechanism existed implicitly assumed exactly this, so
	 * omission has to keep meaning "heartbeat only", not "any trigger" --
	 * the opposite of BOOL_ANY/NODE_STATE_ANY's "omission matches
	 * everything" convention elsewhere in this file, and deliberately so:
	 * here the safe default is the narrower match, since none of the 48
	 * existing rows should start matching a manual perform_failover() call
	 * just because they don't mention this field at all.
	 */
	API_TRIGGER_NODE_ACTIVE = 0,
	API_TRIGGER_SPECIFIC
} ApiTriggerKind;

typedef struct ApiTriggerPattern
{
	ApiTriggerKind kind;
	MonitorApiFunction function; /* meaningful only when kind == API_TRIGGER_SPECIFIC */
} ApiTriggerPattern;

#define API_TRIGGER(fn) ((ApiTriggerPattern) { .kind = API_TRIGGER_SPECIFIC, .function = \
												   (fn) })

static bool
MatchApiTrigger(MonitorApiFunction actual, ApiTriggerPattern pattern)
{
	switch (pattern.kind)
	{
		case API_TRIGGER_SPECIFIC:
		{
			return actual == pattern.function;
		}

		case API_TRIGGER_NODE_ACTIVE:
		default:
		{
			return actual == API_FUNCTION_NONE;
		}
	}
}


/*
 * NodeActiveContext: group-level facts, computed once per dispatch call
 * alongside the two NodeStatus roles above.
 */
typedef struct NodeActiveContext
{
	NodeStatus activeNode;
	NodeStatus primaryNode;

	/*
	 * candidateNode is only populated by BuildMSFailoverNodeActiveContext
	 * (the MS-failover cluster's own nested-dispatch context builder,
	 * mirroring BuildForPrimaryNodeNodeActiveContext's role-specific
	 * pattern): FindCandidateNodeBeingPromoted(ctx->groupNodeList)'s result,
	 * i.e. the node currently mid-promotion, if any. .node == NULL if no
	 * failover candidate has been selected yet this round. Left at its
	 * memset-zero default (.node == NULL, .isUnhealthy == true) for every
	 * other dispatch pass -- no row outside the MS-failover sub-section
	 * references it.
	 */
	NodeStatus candidateNode;

	MonitorApiFunction apiFunction;

	bool groupHasExactlyOneNode;
	bool groupHasExactlyTwoNodes;
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

	/*
	 * lastHealthySyncStandbyGoingToMaintenance is set only inside
	 * ProceedGroupStateForApiTrigger's own API_FUNCTION_START_MAINTENANCE
	 * branch (see its own comment there): true when activeNode is the last
	 * remaining healthy synchronous standby and formation->number_sync_
	 * standbys is zero, meaning putting it into maintenance would otherwise
	 * block writes on the primary until the monitor separately assigns it
	 * wait_primary -- node_active_protocol.c:1973-1986's own real condition,
	 * verbatim. Left false (the memset default) for every other dispatch
	 * pass; not a general-purpose fact reused elsewhere.
	 */
	bool lastHealthySyncStandbyGoingToMaintenance;

	/*
	 * The following four facts are set only by
	 * BuildMSFailoverNodeActiveContext, for the MS-failover cluster's own
	 * declarative rows (see group_state_machine.c's "MS-failover /
	 * candidate-selection cluster" section) -- left false (the memset
	 * default) for every other dispatch pass.
	 */

	/* WalSourceNodesAreAllUnhealthy(ctx, ctx->groupNodeList, activeNode) --
	 * activeNode-specific despite living at this level (not on NodeStatus):
	 * it depends on the whole group's other REPORT_LSN peers, not on
	 * activeNode's own state alone. */
	bool activeNodeAllWalSourcesUnhealthy;

	/* mirrors whether ProceedGroupStateForMSFailover's own
	 * "nodeBeingPromoted != NULL" branch will actually drive that candidate
	 * via ProceedWithMSFailover this round, rather than falling through to
	 * BuildCandidateList/selection instead -- see this file's own
	 * ActionRunMultiStandbyFailoverCascade comment and the design doc's
	 * identically-named fact for the full derivation. */
	bool candidatePromotionInProgress;

	/* WalDifferenceWithin(mostAdvancedNode, ctx->primaryNode,
	 * PromoteXlogThreshold) for the current MS-failover candidate pool's
	 * own most-advanced member -- SelectFailoverCandidateNode's own
	 * data-loss guard. */
	bool mostAdvancedCandidateWithinPromoteThreshold;

	/* GuardDataLoss (metadata.c), the pgautofailover.guard_data_loss GUC --
	 * a single global process-wide bool, grouped here (not read directly
	 * inside an extraAction) so any row gated on it says so in its own
	 * conditions. */
	bool guardDataLossEnabled;

	/*
	 * Set unconditionally true by BuildMSFailoverNodeActiveContext, false
	 * (the memset default) everywhere else -- the MS-failover cluster's own
	 * "am I actually being dispatched from inside the MS-failover cluster's
	 * own bounded nested search" marker. Every row from
	 * MonitorFSM_MSFailoverStart onwards that doesn't already carry a
	 * condition guaranteed false outside that nested search (as pos 363/365
	 * do, via activeNodeAllWalSourcesUnhealthy/candidatePromotionInProgress)
	 * must require this true: the top-level driver's own ordinary lookup
	 * (ProceedGroupStateFromContext) shares MonitorFSM_PrimaryNodeSectionStart
	 * as its own upper bound, so it scans straight through this whole
	 * cluster too -- without this guard, pos 367-373's bare activeNode-state
	 * patterns (no distinguishing condition of their own) would fire for
	 * any ordinary, non-MS-failover heartbeat whose reported/goal states
	 * happened to line up, hijacking normal secondary/catchingup/maintenance
	 * convergence. Confirmed by node_active_protocol.out/guard_data_loss.out/
	 * etc. regressing exactly this way before this field existed.
	 */
	bool inMSFailoverCluster;
} NodeActiveContext;

typedef struct NodeActiveContextPattern
{
	ApiTriggerPattern apiTrigger; /* omitted -> {0} -> API_TRIGGER_NODE_ACTIVE, matching every
	                               *   row's existing meaning with no changes required elsewhere */

	BoolPattern groupHasExactlyOneNode;
	BoolPattern groupHasExactlyTwoNodes;
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
	BoolPattern lastHealthySyncStandbyGoingToMaintenance;

	BoolPattern activeNodeAllWalSourcesUnhealthy;
	BoolPattern candidatePromotionInProgress;
	BoolPattern mostAdvancedCandidateWithinPromoteThreshold;
	BoolPattern guardDataLossEnabled;
	BoolPattern inMSFailoverCluster;
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

/*
 * MonitorFSMSection itself is declared in group_state_machine.h, not here:
 * it needs to be visible to pg_auto_failover.c (to register
 * pgautofailover.fsm_section and wire up dump_fsm()) the same way
 * ReplicationState is. The comment on the MonitorFSM_* index constants
 * below explains what each of its three values corresponds to in the
 * original if-chain. Every row is tagged with its own section explicitly
 * (rather than leaving section membership implicit in array position
 * alone) so a reader scanning the table sees which region a row belongs to
 * without cross-referencing an index against a separate comment block, and
 * so AssertMonitorFSMWellFormed() can verify the MonitorFSM_* index
 * constants actually agree with where each section's rows really are.
 */

typedef struct MonitorFSMTransition
{
	/*
	 * pos and section are metadata, not match inputs: RuleMatches() never
	 * reads either. pos is a human-facing row number -- purely so a comment,
	 * a bug report, or a SQL query against dump_fsm() can say "row 203" and
	 * a reader can find it without counting braces up from the top of the
	 * array. Each MonitorFSMSection gets its own hundred-block, starting at
	 * *01 rather than *00 so the position within the block reads as an
	 * ordinary 1-based count (101 is the section's 1st row, 103 its 2nd,
	 * ...) instead of an off-by-one 0th/1st/2nd. Rows within a section are
	 * numbered every 2 (101, 103, 105, ...) rather than consecutively -- so
	 * a new row can be inserted between two existing ones (e.g. 102 between
	 * 101 and 103) without renumbering anything else in the file.
	 * section records which of the three real control-flow regions (see
	 * MonitorFSMSection above) this row belongs to; AssertMonitorFSMWellFormed()
	 * uses both to confirm the MonitorFSM_* index constants below are still
	 * correct, so a boundary drifting out of sync with the rows it's meant to
	 * bound fails at first use, not by accident months later. Both are also
	 * exposed to SQL via dump_fsm() and attributed to the pgautofailover.event
	 * row a matched rule produces (see rule_pos/rule_section below).
	 */
	int pos;
	MonitorFSMSection section;

	NodeStatusPattern activeNode;
	NodeStatusPattern primaryNode;
	NodeStatusPattern candidateNode;  /* MS-failover sub-section rows only; see
	                                   *  NodeActiveContext's own comment on
	                                   *  .candidateNode */
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
 * name; the four index constants below are forward-declared the same way,
 * for the same reason -- both those actions and the top-level driver need
 * them to bound their searches.
 *
 * WHY THESE CONSTANTS EXIST AT ALL: the original if-chain has real, load-
 * bearing structure that a single flat "first match wins over the whole
 * array" search would destroy. Two different things are true about
 * activeNode/primaryNode depending on WHERE in the original control flow a
 * row came from (whether .activeNode means "the reporting node" or "the
 * primary node substituted in"), and one specific fallback (the MS-failover
 * cascade declining) needs to resume scanning from a specific *later* point,
 * not from the top. Each constant below is the index where one of those
 * real regions starts, so a search can be bounded to exactly the region
 * that's semantically valid for the situation at hand:
 *
 *   MonitorFSM_EarlyChecksStart = 15
 *     Where MONITOR_FSM_SECTION_API_TRIGGERED ends and MONITOR_FSM_SECTION_
 *     EARLY_CHECKS begins. Rows [0, 15) are the operator-triggered rows
 *     (perform_failover, remove_node, start/stop_maintenance,
 *     set_node_candidate_priority, set_node_replication_quorum,
 *     set_formation_number_sync_standbys) -- reached only via
 *     ProceedGroupStateForApiTrigger(), never via the ordinary node_active()
 *     heartbeat path (nac->apiFunction stays API_FUNCTION_NONE there, and
 *     every one of these 15 rows' .conditions.apiTrigger requires a specific
 *     non-NONE value -- see MatchApiTrigger). Used to: (a) start the
 *     api-triggered lookup at 0, bounded to end here, (b) start the
 *     early-checks lookup here instead of at 0, now that the heartbeat
 *     sections no longer begin at the very top of the array.
 *
 *   MonitorFSM_FromContextStart = 21
 *     Where MONITOR_FSM_SECTION_REPORTING_NODE begins. Rows [15, 21) are the
 *     six checks (DROPPED, goal-DROPPED, MAINTENANCE, the demote_timeout
 *     self-fence, both "alone in group" rows) that
 *     ProceedGroupStateFromContext() runs unconditionally, before its one
 *     real branch point (IsInPrimaryState(activeNode)) -- so they're tried
 *     first regardless of which way that branch goes, and everything from
 *     index 21 on is reached only when activeNode is confirmed NOT currently
 *     primary-role. Used to: (a) bound the early-checks lookup to [15, 21)
 *     in the top-level driver, (b) start the ordinary FromContext lookup at
 *     21 once activeNode is confirmed non-primary.
 *
 *   MonitorFSM_FromContextResumeStart = 24
 *     A resume point *inside* MONITOR_FSM_SECTION_REPORTING_NODE, not a
 *     section boundary of its own -- the row right after the merged
 *     nodesCount>2-unhealthy-primary row (pos 209, "nodesCount>2, primary
 *     unhealthy -> draining/maintenance + MS-failover cascade") -- only used
 *     by ActionRunMultiStandbyFailoverCascade: when
 *     ProceedGroupStateForMSFailover() declines, the real source falls
 *     through to whatever if-statement is textually next, and this is where
 *     that "next" starts in this table. Named similarly to (but NOT the
 *     same concept as) the design doc's MonitorFSM_MSFailoverClusterStart --
 *     see ActionRunMultiStandbyFailoverCascade's own comment for the
 *     distinction.
 *
 *   MonitorFSM_MSFailoverStart = 52
 *     A resume point *inside* MONITOR_FSM_SECTION_REPORTING_NODE, like
 *     MonitorFSM_FromContextResumeStart above, not a section boundary of its
 *     own: nine rows, appended at the end of the section rather than
 *     renumbered into it, for the MS-failover / candidate-selection
 *     cluster's own declarative transitions (see that section's own
 *     comment below) -- two (retry-reset, join_secondary) reached through
 *     TryMSFailoverDeclarativeRow's bounded nested dispatch, gated by the
 *     exact same hand-written C condition ProceedGroupStateForMSFailover/
 *     ProceedWithMSFailover already evaluate; four (BuildCandidateList's
 *     own fan-out) through TryFanOutReportLsnRow's bounded nested dispatch,
 *     one per node the fan-out loop touches; two (PromoteSelectedNode's own
 *     two outcomes) through DispatchMonitorFSMRuleByPos, since first-match-
 *     wins can't distinguish between them (see their own comment); one
 *     (the "still gathering candidates" case) purely for dump_fsm()
 *     completeness, never itself dispatched. None reached through the
 *     ordinary top-level driver.
 *
 *   MonitorFSM_PrimaryNodeSectionStart = 61
 *     Where MONITOR_FSM_SECTION_PRIMARY_NODE begins: the declarative
 *     replacement for ProceedGroupStateForPrimaryNode()'s own if-chain, in
 *     which .activeNode means the *primary* node, not the reporting node.
 *     Used to: (a) bound the ordinary FromContext lookup to end here (so it
 *     can never wander into primary-role rows built under a different
 *     NodeActiveContext), (b) start the primary-role lookup here, both from
 *     the top-level driver (activeNode already primary-role) and from
 *     ActionRunPrimaryNodeTransition's nested pass on primaryNode
 *     (join_secondary's cascade row).
 *
 *   MonitorFSM_SIZE = 72
 *     Total row count -- the end bound for the primary-role lookup (nothing
 *     comes after MONITOR_FSM_SECTION_PRIMARY_NODE), and the size every
 *     other bounded search is checked against.
 *
 * Kept as plain hardcoded integers, exactly as the design doc's own
 * placeholders are -- recomputed by hand whenever a row is added, removed,
 * or moved across a boundary. What makes a wrong value here safe to keep as
 * a hand-maintained integer, rather than a foot-gun: every row also carries
 * its own .pos and .section fields (see MonitorFSMTransition above), and
 * AssertMonitorFSMWellFormed() (below the array) walks the whole table once
 * and asserts these six constants agree with what the rows themselves say
 * -- .pos matches array position, and .section actually changes from
 * API_TRIGGERED to EARLY_CHECKS to REPORTING_NODE to PRIMARY_NODE exactly at
 * these four indices and nowhere else. A boundary that drifts out of sync
 * with a row added, removed, or moved fails loudly at first use (an
 * assertion), not silently as a row from the wrong section matching
 * unexpectedly or a search that scans zero rows and never matches.
 */
static const MonitorFSMTransition MonitorFSM[];

#define MonitorFSM_EarlyChecksStart 15
#define MonitorFSM_FromContextStart 21
#define MonitorFSM_FromContextResumeStart 24
#define MonitorFSM_MSFailoverStart 52
#define MonitorFSM_PrimaryNodeSectionStart 61
#define MonitorFSM_SIZE 72

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

	return MatchApiTrigger(nac->apiFunction, cond->apiTrigger) &&

		   NodeMatchesPattern(&nac->activeNode, &rule->activeNode) &&
		   NodeMatchesPattern(&nac->primaryNode, &rule->primaryNode) &&
		   NodeMatchesPattern(&nac->candidateNode, &rule->candidateNode) &&

		   BoolMatchesPattern(nac->groupHasExactlyOneNode,
							  cond->groupHasExactlyOneNode) &&
		   BoolMatchesPattern(nac->groupHasExactlyTwoNodes,
							  cond->groupHasExactlyTwoNodes) &&
		   BoolMatchesPattern(nac->groupHasMoreThanTwoNodes,
							  cond->groupHasMoreThanTwoNodes) &&
		   BoolMatchesPattern(nac->anyOtherNodeWaitingStandby,
							  cond->anyOtherNodeWaitingStandby) &&
		   BoolMatchesPattern(nac->numberSyncStandbysIsZero,
							  cond->numberSyncStandbysIsZero) &&
		   BoolMatchesPattern(nac->replicationQuorumCountIsZero,
							  cond->replicationQuorumCountIsZero) &&
		   BoolMatchesPattern(nac->secondaryNodesCountIsZero,
							  cond->secondaryNodesCountIsZero) &&
		   BoolMatchesPattern(nac->secondaryQuorumNodesCountIsZero,
							  cond->secondaryQuorumNodesCountIsZero) &&
		   BoolMatchesPattern(nac->atLeastOneHealthyCandidate,
							  cond->atLeastOneHealthyCandidate) &&
		   BoolMatchesPattern(nac->walWithinPromoteThreshold,
							  cond->walWithinPromoteThreshold) &&
		   BoolMatchesPattern(nac->walWithinSyncThreshold,
							  cond->walWithinSyncThreshold) &&
		   BoolMatchesPattern(nac->activeAndPrimaryTliMatch,
							  cond->activeAndPrimaryTliMatch) &&
		   BoolMatchesPattern(nac->primaryIsWaitPrimaryPresumedDead,
							  cond->primaryIsWaitPrimaryPresumedDead) &&
		   BoolMatchesPattern(nac->failoverInProgress, cond->failoverInProgress) &&
		   BoolMatchesPattern(nac->replicationStallExceeded,
							  cond->replicationStallExceeded) &&
		   BoolMatchesPattern(nac->lastHealthySyncStandbyGoingToMaintenance,
							  cond->lastHealthySyncStandbyGoingToMaintenance) &&
		   BoolMatchesPattern(nac->activeNodeAllWalSourcesUnhealthy,
							  cond->activeNodeAllWalSourcesUnhealthy) &&
		   BoolMatchesPattern(nac->candidatePromotionInProgress,
							  cond->candidatePromotionInProgress) &&
		   BoolMatchesPattern(nac->mostAdvancedCandidateWithinPromoteThreshold,
							  cond->mostAdvancedCandidateWithinPromoteThreshold) &&
		   BoolMatchesPattern(nac->guardDataLossEnabled, cond->guardDataLossEnabled) &&
		   BoolMatchesPattern(nac->inMSFailoverCluster, cond->inMSFailoverCluster);
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

	/*
	 * Attribute every event this row's dispatch produces -- its own
	 * extraAction's side effects included -- to this rule's .pos/.section
	 * (see CurrentMonitorFSMRulePos/Section in notifications.h). Saved and
	 * restored rather than cleared to 0 on the way out: a nested dispatch
	 * (ActionRunMultiStandbyFailoverCascade's/ActionRunPrimaryNodeTransition's
	 * own bounded search, run from inside extraAction below) sets these to
	 * the *inner* row's own pos/section for its own duration, and once it
	 * returns, this row's own subsequent activeNodeAssignedState/
	 * otherNodeAssignedState calls need to see this (outer) row's values
	 * again, not 0.
	 */
	int savedRulePos = CurrentMonitorFSMRulePos;
	int savedRuleSection = CurrentMonitorFSMRuleSection;

	CurrentMonitorFSMRulePos = rule->pos;
	CurrentMonitorFSMRuleSection = (int) rule->section;

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

	CurrentMonitorFSMRulePos = savedRulePos;
	CurrentMonitorFSMRuleSection = savedRuleSection;
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
	int index = FindMatchingMonitorFSMRuleIndexFrom(MonitorFSM, endIndex, startIndex,
													nac);

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
	/*
	 * A no-op in a non-assert build (see AssertMonitorFSMWellFormed's own
	 * #ifdef USE_ASSERT_CHECKING), so this stays an unconditional call --
	 * matching AssignDeclaredGoalState's own pattern -- rather than an
	 * #ifdef'd call site, which would make the function itself look unused
	 * (and fail -Werror=unused-function) in a non-assert build.
	 */
	static bool monitorFSMChecked = false;

	if (!monitorFSMChecked)
	{
		AssertMonitorFSMWellFormed();
		monitorFSMChecked = true;
	}

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
 * the original source's own if-chain inside ProceedGroupStateFromContext (now
 * the report_lsn/prepare_promotion/stop_replication/... rows further down
 * MonitorFSM[]'s REPORTING_NODE section, for this SAME activeNode).
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
 * from MonitorFSM_FromContextResumeStart, not a flag back to the top-level
 * driver: FindAndDispatchMonitorFSMRule's own internal loop already finds
 * whichever row is the correct next match, however many rows down that is,
 * in one call -- no repeated re-dispatch needed to walk past intervening
 * non-matches.
 *
 * NOTE on naming: MonitorFSM_FromContextResumeStart is NOT
 * MonitorFSM_MSFailoverStart, despite both marking a conceptually similar
 * "resume point" -- they bound two different things. MonitorFSM_FromContextResumeStart
 * (used here) just marks "resume scanning ordinary REPORTING_NODE rows after
 * ProceedGroupStateForMSFailover declines" -- ProceedGroupStateForMSFailover()
 * itself, and the BuildCandidateList/SelectFailoverCandidateNode/
 * PromoteSelectedNode functions it calls, stay hand-written C, called
 * wholesale from here exactly as before this refactor (the candidate-selection
 * algorithm itself doesn't reduce to declarative conditions any more cleanly
 * than it did before -- see this file's own top-of-file design comment).
 * MonitorFSM_MSFailoverStart, by contrast, bounds the *separate* nine-row
 * MS-failover cluster (pos 363-379, "MS-failover / candidate-selection
 * cluster" section below) that those same hand-written functions now reach
 * *into*, at their own tail end, via TryMSFailoverDeclarativeRow/
 * TryFanOutReportLsnRow/DispatchMonitorFSMRuleByPos -- covering the plain
 * per-node goal assignments (retry-reset, join_secondary, BuildCandidateList's
 * own report_lsn fan-out, PromoteSelectedNode's prepare_promotion/fast_forward
 * choice) that those functions used to make via a raw AssignGoalState call,
 * with the original call kept as an unconditional fallback on no match. The
 * candidate-selection algorithm's own logic (priority sort, LSN comparison,
 * WAL-fetch orchestration) is not part of either bounded range.
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
		(void) FindAndDispatchMonitorFSMRule(ctx, nac, MonitorFSM_FromContextResumeStart,
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
ActionRunPlainMSFailoverCascade(GroupStateContext *ctx, NodeActiveContext *nac,
								char *message)
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
ActionRunPrimaryNodeTransition(GroupStateContext *ctx, NodeActiveContext *nac,
							   char *message)
{
	NodeActiveContext primaryNac;

	BuildForPrimaryNodeNodeActiveContext(ctx, nac->primaryNode.node, &primaryNac);

	(void) FindAndDispatchMonitorFSMRule(ctx, &primaryNac,
										 MonitorFSM_PrimaryNodeSectionStart,
										 MonitorFSM_SIZE);
}


static void
ActionCatchupUnhealthySecondaries(GroupStateContext *ctx, NodeActiveContext *nac,
								  char *message)
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
	nac->groupHasExactlyTwoNodes = (ctx->groupNodeCount == 2);
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
BuildForPrimaryNodeNodeActiveContext(GroupStateContext *ctx,
									 AutoFailoverNode *primaryNode,
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
 * ActionFanOutReportLsnOnPrimaryRemoval implements RemoveNode's own
 * primary-removal fan-out (node_active_protocol.c's RemoveNode(),
 * "if (currentNodeIsPrimary) { foreach other node not in maintenance ->
 * report_lsn }"), reusing the exact otherNodesFn-style shape already
 * established for the heartbeat side's own fan-out
 * (ActionCatchupUnhealthySecondaries above) -- a dynamically-sized list of
 * nodes can't be expressed as a single declared activeNodeAssignedState/
 * otherNodeAssignedState slot, operator-triggered or not. Runs before the
 * row's own activeNodeAssignedState = DROPPED (see DispatchMonitorFSMRule),
 * matching the real source's own order: fan out to the survivors first,
 * then mark the removed node itself dropped.
 */
static void
ActionFanOutReportLsnOnPrimaryRemoval(GroupStateContext *ctx, NodeActiveContext *nac,
									  char *message)
{
	List *otherNodesGroupList = AutoFailoverOtherNodesList(nac->activeNode.node);
	ListCell *nodeCell = NULL;

	foreach(nodeCell, otherNodesGroupList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (IsInMaintenance(otherNode))
		{
			continue;
		}

		char otherMessage[BUFSIZE] = { 0 };

		snprintf(otherMessage, BUFSIZE,
				 "Setting goal state of " NODE_FORMAT
				 " to report_lsn after primary node removal.",
				 NODE_FORMAT_ARGS(otherNode));

		AssignGoalState(otherNode, REPLICATION_STATE_REPORT_LSN, otherMessage);
	}
}


/*
 * BuildApiTriggerNodeActiveContext computes every fact the API_TRIGGERED
 * section of MonitorFSM[] needs, for one particular apiFunction.
 *
 * activeNode is whichever single node the call is most fundamentally about
 * -- the design doc's own reframing of "activeNode" for operator-triggered
 * rows (see "Operator-triggered transitions belong in this table too"):
 * the standby being promoted for perform_failover's 2-node row, the primary
 * itself for perform_failover's >2-node row and every
 * set_node_candidate_priority/set_node_replication_quorum/
 * set_formation_number_sync_standbys row (mirroring
 * BuildForPrimaryNodeNodeActiveContext's own "primaryNode IS activeNode"
 * convention), the node being removed for remove_node, the node being
 * (re)moved into/out of maintenance for start/stop_maintenance.
 *
 * primaryNode is the group's real primary, only when a row for this
 * apiFunction genuinely needs to reference "the primary" as a role distinct
 * from activeNode (perform_failover's 2-node row, start_maintenance's
 * secondary rows); NULL otherwise, exactly like the heartbeat side already
 * passes NULL for primaryNode in the analogous "primaryNode IS activeNode"
 * case.
 */
static void
BuildApiTriggerNodeActiveContext(GroupStateContext *ctx, MonitorApiFunction apiFunction,
								 AutoFailoverNode *activeNode,
								 AutoFailoverNode *primaryNode,
								 NodeActiveContext *nac)
{
	memset(nac, 0, sizeof(NodeActiveContext));

	nac->apiFunction = apiFunction;

	BuildNodeStatus(ctx, activeNode, &nac->activeNode);
	BuildNodeStatus(ctx, primaryNode, &nac->primaryNode);

	nac->groupHasExactlyOneNode = (ctx->groupNodeCount == 1);
	nac->groupHasExactlyTwoNodes = (ctx->groupNodeCount == 2);
	nac->groupHasMoreThanTwoNodes = (ctx->groupNodeCount > 2);
	nac->failoverInProgress = IsFailoverInProgress(ctx->groupNodeList);

	if (apiFunction == API_FUNCTION_START_MAINTENANCE && primaryNode != NULL)
	{
		/*
		 * Mirrors node_active_protocol.c's start_maintenance() own
		 * pre-dispatch computation verbatim (its own secondaryNodesCount,
		 * formation->number_sync_standbys, and IsHealthySyncStandby(
		 * currentNode) triple): true when activeNode is the only remaining
		 * healthy synchronous standby and number_sync_standbys is zero --
		 * putting it into maintenance would otherwise block writes on the
		 * primary until the monitor separately assigns it wait_primary.
		 */
		List *secondaryNodesList =
			AutoFailoverOtherNodesListInState(primaryNode, REPLICATION_STATE_SECONDARY);
		int secondaryNodesCount = CountHealthySyncStandbys(secondaryNodesList);

		nac->lastHealthySyncStandbyGoingToMaintenance =
			ctx->formation->number_sync_standbys == 0 &&
			secondaryNodesCount == 1 &&
			IsHealthySyncStandby(activeNode);
	}
}


/*
 * ProceedGroupStateForApiTrigger dispatches a single operator-triggered
 * transition through MonitorFSM[]'s API_TRIGGERED section (pos 101-1xx --
 * see "Operator-triggered transitions belong in this table too" in the
 * design doc). Every SQL-callable wrapper in node_active_protocol.c/
 * formation_metadata.c that assigns a goal state as a direct consequence of
 * an operator call -- perform_failover, remove_node, start/stop_
 * maintenance, set_node_candidate_priority, set_node_replication_quorum,
 * set_formation_number_sync_standbys -- keeps its own imperative shape
 * exactly as before (argument parsing, locking, resolving which node(s) are
 * involved, and every existing validation ereport(ERROR)/WARNING/NOTICE,
 * all preserved unchanged so their exact message text stays test-stable),
 * and calls this once in the middle to make the actual state assignment(s),
 * then continues with whatever POST side effect the real source still does
 * (a continuation ProceedGroupState() call, a candidatePriority trick,
 * number_sync_standbys bookkeeping) as further hand-written code -- none of
 * that imperative surrounding code becomes a row, matching the design doc's
 * own "pre/post side effects stay hand-written C" principle.
 *
 * Unlike the heartbeat side (ProceedGroupStateFromContext, which silently
 * no-ops on no match -- see its own comment for why that's the right call
 * there), a genuine no-match here is always ereport(ERROR)'d as a bug: by
 * the time this is called, the wrapper has already validated, via its own
 * preserved pre-checks, that the operation IS valid for the resolved
 * node's current state -- reaching no-match here means this table's own
 * conditions have drifted out of sync with the wrapper's pre-checks, a
 * real gap to fix, not a normal "operator asked for something invalid"
 * outcome (that case is already rejected earlier, with its own specific,
 * pre-existing ereport(ERROR) message, before this function is ever
 * called).
 */
bool
ProceedGroupStateForApiTrigger(MonitorApiFunction apiFunction,
							   AutoFailoverNode *activeNode,
							   AutoFailoverNode *primaryNode)
{
	GroupStateContext ctx;
	NodeActiveContext nac;

	BuildGroupStateContext(&ctx, activeNode);
	BuildApiTriggerNodeActiveContext(&ctx, apiFunction, activeNode, primaryNode, &nac);

	int index = FindMatchingMonitorFSMRuleIndexFrom(MonitorFSM,
													MonitorFSM_EarlyChecksStart,
													0, &nac);

	if (index < 0)
	{
		ereport(ERROR,
				(errmsg("BUG: no MonitorFSM[] row matches api-triggered call for "
						NODE_FORMAT " in state \"%s\"",
						NODE_FORMAT_ARGS(activeNode),
						ReplicationStateGetName(activeNode->reportedState))));
	}

	DispatchMonitorFSMRule(&ctx, &nac, &MonitorFSM[index]);

	return true;
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
 * --- [0, MonitorFSM_EarlyChecksStart): MONITOR_FSM_SECTION_API_TRIGGERED,
 * pos 101-1xx. Reached only via ProceedGroupStateForApiTrigger(), never via
 * the ordinary node_active() heartbeat path -- every row here requires a
 * specific non-NONE .conditions.apiTrigger, which an ordinary heartbeat
 * call's implicit API_FUNCTION_NONE can never match (MatchApiTrigger). See
 * that function's own comment (just above this array) for the operator
 * side's dispatch shape and why a no-match there is always ereport(ERROR),
 * unlike the heartbeat side below.
 *
 * --- [MonitorFSM_EarlyChecksStart, MonitorFSM_FromContextStart): the six
 * checks the real if-chain runs BEFORE the IsInPrimaryState(activeNode)
 * early return (group_state_machine.c:284) -- DROPPED, goal==DROPPED,
 * MAINTENANCE, the demote_timeout self-fence, and both "alone in group"
 * rows. These fire regardless of whether activeNode is currently the
 * primary (a primary that just lost its only standby must still reach
 * SINGLE here, before ever redirecting into the
 * ProceedGroupStateForPrimaryNode section) -- confirmed by the drop_node
 * regression test, which failed the first time this table put the
 * primary-state redirect ahead of these six checks instead of after them.
 * None of these six rows reference .primaryNode at all, so they can be
 * matched against a NodeActiveContext built with primaryNode == NULL, before
 * primaryNode is even resolved.
 */
static const MonitorFSMTransition MonitorFSM[] = {
	/*
	 * remove_node(), node_active_protocol.c:1163-1270 (RemoveNode) --
	 * primary being removed: fan out report_lsn to every surviving,
	 * non-maintenance standby (extraAction, runs first), then mark the
	 * removed node itself dropped (activeNodeAssignedState, runs after --
	 * matches the real source's own order). canTakeWrites, not
	 * isInPrimaryState: RemoveNode's own guard is CanTakeWritesInState(
	 * currentNode->goalState) with no requirement that reportedState has
	 * converged -- see canTakeWrites's own comment on NodeMatchesPattern
	 * above. Must come before the catchall row below: first-match-wins
	 * dispatch means whichever row is tried first wins when both could
	 * apply, and only a node that canTakeWrites should get the fan-out.
	 */
	{ .pos = 101, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_REMOVE_NODE) },
	  .activeNode = { .canTakeWrites = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_DROPPED),
	  .extraAction = ActionFanOutReportLsnOnPrimaryRemoval,
	  .comment = "remove_node, removed node can take writes -> dropped, "
				 "every surviving non-maintenance standby joins report_lsn" },

	/*
	 * remove_node(), the removed node itself when it's NOT the primary --
	 * unconditional at this point in the real source (the "already
	 * DROPPED" idempotency case returns earlier, before dispatch is ever
	 * called; see RemoveNode()'s own pre-checks, kept hand-written). Note
	 * this is doc-corrected from an earlier draft of this table, which
	 * modeled the fan-out row above and this row as two competing
	 * alternatives under first-match-wins -- that would have skipped
	 * assigning DROPPED to a removed *primary* entirely, since the row
	 * above would already have matched and stopped dispatch. The real
	 * source does both unconditionally in sequence (fan out, THEN mark
	 * dropped), not as alternatives -- reflected here by having the row
	 * above do both itself, and this row only needing to cover the
	 * non-primary case that never matched the row above at all.
	 */
	{ .pos = 103, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_REMOVE_NODE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_DROPPED),
	  .comment = "remove_node, removed node cannot take writes -> dropped" },

	/*
	 * perform_failover(), 2-node group -- node_active_protocol.c:1456-1554.
	 * The SQL wrapper resolves the sole standby and validates
	 * candidatePriority != 0 and both nodes converged BEFORE dispatch (an
	 * operator-facing ereport(ERROR) on failure, not a table row -- see
	 * ProceedGroupStateForApiTrigger's own comment on pre/post side
	 * effects); by the time dispatch runs, activeNode is that standby.
	 */
	{ .pos = 105, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_PERFORM_FAILOVER),
					  .groupHasExactlyTwoNodes = BOOL_TRUE },
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DRAINING),
	  .comment = "manual failover, 2-node group, primary+standby both converged -> "
				 "standby prepare_promotion, primary draining" },

	/*
	 * perform_failover(), >2-node group -- node_active_protocol.c:1555-1601.
	 * No standby is named at this point in the real source at all -- there's
	 * nothing else for activeNode to be here but the primary itself, the
	 * one node this specific call is fundamentally about. The
	 * candidatePriority trick and the ProceedGroupState(firstStandbyNode)
	 * continuation are POST side effects, hand-written in perform_failover()
	 * itself after this row's own DRAINING assignment has committed -- not
	 * modeled as a further declarative row or extraAction, since they're
	 * about biasing an election already left to the heartbeat-driven
	 * MS-failover cluster rows, not a goal-state assignment of their own.
	 */
	{ .pos = 107, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_PERFORM_FAILOVER),
					  .groupHasMoreThanTwoNodes = BOOL_TRUE },
	  .activeNode = { .isInPrimaryState = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_DRAINING),
	  .comment = "manual failover, >2-node group -> primary drains, election proceeds "
				 "via the heartbeat-driven MS-failover cluster rows" },

	/*
	 * start_maintenance(), primary, 2-node group --
	 * node_active_protocol.c:1901-1934. The WARNING about blocking writes,
	 * the candidatesCount<1 guard, and the already-in-maintenance
	 * idempotency check all stay hand-written pre-dispatch, exactly where
	 * they already are. The firstStandbyNode -> prepare_promotion
	 * assignment is a POST side effect, hand-written in start_maintenance()
	 * itself: unlike the >2-node row below, it has no ordering dependency on
	 * this row's own assignment (neither reads the other's freshly-committed
	 * state), so it doesn't need extraAction to sequence correctly.
	 */
	{ .pos = 109, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_START_MAINTENANCE),
					  .groupHasExactlyTwoNodes = BOOL_TRUE },
	  .activeNode = { .isInPrimaryState = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_MAINTENANCE),
	  .comment = "start_maintenance, primary, 2-node group -> prepare_maintenance "
				 "(standby separately assigned prepare_promotion)" },

	/*
	 * start_maintenance(), primary, >2-node group --
	 * node_active_protocol.c:1936-1950. The ProceedGroupState(
	 * firstStandbyNode) continuation is a POST side effect, hand-written in
	 * start_maintenance() itself, called after this row's own
	 * prepare_maintenance assignment has committed (it re-fetches fresh
	 * state, so ordering matters here, unlike the 2-node row above).
	 */
	{ .pos = 111, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_START_MAINTENANCE),
					  .groupHasMoreThanTwoNodes = BOOL_TRUE },
	  .activeNode = { .isInPrimaryState = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_MAINTENANCE),
	  .comment = "start_maintenance, primary, >2-node group -> prepare_maintenance, "
				 "election proceeds via the heartbeat-driven MS-failover cluster rows" },

	/*
	 * start_maintenance(), secondary, last healthy sync standby --
	 * node_active_protocol.c:1973-1986. lastHealthySyncStandbyGoingToMaintenance
	 * is computed by BuildApiTriggerNodeActiveContext (see its own comment)
	 * only for this apiFunction, mirroring the real source's own
	 * number_sync_standbys==0 && secondaryNodesCount==1 &&
	 * IsHealthySyncStandby(currentNode) check verbatim. Must come before the
	 * ordinary-secondary row below: both match the same activeNode/
	 * primaryNode state pattern, and only the more specific condition should
	 * win.
	 */
	{ .pos = 113, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_START_MAINTENANCE),
					  .lastHealthySyncStandbyGoingToMaintenance = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_REPORTED,
										.reportedStates = STATES(
											REPLICATION_STATE_SECONDARY,
											REPLICATION_STATE_CATCHINGUP) }
	  },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_MAINTENANCE),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "start_maintenance, secondary, last healthy sync standby -> "
				 "wait_maintenance, primary wait_primary (disables sync rep)" },

	/*
	 * start_maintenance(), secondary, ordinary case --
	 * node_active_protocol.c:1987-1996.
	 */
	{ .pos = 115, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_START_MAINTENANCE) },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_REPORTED,
										.reportedStates = STATES(
											REPLICATION_STATE_SECONDARY,
											REPLICATION_STATE_CATCHINGUP) }
	  },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "start_maintenance, secondary, ordinary case -> maintenance" },

	/*
	 * stop_maintenance(), no primary at all -- node_active_protocol.c:
	 * 2090-2102. totalNodesCount==1 (skip dispatch, direct
	 * ProceedGroupState(currentNode)) and primaryNode==NULL&&totalNodesCount
	 * ==2 (ereport(ERROR)) both stay hand-written pre-dispatch branches in
	 * stop_maintenance() itself -- by the time this row's own dispatch call
	 * runs, primaryNode==NULL only happens with totalNodesCount>2 (the real
	 * source's own condition, `(primaryNode == NULL || IsDemotedPrimary(
	 * primaryNode)) && totalNodesCount > 2`, but the >2 half of that
	 * disjunct is redundant here since the 2-node&&NULL case never reaches
	 * dispatch at all, per the guard above).
	 */
	{ .pos = 117, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_STOP_MAINTENANCE) },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "stop_maintenance, no primary -> report_lsn" },

	/*
	 * stop_maintenance(), primary fully demoted -- node_active_protocol.c:
	 * 2103-2125 (both the >2-node and ==2-node demoted-primary branches:
	 * they assign the identical report_lsn outcome, differing only in log
	 * message text, so this one row covers both -- isDemotedPrimary alone,
	 * with no node-count condition, is exactly their shared real
	 * condition).
	 */
	{ .pos = 119, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_STOP_MAINTENANCE) },
	  .primaryNode = { .isDemotedPrimary = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "stop_maintenance, primary demoted -> report_lsn" },

	/*
	 * stop_maintenance(), failover in progress -- node_active_protocol.c:
	 * 2133-2142. The real source's own LogAndNotifyMessage text says
	 * "catchingup" here, but the actual SetNodeGoalState call assigns
	 * REPORT_LSN -- a real, pre-existing message/behavior mismatch in the
	 * source, not a modeling error in this table.
	 */
	{ .pos = 121, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_STOP_MAINTENANCE),
					  .failoverInProgress = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "stop_maintenance, failover in progress -> report_lsn (source's own "
				 "log message says \"catchingup\" here, but the actual call assigns "
				 "REPORT_LSN -- see this row's own comment above)" },

	/*
	 * stop_maintenance(), ordinary case -- node_active_protocol.c:2143-2152.
	 * Catchall: reached only once primaryNode exists, isn't demoted, and no
	 * failover is in progress -- exactly the real source's own final
	 * "else" branch.
	 */
	{ .pos = 123, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(API_FUNCTION_STOP_MAINTENANCE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "stop_maintenance, ordinary case -> catchingup" },

	/*
	 * set_node_candidate_priority(), node_active_protocol.c:2282-2296.
	 * activeNode IS the primary here (mirrors BuildForPrimaryNodeNodeActiveContext's
	 * own "primaryNode IS activeNode" convention): the row's only real target
	 * is the primary's own apply_settings transition. The "primary is
	 * already apply_settings" ereport(ERROR) and the "no primary yet, just
	 * proceed" no-op both stay hand-written pre-dispatch in
	 * set_node_candidate_priority() itself, exactly where they already are
	 * -- this row is only reached once the wrapper has confirmed a primary
	 * exists and isn't already apply_settings.
	 */
	{ .pos = 125, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(
						  API_FUNCTION_SET_NODE_CANDIDATE_PRIORITY) },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_NOT_STABLE,
										.reportedStates = STATES(
											REPLICATION_STATE_APPLY_SETTINGS) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_APPLY_SETTINGS),
	  .comment = "set_node_candidate_priority, primary not already apply_settings -> "
				 "apply_settings" },

	/*
	 * set_node_replication_quorum(), node_active_protocol.c:2427-2441. Same
	 * shape as set_node_candidate_priority above.
	 */
	{ .pos = 127, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(
						  API_FUNCTION_SET_NODE_REPLICATION_QUORUM) },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_NOT_STABLE,
										.reportedStates = STATES(
											REPLICATION_STATE_APPLY_SETTINGS) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_APPLY_SETTINGS),
	  .comment = "set_node_replication_quorum, primary not already apply_settings -> "
				 "apply_settings" },

	/*
	 * set_formation_number_sync_standbys(), formation_metadata.c:591-606,639.
	 * The "primary not in primary/wait_primary state" ereport(ERROR) stays
	 * hand-written pre-dispatch, exactly where it already is -- kept as this
	 * row's own condition too (belt and suspenders: dump_fsm() should show
	 * what's actually valid, and a future drift between the two fails loudly
	 * via this row's own no-match ERROR rather than silently).
	 */
	{ .pos = 129, .section = MONITOR_FSM_SECTION_API_TRIGGERED,
	  .conditions = { .apiTrigger = API_TRIGGER(
						  API_FUNCTION_SET_FORMATION_NUMBER_SYNC_STANDBYS) },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_ASSIGNED,
										.assignedStates = STATES(
											REPLICATION_STATE_PRIMARY,
											REPLICATION_STATE_WAIT_PRIMARY) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_APPLY_SETTINGS),
	  .comment = "set_formation_number_sync_standbys, primary in primary/wait_primary "
				 "-> apply_settings" },

	/* converged to dropped -> remove the node from the catalog entirely */
	{ .pos = 201, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DROPPED) },
	  .extraAction = ActionRemoveDroppedNode,
	  .comment = "converged to dropped -> remove the node from the catalog" },

	/* goal already dropped (mid-drop, row above hasn't converged yet) -> no-op */
	{ .pos = 203, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_DROPPED_GOAL },
	  .comment = "goal already dropped -> no-op" },

	/* converged to maintenance -> no-op, frozen until stop_maintenance() */
	{ .pos = 205, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_MAINTENANCE) },
	  .comment = "converged to maintenance -> no-op, frozen until stop_maintenance()" },

	/* demote_timeout self-fence re-target (issue #1025) */
	{ .pos = 207, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_REPORTED_DEMOTE_TIMEOUT,
					  .unreachableFromDemoteTimeout = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "reported demote_timeout, assigned goal can't reach it -> demoted" },

	/* alone in group, candidate-eligible */
	{ .pos = 209, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_NOT_STABLE_SINGLE,
					  .candidateEligible = BOOL_TRUE },
	  .conditions = { .groupHasExactlyOneNode = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SINGLE),
	  .comment = "alone in group, candidate-eligible -> single" },

	/* alone in group, not candidate-eligible */
	{ .pos = 211, .section = MONITOR_FSM_SECTION_EARLY_CHECKS,
	  .activeNode = { .statePattern = FSM_NOT_STABLE_SINGLE,
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
	{ .pos = 301, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isComparableToReferenceTli = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment =
		  "converged secondary, reportedTLI not an ancestor of reference -> catchingup" },

	/* replication stall (#997): primary healthy, no standby past replication_stall_timeout */
	{ .pos = 303, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .primaryNode = { .isInPrimaryState = BOOL_TRUE,
					   .isHealthy = BOOL_TRUE },
	  .conditions = { .replicationStallExceeded = BOOL_TRUE },
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment =
		  "primary healthy, no standby past replication_stall_timeout -> wait_primary" },

	/* nodesCount>2, primary unhealthy -- draining/maintenance/nothing decided inside the
	 * action, then the MS-failover cascade unconditionally; must be a single row, see
	 * ActionRunMultiStandbyFailoverCascade's comment for why. */
	{ .pos = 305, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .primaryNode = { .isUnhealthy = BOOL_TRUE },
	  .conditions = { .groupHasMoreThanTwoNodes = BOOL_TRUE },
	  .extraAction = ActionRunMultiStandbyFailoverCascade,
	  .comment =
		  "nodesCount>2, primary unhealthy -> draining/maintenance + MS-failover cascade" },

	/* report_lsn, primary converged wait/join_primary, healthy */
	{ .pos = 307, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment =
		  "report_lsn, primary converged wait/join_primary, healthy -> secondary" },

	/* report_lsn, primary converged primary, healthy */
	{ .pos = 309, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY),
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "report_lsn, primary converged primary, healthy -> secondary" },

	/* fast_forward done -> prepare_promotion */
	{ .pos = 311, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_FAST_FORWARD) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .comment = "fast_forward done -> prepare_promotion" },

	/* continue an already-started MS failover -- a direct `return` in the real source, and no
	 * later row in this table matches activeNode in REPORT_LSN/FAST_FORWARD, so it doesn't
	 * matter here whether the extraAction's bool stops dispatch or lets it keep scanning. */
	{ .pos = 313, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_REPORT_LSN_OR_FAST_FORWARD },
	  .extraAction = ActionRunPlainMSFailoverCascade,
	  .comment = "report_lsn or fast_forward, continuing an already-started failover -> "
				 "MS-failover cascade" },

	/* wait_standby, primary converged wait/join_primary */
	{ .pos = 315, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment = "wait_standby, primary converged wait/join_primary -> catchingup" },

	/* wait_standby (quorum member), primary converged primary */
	{ .pos = 317, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY),
					  .replicationQuorum = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_APPLY_SETTINGS),
	  .comment = "wait_standby (quorum member), primary converged primary -> "
				 "catchingup + apply_settings" },

	/* wait_standby (not a quorum member), primary converged primary */
	{ .pos = 319, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_STANDBY),
					  .replicationQuorum = BOOL_FALSE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment =
		  "wait_standby (not a quorum member), primary converged primary -> catchingup" },

	/* caught up, same TLI as primary, within sync threshold */
	{ .pos = 321, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_CATCHINGUP),
					  .isHealthy = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_PRIMARY_OR_WAIT_OR_JOIN },
	  .conditions = { .activeAndPrimaryTliMatch = BOOL_TRUE,
					  .walWithinSyncThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "caught up, same TLI as primary, within sync threshold -> secondary" },

	/* primary fails, already converged wait_primary (no draining edge, issue #1168) */
	{ .pos = 323, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isHealthy = BOOL_TRUE,
					  .candidateEligible = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY),
					   .isUnhealthy = BOOL_TRUE },
	  .conditions = { .walWithinPromoteThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .comment = "primary fails, already converged wait_primary (issue #1168) -> "
				 "secondary -> prepare_promotion only (1 of 2)" },

	/* primary fails, not already wait_primary */
	{ .pos = 325, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SECONDARY),
					  .isHealthy = BOOL_TRUE,
					  .candidateEligible = BOOL_TRUE },
	  .primaryNode = { .statePattern = FSM_NOT_STABLE_WAIT_PRIMARY,
					   .isInPrimaryState = BOOL_TRUE,
					   .isUnhealthy = BOOL_TRUE },
	  .conditions = { .walWithinPromoteThreshold = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DRAINING),
	  .comment =
		  "primary fails, not already wait_primary -> secondary -> prepare_promotion, "
		  "primary -> draining (2 of 2)" },

	/* wait_maintenance, primary converged wait_primary */
	{ .pos = 327, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_MAINTENANCE) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "wait_maintenance, primary converged wait_primary -> maintenance" },

	/* wait_maintenance, primary's goal no longer wait_primary */
	{ .pos = 329, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_MAINTENANCE) },
	  .primaryNode = { .statePattern = FSM_NOT_ASSIGNED_WAIT_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment =
		  "wait_maintenance, primary's goal no longer wait_primary -> maintenance" },

	/* prepare_promotion, primary converged prepare_maintenance */
	{ .pos = 331, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_MAINTENANCE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .comment =
		  "prepare_promotion, primary converged prepare_maintenance -> stop_replication" },

	/* Citus worker, primary present */
	{ .pos = 333, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment =
		  "Citus worker prepare_promotion, primary present -> wait_primary + demoted" },

	/* Citus worker, primary removed */
	{ .pos = 335, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "Citus worker prepare_promotion, primary removed -> wait_primary" },

	/* prepare_promotion, primary present, already converged wait_primary (issue #1168) */
	{ .pos = 337, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_TRUE,
					   .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY),
					   .isInMaintenance = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .comment =
		  "prepare_promotion, primary already converged wait_primary (issue #1168) -> "
		  "stop_replication only (1 of 2)" },

	/* prepare_promotion, primary present, not in maintenance, not already wait_primary */
	{ .pos = 339, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_TRUE,
					   .statePattern = FSM_NOT_STABLE_WAIT_PRIMARY,
					   .isInMaintenance = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_STOP_REPLICATION),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTE_TIMEOUT),
	  .comment = "prepare_promotion, primary present, not in maintenance -> "
				 "stop_replication + demote_timeout (2 of 2)" },

	/* prepare_promotion, primary removed */
	{ .pos = 341, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_PROMOTION) },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "prepare_promotion, primary removed -> wait_primary" },

	/* stop_replication, primary converged prepare_maintenance */
	{ .pos = 343, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PREPARE_MAINTENANCE) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_MAINTENANCE),
	  .comment = "stop_replication, primary converged prepare_maintenance -> "
				 "wait_primary + maintenance" },

	/* stop_replication, primary converged demote_timeout (3-way OR, 1 of 3) */
	{ .pos = 345, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTE_TIMEOUT) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary converged demote_timeout -> "
				 "wait_primary + demoted (1 of 3)" },

	/* stop_replication, primary's drain time expired (3-way OR, 2 of 3) */
	{ .pos = 347, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .primaryNode = { .drainTimeExpired = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary's drain time expired -> "
				 "wait_primary + demoted (2 of 3)" },

	/* stop_replication, primary's goal is wait_primary but presumed dead (3-way OR, 3 of 3) */
	{ .pos = 349, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION) },
	  .conditions = { .primaryIsWaitPrimaryPresumedDead = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment = "stop_replication, primary's goal wait_primary but presumed dead -> "
				 "wait_primary + demoted (3 of 3)" },

	/* Citus worker, primary present */
	{ .pos = 351, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .otherNodeAssignedState = GOAL(REPLICATION_STATE_DEMOTED),
	  .comment =
		  "Citus worker stop_replication, primary present -> wait_primary + demoted" },

	/* Citus worker, primary removed */
	{ .pos = 353, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_STOP_REPLICATION),
					  .isCitusWorkerGroup = BOOL_TRUE },
	  .primaryNode = { .exists = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "Citus worker stop_replication, primary removed -> wait_primary" },

	/* demoted, primary reported wait/join_primary with goal primary */
	{ .pos = 355, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTED) },
	  .primaryNode = { .statePattern = FSM_WAIT_OR_JOIN_PRIMARY_TRANSITIONING_TO_PRIMARY,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment =
		  "demoted, primary reported wait/join_primary with goal primary -> catchingup" },

	/* demoted, primary converged wait/join_primary/primary, healthy */
	{ .pos = 357, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_DEMOTED) },
	  .primaryNode = { .statePattern = FSM_PRIMARY_OR_WAIT_OR_JOIN,
					   .isHealthy = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_CATCHINGUP),
	  .comment =
		  "demoted, primary converged wait/join_primary/primary, healthy -> catchingup" },

	/* join_secondary, primary reported wait_primary with goal wait/primary -- cascades into a
	 * nested pass on primaryNode */
	{ .pos = 359, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_SECONDARY) },
	  .primaryNode = { .statePattern = FSM_WAIT_PRIMARY_TRANSITIONING_TO_PRIMARY },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .extraAction = ActionRunPrimaryNodeTransition,
	  .comment =
		  "join_secondary, primary reported wait_primary with goal wait/primary -> "
		  "secondary" },

	/* join_secondary, primary converged primary */
	{ .pos = 361, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_SECONDARY) },
	  .primaryNode = { .statePattern = FSM_STATE(REPLICATION_STATE_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_SECONDARY),
	  .comment = "join_secondary, primary converged primary -> secondary" },

	/*
	 * --- [MonitorFSM_MSFailoverStart, MonitorFSM_PrimaryNodeSectionStart):
	 * the MS-failover / candidate-selection cluster's two genuinely
	 * declarative transitions (see the design doc's "The MS-failover /
	 * candidate-selection cluster" section, and TryMSFailoverDeclarativeRow's
	 * own comment below): BuildCandidateList/SelectFailoverCandidateNode/
	 * PromoteSelectedNode themselves stay hand-written C, called from
	 * ProceedGroupStateForMSFailover exactly as before -- these two rows
	 * only cover the pair of assignments that were already expressible as
	 * plain per-node facts, gated by the exact same hand-written condition
	 * that already decided whether to make them (so a mismatch here can
	 * only widen the row's own no-op fallback, never change real behavior).
	 * Appended after the ordinary REPORTING_NODE rows rather than
	 * renumbered into them, so nothing else shifts.
	 */

	/* MS-failover: candidate stuck in fast_forward, all WAL sources unhealthy, retry */
	{ .pos = 363, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .activeNodeAllWalSourcesUnhealthy = BOOL_TRUE,
					  .guardDataLossEnabled = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_TRANSITIONING,
										.reportedStates = STATES(
											REPLICATION_STATE_REPORT_LSN),
										.assignedStates = STATES(
											REPLICATION_STATE_FAST_FORWARD) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment =
		  "MS-failover: candidate stuck in fast_forward, all WAL sources unhealthy, "
		  "guard_data_loss=true -> report_lsn (retry once a source recovers)" },

	/* MS-failover: candidate ready to stream WAL -> follower joins as secondary */
	{ .pos = 365, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .candidatePromotionInProgress = BOOL_TRUE },
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .candidateNode = { .isReadyToStreamWAL = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_JOIN_SECONDARY),
	  .comment =
		  "MS-failover: activeNode in report_lsn, failover candidate ready to stream "
		  "WAL -> join_secondary" },

	/*
	 * MS-failover: BuildCandidateList's own fan-out (group_state_machine.c,
	 * "Nodes in SECONDARY or CATCHINGUP states are candidates due to report
	 * their LSN..."). Every AssignGoalState call in that loop is dispatched
	 * through TryFanOutReportLsnRow (see its own comment) against exactly
	 * these four rows -- one per distinct fromState shape the real if-chain
	 * checks, since no single NodeStatePattern covers all three disjuncts
	 * (SECONDARY/CATCHINGUP transitioning, MAINTENANCE->CATCHINGUP,
	 * DRAINING/DEMOTED stable or DEMOTED->CATCHINGUP transitioning) at once.
	 * The loop's own skip conditions (old/new primary unless draining or
	 * demoted, unhealthy-and-not-reporting) stay exactly where they are,
	 * hand-written, ahead of these rows -- by the time one of these four is
	 * reached, the loop has already established the node is a legitimate
	 * fan-out target; these rows only need to name which state it's coming
	 * from, not re-derive that eligibility.
	 *
	 * Each carries .conditions.inMSFailoverCluster = BOOL_TRUE: unlike pos
	 * 363/365, none of these four has an activeNode-state pattern narrow
	 * enough on its own to stay clear of ProceedGroupStateFromContext's own
	 * ordinary top-level lookup, which shares this same section's upper
	 * bound (MonitorFSM_PrimaryNodeSectionStart) and would otherwise match
	 * them against any ordinary, non-MS-failover heartbeat whose reported/
	 * goal states happened to line up -- see inMSFailoverCluster's own
	 * comment on NodeActiveContext.
	 */
	{ .pos = 367, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_TRANSITIONING,
										.reportedStates = STATES(
											REPLICATION_STATE_SECONDARY,
											REPLICATION_STATE_CATCHINGUP),
										.assignedStates = STATES(
											REPLICATION_STATE_SECONDARY,
											REPLICATION_STATE_CATCHINGUP) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment =
		  "MS-failover fan-out: secondary/catchingup, not yet converged -> report_lsn "
		  "(1 of 4)" },

	{ .pos = 369, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_TRANSITIONING,
										.reportedStates = STATES(
											REPLICATION_STATE_MAINTENANCE),
										.assignedStates = STATES(
											REPLICATION_STATE_CATCHINGUP) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment =
		  "MS-failover fan-out: rejoining from maintenance -> report_lsn (2 of 4)" },

	{ .pos = 371, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_STABLE,
										.reportedStates = STATES(
											REPLICATION_STATE_DRAINING,
											REPLICATION_STATE_DEMOTED) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "MS-failover fan-out: old primary converged draining or demoted -> "
				 "report_lsn (3 of 4)" },

	{ .pos = 373, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE },
	  .activeNode = { .statePattern = { .kind = NODE_STATE_TRANSITIONING,
										.reportedStates = STATES(
											REPLICATION_STATE_DEMOTED),
										.assignedStates = STATES(
											REPLICATION_STATE_CATCHINGUP) }
	  },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_REPORT_LSN),
	  .comment = "MS-failover fan-out: old primary demoted, was rejoining a now-failed "
				 "primary -> report_lsn (4 of 4)" },

	/*
	 * MS-failover: PromoteSelectedNode's own two outcomes
	 * (group_state_machine.c). ResolveAcceptedTimeline and the
	 * candidatePriority resets stay hand-written side effects, exactly
	 * where they are; only the final AssignGoalState is dispatched, via
	 * DispatchMonitorFSMRuleByPos (see its own comment) rather than a
	 * RuleMatches search -- both rows below share an identical condition
	 * set (activeNode in report_lsn, no promotion already in flight, the
	 * pool's most-advanced candidate within the promote threshold), so
	 * first-match-wins can never distinguish them on its own. The real
	 * choice -- selectedNode->reportedLSN == candidateList->
	 * mostAdvancedReportedLSN, an internal LSN comparison no BoolPattern
	 * can express -- is made in PromoteSelectedNode itself, exactly as
	 * before; both rows exist so dump_fsm() shows both reachable outcomes,
	 * matching the design doc's own resolution of this exact ambiguity
	 * ("kept as 2 rows anyway, for dump_fsm() edge visibility... this pair
	 * genuinely isn't disambiguated by this table's own dispatch model").
	 */
	{ .pos = 375, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE,
					  .candidatePromotionInProgress = BOOL_FALSE,
					  .mostAdvancedCandidateWithinPromoteThreshold = BOOL_TRUE },
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PREPARE_PROMOTION),
	  .comment = "MS-failover: no promotion in flight, most-advanced candidate within "
				 "threshold, selected candidate already has all WAL -> prepare_promotion "
				 "(1 of 2 -- see this row's own comment on why both are listed)" },

	{ .pos = 377, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE,
					  .candidatePromotionInProgress = BOOL_FALSE,
					  .mostAdvancedCandidateWithinPromoteThreshold = BOOL_TRUE },
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_REPORT_LSN) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_FAST_FORWARD),
	  .comment = "MS-failover: no promotion in flight, most-advanced candidate within "
				 "threshold, selected candidate is lagging -> fast_forward (2 of 2)" },

	/* MS-failover: no promotion in flight, either not enough candidates have reported yet
	 * or the most-advanced one is still too far behind the primary to safely promote --
	 * a pure no-op besides BuildCandidateList's own fan-out (the four rows above); listed
	 * for dump_fsm() completeness, never itself dispatched (no assignment to attribute).
	 * inMSFailoverCluster=BOOL_TRUE despite that: without it this row's bare
	 * candidatePromotionInProgress=BOOL_FALSE (true by default everywhere) plus its
	 * missing activeNode state pattern (matches ANY state) would let it swallow the
	 * ordinary top-level lookup's own "BUG: no MonitorFSM row matches" detection for
	 * any otherwise-unmatched heartbeat, silently no-op'ing instead of erroring. */
	{ .pos = 379, .section = MONITOR_FSM_SECTION_REPORTING_NODE,
	  .conditions = { .inMSFailoverCluster = BOOL_TRUE,
					  .candidatePromotionInProgress = BOOL_FALSE },
	  .comment = "MS-failover: no promotion in flight, not enough (or not safe enough) "
				 "candidates yet -> no-op besides the fan-out above" },

	/* --- [MonitorFSM_PrimaryNodeSectionStart, MonitorFSM_SIZE): the declarative replacement
	 * for ProceedGroupStateForPrimaryNode()'s own sequential if-chain. Here .activeNode maps
	 * to the primaryNode parameter, not a reporting node -- reached either directly by the
	 * top-level driver (activeNode already primary-role) or via ActionRunPrimaryNodeTransition's
	 * nested pass on primaryNode (the join_secondary cascade row above). */

	/* primary alone, another node reached wait_standby */
	{ .pos = 401, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_SINGLE) },
	  .conditions = { .anyOtherNodeWaitingStandby = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .comment = "primary alone, another node reached wait_standby -> wait_primary" },

	/* all nodes async, zero secondaries */
	{ .pos = 403, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .conditions = { .replicationQuorumCountIsZero = BOOL_TRUE,
					  .secondaryNodesCountIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "all nodes async, zero secondaries -> wait_primary "
				 "(+ unhealthy-secondary fan-out to catchingup)" },

	/* all nodes async, >=1 secondary */
	{ .pos = 405, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .conditions = { .replicationQuorumCountIsZero = BOOL_TRUE,
					  .secondaryNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "all nodes async, >=1 secondary -> primary "
				 "(+ unhealthy-secondary fan-out to catchingup)" },

	/* converged primary/apply_settings (not wait_primary), no quorum secondaries,
	 * number_sync_standbys=0, no failover in progress (issue #774) */
	{ .pos = 407, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_PRIMARY_OR_APPLY_SETTINGS_ONLY },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_TRUE,
					  .failoverInProgress = BOOL_FALSE,
					  .numberSyncStandbysIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment =
		  "converged primary/apply_settings, no quorum secondaries, no failover in "
		  "progress, number_sync_standbys=0 -> wait_primary "
		  "(+ unhealthy-secondary fan-out to catchingup)" },

	/* same, but number_sync_standbys>0 -> block writes on primary */
	{ .pos = 409, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_PRIMARY_OR_APPLY_SETTINGS_ONLY },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_TRUE,
					  .failoverInProgress = BOOL_FALSE,
					  .numberSyncStandbysIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment =
		  "converged primary/apply_settings, no quorum secondaries, no failover in "
		  "progress, number_sync_standbys>0 -> primary (block writes) "
		  "(+ unhealthy-secondary fan-out to catchingup)" },

	/* wait_primary, >=1 quorum secondary */
	{ .pos = 411, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_WAIT_PRIMARY) },
	  .conditions = { .secondaryQuorumNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "wait_primary, >=1 quorum secondary -> primary "
				 "(+ unhealthy-secondary fan-out to catchingup)" },

	/* apply_settings, both zero */
	{ .pos = 413, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_TRUE,
					  .secondaryQuorumNodesCountIsZero = BOOL_TRUE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_WAIT_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment = "apply_settings, both zero -> wait_primary "
				 "(+ unhealthy-secondary fan-out to catchingup)" },

	/* apply_settings, number_sync_standbys != 0 (1 of 2 disjuncts) */
	{ .pos = 415, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment =
		  "apply_settings, number_sync_standbys != 0 -> primary (1 of 2 disjuncts) "
		  "(+ unhealthy-secondary fan-out to catchingup)" },

	/* apply_settings, sync_standbys=0 but >=1 quorum secondary (2 of 2) */
	{ .pos = 417, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_APPLY_SETTINGS) },
	  .conditions = { .numberSyncStandbysIsZero = BOOL_TRUE,
					  .secondaryQuorumNodesCountIsZero = BOOL_FALSE },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment =
		  "apply_settings, sync_standbys=0 but >=1 quorum secondary -> primary (2 of 2) "
		  "(+ unhealthy-secondary fan-out to catchingup)" },

	/* converged primary/wait_primary/apply_settings, no other condition applies */
	{ .pos = 419, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_PRIMARY_ROLE_STATES },
	  .extraAction = ActionCatchupUnhealthySecondaries,
	  .comment =
		  "converged primary/wait_primary/apply_settings, no other condition applies -> "
		  "no-op besides the unhealthy-secondary fan-out" },

	/* backwards-compat: join_primary -> primary */
	{ .pos = 421, .section = MONITOR_FSM_SECTION_PRIMARY_NODE,
	  .activeNode = { .statePattern = FSM_STATE(REPLICATION_STATE_JOIN_PRIMARY) },
	  .activeNodeAssignedState = GOAL(REPLICATION_STATE_PRIMARY),
	  .comment = "backwards-compat: join_primary -> primary" },
};

/*
 * AssertMonitorFSMWellFormed cross-checks the five MonitorFSM_* index
 * constants above against what the rows themselves declare via .pos/
 * .section, so a boundary that's drifted out of sync with an added,
 * removed, or reordered row is caught here -- loudly, at first use --
 * instead of silently, as a row from the wrong section matching
 * unexpectedly or a bounded search that scans zero rows and never matches.
 * A no-op build (USE_ASSERT_CHECKING off) skips this entirely, matching
 * every other structural check in this file (see AssignDeclaredGoalState).
 *
 * .pos is NOT the array index: each section's rows are numbered starting
 * at *01 within its own hundred-block (101/201/301/401, matching how many
 * MONITOR_FSM_SECTION_* values exist -- *01 rather than *00 so the count
 * within the block reads as ordinary 1-based), stepping by 2 within the
 * section rather than by 1 -- so a new row can be inserted between two
 * existing ones (e.g. 202 between 201 and 203) without renumbering
 * anything else in the file. What's checked here is weaker than "exactly
 * i*2 + sectionStart + 1" as a result: strictly increasing, and within the
 * section's own hundred-block -- consistent with rows being added over
 * time at whatever free position is nearest where they belong, not at a
 * specific computed slot.
 */
static void
AssertMonitorFSMWellFormed(void)
{
#ifdef USE_ASSERT_CHECKING
	int previousPos = 0;

	for (int i = 0; i < MonitorFSM_SIZE; i++)
	{
		Assert(MonitorFSM[i].pos > previousPos);
		previousPos = MonitorFSM[i].pos;
	}

	for (int i = 0; i < MonitorFSM_EarlyChecksStart; i++)
	{
		Assert(MonitorFSM[i].section == MONITOR_FSM_SECTION_API_TRIGGERED);
		Assert(MonitorFSM[i].pos >= 100 && MonitorFSM[i].pos < 200);
	}

	for (int i = MonitorFSM_EarlyChecksStart; i < MonitorFSM_FromContextStart; i++)
	{
		Assert(MonitorFSM[i].section == MONITOR_FSM_SECTION_EARLY_CHECKS);
		Assert(MonitorFSM[i].pos >= 200 && MonitorFSM[i].pos < 300);
	}

	for (int i = MonitorFSM_FromContextStart; i < MonitorFSM_PrimaryNodeSectionStart; i++)
	{
		Assert(MonitorFSM[i].section == MONITOR_FSM_SECTION_REPORTING_NODE);
		Assert(MonitorFSM[i].pos >= 300 && MonitorFSM[i].pos < 400);
	}

	for (int i = MonitorFSM_PrimaryNodeSectionStart; i < MonitorFSM_SIZE; i++)
	{
		Assert(MonitorFSM[i].section == MONITOR_FSM_SECTION_PRIMARY_NODE);
		Assert(MonitorFSM[i].pos >= 400 && MonitorFSM[i].pos < 500);
	}

	Assert(MonitorFSM_FromContextResumeStart > MonitorFSM_FromContextStart);
	Assert(MonitorFSM_FromContextResumeStart < MonitorFSM_PrimaryNodeSectionStart);
#endif
}


/*
 * MonitorFSMSectionGetName returns the (enum) name of a MonitorFSMSection --
 * the SQL-facing spelling used by pgautofailover.fsm_section, dump_fsm(),
 * and the rule_section column on pgautofailover.event. Mirrors
 * ReplicationStateGetName's role for ReplicationState exactly.
 */
const char *
MonitorFSMSectionGetName(MonitorFSMSection section)
{
	switch (section)
	{
		case MONITOR_FSM_SECTION_API_TRIGGERED:
		{
			return "api_triggered";
		}

		case MONITOR_FSM_SECTION_EARLY_CHECKS:
		{
			return "early_checks";
		}

		case MONITOR_FSM_SECTION_REPORTING_NODE:
		{
			return "reporting_node";
		}

		case MONITOR_FSM_SECTION_PRIMARY_NODE:
		{
			return "primary_node";
		}

		default:
		{
			ereport(ERROR,
					(errmsg("bug: unknown MonitorFSMSection (%d)", section)));
		}
	}
}


/*
 * MonitorApiFunctionGetName returns the (enum) name of a MonitorApiFunction --
 * the dump_fsm()/pgautofailover.fsm section column's own spelling (appended
 * after the section name, see MonitorFSMTransitionSectionText) for which
 * operator-triggered SQL entry point a MONITOR_FSM_SECTION_API_TRIGGERED
 * row's .conditions.apiTrigger requires. API_FUNCTION_NONE has no row that
 * matches on it specifically (it's the ordinary heartbeat path's implicit
 * value, never written as an explicit API_TRIGGER() in any row), but is
 * still given a name here rather than treated as an error, matching
 * MonitorFSMSectionGetName's own "every enumerator has a name" discipline.
 */
static const char *
MonitorApiFunctionGetName(MonitorApiFunction apiFunction)
{
	switch (apiFunction)
	{
		case API_FUNCTION_NONE:
		{
			return "node_active";
		}

		case API_FUNCTION_REMOVE_NODE:
		{
			return "remove_node";
		}

		case API_FUNCTION_PERFORM_FAILOVER:
		{
			return "perform_failover";
		}

		case API_FUNCTION_START_MAINTENANCE:
		{
			return "start_maintenance";
		}

		case API_FUNCTION_STOP_MAINTENANCE:
		{
			return "stop_maintenance";
		}

		case API_FUNCTION_SET_NODE_CANDIDATE_PRIORITY:
		{
			return "set_node_candidate_priority";
		}

		case API_FUNCTION_SET_NODE_REPLICATION_QUORUM:
		{
			return "set_node_replication_quorum";
		}

		case API_FUNCTION_SET_FORMATION_NUMBER_SYNC_STANDBYS:
		{
			return "set_formation_number_sync_standbys";
		}

		default:
		{
			ereport(ERROR,
					(errmsg("bug: unknown MonitorApiFunction (%d)", apiFunction)));
		}
	}
}


/*
 * MonitorFSMTransitionSectionText renders a row's section as the
 * dump_fsm()/pgautofailover.fsm section column: plain "reporting_node"-style
 * text for most rows, but "api_triggered: remove_node"-style text (section
 * name, ": ", API function name) whenever the row's own
 * .conditions.apiTrigger requires a specific operator-triggered entry point
 * (API_TRIGGER_SPECIFIC -- true for every row in
 * MONITOR_FSM_SECTION_API_TRIGGERED, and only those rows) -- one column
 * instead of a separate, mostly-NULL api_function column, since the two
 * facts are never independently meaningful: a row either has both a section
 * and no specific trigger, or both a section and exactly one trigger.
 */
static Datum
MonitorFSMTransitionSectionText(const MonitorFSMTransition *rule)
{
	const char *sectionName = MonitorFSMSectionGetName(rule->section);

	if (rule->conditions.apiTrigger.kind == API_TRIGGER_SPECIFIC)
	{
		StringInfoData buf;

		initStringInfo(&buf);
		appendStringInfo(&buf, "%s: %s", sectionName,
						 MonitorApiFunctionGetName(rule->conditions.apiTrigger.function));

		return CStringGetTextDatum(buf.data);
	}

	return CStringGetTextDatum(sectionName);
}


/*
 * MonitorFSMSectionTypeOid returns the OID of the pgautofailover.fsm_section
 * type. Mirrors ReplicationStateTypeOid exactly, see its own comment for why
 * the String/Value split exists.
 */
Oid
MonitorFSMSectionTypeOid(void)
{
#if (PG_VERSION_NUM >= 150000)
	String *schemaName = makeString(AUTO_FAILOVER_SCHEMA_NAME);
	String *typeName = makeString(FSM_SECTION_TYPE_NAME);
#else
	Value *schemaName = makeString(AUTO_FAILOVER_SCHEMA_NAME);
	Value *typeName = makeString(FSM_SECTION_TYPE_NAME);
#endif

	List *enumTypeNameList = list_make2(schemaName, typeName);
	TypeName *enumTypeName = makeTypeNameFromNameList(enumTypeNameList);
	Oid enumTypeOid = typenameTypeId(NULL, enumTypeName);

	return enumTypeOid;
}


/*
 * MonitorFSMSectionGetEnum returns the SQL enum OID for a given
 * MonitorFSMSection value. Mirrors ReplicationStateGetEnum exactly.
 */
Oid
MonitorFSMSectionGetEnum(MonitorFSMSection section)
{
	const char *enumName = MonitorFSMSectionGetName(section);
	Oid enumTypeOid = MonitorFSMSectionTypeOid();

	HeapTuple enumTuple = SearchSysCache2(ENUMTYPOIDNAME,
										  ObjectIdGetDatum(enumTypeOid),
										  CStringGetDatum(enumName));
	if (!HeapTupleIsValid(enumTuple))
	{
		ereport(ERROR, (errmsg("invalid value for enum: %d", section)));
	}

	Oid sectionOid = HeapTupleGetOid(enumTuple);

	ReleaseSysCache(enumTuple);

	return sectionOid;
}


/*
 * EnumGetMonitorFSMSection returns the internal value of a fsm_section enum.
 * Mirrors EnumGetReplicationState exactly.
 */
MonitorFSMSection
EnumGetMonitorFSMSection(Oid monitorFSMSectionOid)
{
	HeapTuple enumTuple = SearchSysCache1(ENUMOID,
										  ObjectIdGetDatum(monitorFSMSectionOid));
	if (!HeapTupleIsValid(enumTuple))
	{
		ereport(ERROR, (errmsg("invalid input value for enum: %u",
							   monitorFSMSectionOid)));
	}

	Form_pg_enum enumForm = (Form_pg_enum) GETSTRUCT(enumTuple);
	char *enumName = NameStr(enumForm->enumlabel);
	MonitorFSMSection section;

	if (strncmp(enumName, "api_triggered", NAMEDATALEN) == 0)
	{
		section = MONITOR_FSM_SECTION_API_TRIGGERED;
	}
	else if (strncmp(enumName, "early_checks", NAMEDATALEN) == 0)
	{
		section = MONITOR_FSM_SECTION_EARLY_CHECKS;
	}
	else if (strncmp(enumName, "reporting_node", NAMEDATALEN) == 0)
	{
		section = MONITOR_FSM_SECTION_REPORTING_NODE;
	}
	else if (strncmp(enumName, "primary_node", NAMEDATALEN) == 0)
	{
		section = MONITOR_FSM_SECTION_PRIMARY_NODE;
	}
	else
	{
		ReleaseSysCache(enumTuple);
		ereport(ERROR, (errmsg("bug: unknown fsm_section enum label \"%s\"", enumName)));
	}

	ReleaseSysCache(enumTuple);

	return section;
}


/*
 * NodeStatePatternReportedStatesText renders a NodeStatePattern's own
 * reportedStates set as a human- and script-readable comma-separated list
 * of pgautofailover.replication_state labels -- the "current (reported)
 * state" a row requires of the node this pattern is attached to, for a
 * keeper-side cross-check to compare against KeeperFSMTransition.current
 * (see fsm.h: current/assigned/pgKind is exactly the (fromState, toState)
 * shape this exists to expose the "from" half of).
 *
 * Only NODE_STATE_STABLE/REPORTED/TRANSITIONING pin down a genuine "current
 * state must be one of these" set -- returns NULL for NODE_STATE_ANY (no
 * constraint at all) and for NODE_STATE_NOT_STABLE/NOT_ASSIGNED (an
 * exclusion, not a "from" set: rendering reportedStates there would read as
 * the opposite of what the row actually requires).
 */
static Datum
NodeStatePatternReportedStatesText(const NodeStatePattern *pattern, bool *isNull)
{
	if (pattern->kind != NODE_STATE_STABLE &&
		pattern->kind != NODE_STATE_REPORTED &&
		pattern->kind != NODE_STATE_TRANSITIONING)
	{
		*isNull = true;
		return (Datum) 0;
	}

	StringInfoData buf;

	initStringInfo(&buf);

	for (int i = 0; i < pattern->reportedStates.count; i++)
	{
		if (i > 0)
		{
			appendStringInfoString(&buf, ", ");
		}

		appendStringInfoString(&buf,
							   ReplicationStateGetName(
								   pattern->reportedStates.states[i]));
	}

	*isNull = false;
	return CStringGetTextDatum(buf.data);
}


/* Appends "name=true"/"name=false" to buf (with a ", " separator when buf
 * isn't empty), skipping BOOL_ANY entirely -- BOOL_ANY means this row
 * doesn't care about the fact at all, so it's noise, not a guard. Shared by
 * NodeStatusPatternConditionsText and NodeActiveContextPatternConditionsText
 * below, one line per BoolPattern field rather than a loop: there's no
 * runtime array of (name, value) pairs to iterate, since the field names
 * only exist at compile time. */
#define APPEND_BOOL_CONDITION(buf, name, boolPattern) \
	do { \
		if ((boolPattern) != BOOL_ANY) \
		{ \
			if ((buf)->len > 0) \
			{ \
				appendStringInfoString((buf), ", "); \
			} \
			appendStringInfo((buf), "%s=%s", (name), \
							 (boolPattern) == BOOL_TRUE ? "true" : "false"); \
		} \
	} while (0)

/*
 * AppendNodeStateGoalCondition appends a role's own goal-state precondition
 * to buf, when its statePattern is NODE_STATE_ASSIGNED/NOT_ASSIGNED: "the
 * node's EXISTING goal (before this dispatch runs) must/must not already be
 * one of these" -- e.g. "goal=dropped" (pos 203, FSM_DROPPED_GOAL: goal
 * already DROPPED, reported state irrelevant) or "goal!=wait_primary" (the
 * wait_maintenance/primary's-goal-no-longer-wait_primary row).
 *
 * This is a genuine match condition, same as any BoolPattern field, but it
 * lives on .statePattern (a NodeStatePattern), not .conditions (a
 * NodeStatusPattern's BoolPattern fields) -- without this, a row like pos
 * 203 renders with every single column empty (no current_state, since
 * NodeStatePatternReportedStatesText only renders STABLE/REPORTED/
 * TRANSITIONING; no conditions, since ASSIGNED/NOT_ASSIGNED isn't a
 * BoolPattern; no assigned_state, since the row is a no-op), making it look
 * unconditional when it very much isn't. Folded into the same
 * *_conditions column as the BoolPattern fields (not the *_current_state
 * column, which is reserved for a strict "current reported state" fromState
 * a keeper-side cross-check can compare against KeeperFSMTransition.current)
 * since both are "things that must be true about this node" from a reader's
 * point of view.
 */
static void
AppendNodeStateGoalCondition(StringInfoData *buf, const NodeStatePattern *pattern)
{
	if (pattern->kind != NODE_STATE_ASSIGNED && pattern->kind != NODE_STATE_NOT_ASSIGNED)
	{
		return;
	}

	if (buf->len > 0)
	{
		appendStringInfoString(buf, ", ");
	}

	appendStringInfoString(buf, pattern->kind == NODE_STATE_ASSIGNED ? "goal=" :
						   "goal!=");

	for (int i = 0; i < pattern->assignedStates.count; i++)
	{
		if (i > 0)
		{
			appendStringInfoString(buf, "|");
		}

		appendStringInfoString(buf, ReplicationStateGetName(
								   pattern->assignedStates.states[i]));
	}
}


/*
 * NodeStatusPatternConditionsText renders every non-BOOL_ANY BoolPattern
 * field of a NodeStatusPattern (activeNode/primaryNode/candidateNode's own
 * per-node guards -- isHealthy foremost among them, per the reason this
 * column exists at all) as a compact "name=value, name=value" list, plus
 * this role's own goal-state precondition if it has one (see
 * AppendNodeStateGoalCondition's own comment). NULL when the row places no
 * per-node guard on this role at all -- the common case for rows that only
 * match on reported state.
 */
static Datum
NodeStatusPatternConditionsText(const NodeStatusPattern *pattern, bool *isNull)
{
	StringInfoData buf;

	initStringInfo(&buf);

	AppendNodeStateGoalCondition(&buf, &pattern->statePattern);

	APPEND_BOOL_CONDITION(&buf, "exists", pattern->exists);
	APPEND_BOOL_CONDITION(&buf, "isHealthy", pattern->isHealthy);
	APPEND_BOOL_CONDITION(&buf, "isUnhealthy", pattern->isUnhealthy);
	APPEND_BOOL_CONDITION(&buf, "candidateEligible", pattern->candidateEligible);
	APPEND_BOOL_CONDITION(&buf, "isInPrimaryState", pattern->isInPrimaryState);
	APPEND_BOOL_CONDITION(&buf, "isInMaintenance", pattern->isInMaintenance);
	APPEND_BOOL_CONDITION(&buf, "isDemotedPrimary", pattern->isDemotedPrimary);
	APPEND_BOOL_CONDITION(&buf, "canTakeWrites", pattern->canTakeWrites);
	APPEND_BOOL_CONDITION(&buf, "isReadyToStreamWAL", pattern->isReadyToStreamWAL);
	APPEND_BOOL_CONDITION(&buf, "drainTimeExpired", pattern->drainTimeExpired);
	APPEND_BOOL_CONDITION(&buf, "isCitusWorkerGroup", pattern->isCitusWorkerGroup);
	APPEND_BOOL_CONDITION(&buf, "replicationQuorum", pattern->replicationQuorum);
	APPEND_BOOL_CONDITION(&buf, "isComparableToReferenceTli",
						  pattern->isComparableToReferenceTli);
	APPEND_BOOL_CONDITION(&buf, "unreachableFromDemoteTimeout",
						  pattern->unreachableFromDemoteTimeout);

	if (buf.len == 0)
	{
		*isNull = true;
		return (Datum) 0;
	}

	*isNull = false;
	return CStringGetTextDatum(buf.data);
}


/*
 * NodeActiveContextPatternConditionsText renders every non-BOOL_ANY
 * BoolPattern field of a row's .conditions (NodeActiveContextPattern --
 * group-level guards: counts, WAL thresholds, guard_data_loss, the
 * MS-failover cluster's own facts) the same way. .apiTrigger is
 * deliberately skipped: which operator function triggers a row is already
 * implied by its section (api_triggered) and named in its own comment, so
 * repeating it here would be noise, not a guard a reader doesn't already
 * have.
 */
static Datum
NodeActiveContextPatternConditionsText(const NodeActiveContextPattern *cond, bool *isNull)
{
	StringInfoData buf;

	initStringInfo(&buf);

	APPEND_BOOL_CONDITION(&buf, "groupHasExactlyOneNode", cond->groupHasExactlyOneNode);
	APPEND_BOOL_CONDITION(&buf, "groupHasExactlyTwoNodes", cond->groupHasExactlyTwoNodes);
	APPEND_BOOL_CONDITION(&buf, "groupHasMoreThanTwoNodes",
						  cond->groupHasMoreThanTwoNodes);
	APPEND_BOOL_CONDITION(&buf, "anyOtherNodeWaitingStandby",
						  cond->anyOtherNodeWaitingStandby);
	APPEND_BOOL_CONDITION(&buf, "numberSyncStandbysIsZero",
						  cond->numberSyncStandbysIsZero);
	APPEND_BOOL_CONDITION(&buf, "replicationQuorumCountIsZero",
						  cond->replicationQuorumCountIsZero);
	APPEND_BOOL_CONDITION(&buf, "secondaryNodesCountIsZero",
						  cond->secondaryNodesCountIsZero);
	APPEND_BOOL_CONDITION(&buf, "secondaryQuorumNodesCountIsZero",
						  cond->secondaryQuorumNodesCountIsZero);
	APPEND_BOOL_CONDITION(&buf, "atLeastOneHealthyCandidate",
						  cond->atLeastOneHealthyCandidate);
	APPEND_BOOL_CONDITION(&buf, "walWithinPromoteThreshold",
						  cond->walWithinPromoteThreshold);
	APPEND_BOOL_CONDITION(&buf, "walWithinSyncThreshold", cond->walWithinSyncThreshold);
	APPEND_BOOL_CONDITION(&buf, "activeAndPrimaryTliMatch",
						  cond->activeAndPrimaryTliMatch);
	APPEND_BOOL_CONDITION(&buf, "primaryIsWaitPrimaryPresumedDead",
						  cond->primaryIsWaitPrimaryPresumedDead);
	APPEND_BOOL_CONDITION(&buf, "failoverInProgress", cond->failoverInProgress);
	APPEND_BOOL_CONDITION(&buf, "replicationStallExceeded",
						  cond->replicationStallExceeded);
	APPEND_BOOL_CONDITION(&buf, "lastHealthySyncStandbyGoingToMaintenance",
						  cond->lastHealthySyncStandbyGoingToMaintenance);
	APPEND_BOOL_CONDITION(&buf, "activeNodeAllWalSourcesUnhealthy",
						  cond->activeNodeAllWalSourcesUnhealthy);
	APPEND_BOOL_CONDITION(&buf, "candidatePromotionInProgress",
						  cond->candidatePromotionInProgress);
	APPEND_BOOL_CONDITION(&buf, "mostAdvancedCandidateWithinPromoteThreshold",
						  cond->mostAdvancedCandidateWithinPromoteThreshold);
	APPEND_BOOL_CONDITION(&buf, "guardDataLossEnabled", cond->guardDataLossEnabled);
	APPEND_BOOL_CONDITION(&buf, "inMSFailoverCluster", cond->inMSFailoverCluster);

	if (buf.len == 0)
	{
		*isNull = true;
		return (Datum) 0;
	}

	*isNull = false;
	return CStringGetTextDatum(buf.data);
}


PG_FUNCTION_INFO_V1(dump_fsm);

/*
 * dump_fsm exposes MonitorFSM[] to SQL, one row per rule, in table order
 * (first-match-wins order -- the same order RuleMatches() itself scans in).
 * This is the cross-check surface the design doc's dump_fsm()/
 * check_fsm_reachability() proposal calls for: enough to correlate a
 * pgautofailover.event row's rule_pos/rule_section (see notifications.h)
 * back to the exact rule that produced it, and enough for an operator or a
 * future keeper-side reachability check to see every transition the
 * monitor's table can produce, without reading the C source. The
 * active_node_current_state/other_node_current_state/candidate_node_current_state
 * columns (rendered via NodeStatePatternReportedStatesText, see its own
 * comment) plus the assigned-state columns already here give a keeper-
 * cross-check tool the full (fromState, toState) shape it needs to compare
 * against KeeperFSMTransition. The *_conditions columns (rendered via
 * NodeStatusPatternConditionsText/NodeActiveContextPatternConditionsText,
 * see their own comments) additionally surface every non-default BoolPattern
 * guard on the row -- health foremost among them, but also maintenance,
 * candidate eligibility, WAL thresholds, guard_data_loss, and the rest --
 * so a reader (or a future automated check) doesn't have to open the C
 * source to see what else has to be true for a given row to fire. The
 * section column (rendered via MonitorFSMTransitionSectionText) also names
 * which operator-triggered SQL entry point a MONITOR_FSM_SECTION_API_TRIGGERED
 * row requires, e.g. "api_triggered: remove_node" -- just the plain section
 * name for every other row, since only that section's rows carry a specific
 * .conditions.apiTrigger.
 */
Datum
dump_fsm(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that "
						"cannot accept a set")));
	}

	if (!(rsinfo->allowedModes & SFRM_Materialize))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));
	}

	TupleDesc tupdesc;

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR,
				(errmsg("function returning record called in context "
						"that cannot accept type record")));
	}

	MemoryContext perQueryContext = rsinfo->econtext->ecxt_per_query_memory;
	MemoryContext oldContext = MemoryContextSwitchTo(perQueryContext);

	Tuplestorestate *tupstore = tuplestore_begin_heap(true, false, work_mem);

	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldContext);

	for (int i = 0; i < MonitorFSM_SIZE; i++)
	{
		const MonitorFSMTransition *rule = &MonitorFSM[i];
		Datum values[13];
		bool isNull[13] = { false };

		values[0] = Int32GetDatum(rule->pos);
		values[1] = MonitorFSMTransitionSectionText(rule);

		if (rule->comment != NULL)
		{
			values[2] = CStringGetTextDatum(rule->comment);
		}
		else
		{
			isNull[2] = true;
		}

		values[3] = NodeStatePatternReportedStatesText(&rule->activeNode.statePattern,
													   &isNull[3]);
		values[4] = NodeStatePatternReportedStatesText(&rule->primaryNode.statePattern,
													   &isNull[4]);
		values[5] = NodeStatePatternReportedStatesText(&rule->candidateNode.statePattern,
													   &isNull[5]);

		values[6] = NodeStatusPatternConditionsText(&rule->activeNode, &isNull[6]);
		values[7] = NodeStatusPatternConditionsText(&rule->primaryNode, &isNull[7]);
		values[8] = NodeStatusPatternConditionsText(&rule->candidateNode, &isNull[8]);
		values[9] = NodeActiveContextPatternConditionsText(&rule->conditions, &isNull[9]);

		if (rule->activeNodeAssignedState.kind == GOAL_STATE_SET)
		{
			values[10] = ObjectIdGetDatum(
				ReplicationStateGetEnum(rule->activeNodeAssignedState.state));
		}
		else
		{
			isNull[10] = true;
		}

		if (rule->otherNodeAssignedState.kind == GOAL_STATE_SET)
		{
			values[11] = ObjectIdGetDatum(
				ReplicationStateGetEnum(rule->otherNodeAssignedState.state));
		}
		else
		{
			isNull[11] = true;
		}

		values[12] = BoolGetDatum(rule->extraAction != NULL);

		tuplestore_putvalues(tupstore, tupdesc, values, isNull);
	}

	return (Datum) 0;
}


/*
 * AllReplicationStates is the full universe of "real" (non-sentinel)
 * ReplicationState values -- every one a node can genuinely report or be
 * assigned, in the order replication_state.h declares them. Deliberately
 * excludes REPLICATION_STATE_UNKNOWN: a meta/sentinel value no MonitorFSM[]
 * row ever reports or assigns, and no KeeperFSM[] edge ever targets either.
 * Used only to resolve NodeStatePatternResolveFromStates' wildcard/negation
 * kinds (ANY, NOT_STABLE, ASSIGNED, NOT_ASSIGNED) into concrete states.
 */
static const ReplicationState AllReplicationStates[] = {
	REPLICATION_STATE_INITIAL,
	REPLICATION_STATE_SINGLE,
	REPLICATION_STATE_WAIT_PRIMARY,
	REPLICATION_STATE_PRIMARY,
	REPLICATION_STATE_DRAINING,
	REPLICATION_STATE_DEMOTE_TIMEOUT,
	REPLICATION_STATE_DEMOTED,
	REPLICATION_STATE_CATCHINGUP,
	REPLICATION_STATE_SECONDARY,
	REPLICATION_STATE_PREPARE_PROMOTION,
	REPLICATION_STATE_STOP_REPLICATION,
	REPLICATION_STATE_WAIT_STANDBY,
	REPLICATION_STATE_MAINTENANCE,
	REPLICATION_STATE_JOIN_PRIMARY,
	REPLICATION_STATE_APPLY_SETTINGS,
	REPLICATION_STATE_PREPARE_MAINTENANCE,
	REPLICATION_STATE_WAIT_MAINTENANCE,
	REPLICATION_STATE_REPORT_LSN,
	REPLICATION_STATE_FAST_FORWARD,
	REPLICATION_STATE_JOIN_SECONDARY,
	REPLICATION_STATE_DROPPED
};

#define ALL_REPLICATION_STATES_COUNT \
	((int) (sizeof(AllReplicationStates) / sizeof(AllReplicationStates[0])))


/*
 * NodeStatePatternResolveFromStates resolves a NodeStatePattern's own "from"
 * (reported-state) constraint into a concrete, palloc'd array of
 * ReplicationState values -- the genuine edge-source set dump_fsm_edges()
 * needs, as opposed to NodeStatePatternReportedStatesText's rendering (which
 * only handles STABLE/REPORTED/TRANSITIONING and returns NULL for
 * everything else, since that one exists for human display, not edge
 * enumeration).
 *
 * ASSIGNED/NOT_ASSIGNED only ever constrain the node's *goal*, never what it
 * currently reports (see each's own comment where declared above), so both
 * resolve to the full state universe here, same as ANY. NOT_STABLE resolves
 * to the complement of its own reportedStates within that universe. *outCount
 * is set to the returned array's length; the caller does not need to free it
 * (called only from dump_fsm_edges(), itself already running inside a
 * per-query memory context the executor resets on its own).
 */
static ReplicationState *
NodeStatePatternResolveFromStates(const NodeStatePattern *pattern, int *outCount)
{
	ReplicationState *out;

	switch (pattern->kind)
	{
		case NODE_STATE_STABLE:
		case NODE_STATE_REPORTED:
		case NODE_STATE_TRANSITIONING:
		{
			out = (ReplicationState *)
				  palloc(pattern->reportedStates.count * sizeof(ReplicationState));

			for (int i = 0; i < pattern->reportedStates.count; i++)
			{
				out[i] = pattern->reportedStates.states[i];
			}

			*outCount = pattern->reportedStates.count;
			return out;
		}

		case NODE_STATE_NOT_STABLE:
		{
			out = (ReplicationState *)
				  palloc(ALL_REPLICATION_STATES_COUNT * sizeof(ReplicationState));
			*outCount = 0;

			for (int i = 0; i < ALL_REPLICATION_STATES_COUNT; i++)
			{
				if (!MatchStateSet(AllReplicationStates[i], pattern->reportedStates))
				{
					out[(*outCount)++] = AllReplicationStates[i];
				}
			}

			return out;
		}

		case NODE_STATE_ANY:
		case NODE_STATE_ASSIGNED:
		case NODE_STATE_NOT_ASSIGNED:
		default:
		{
			out = (ReplicationState *)
				  palloc(ALL_REPLICATION_STATES_COUNT * sizeof(ReplicationState));
			memcpy(out, AllReplicationStates, sizeof(AllReplicationStates));
			*outCount = ALL_REPLICATION_STATES_COUNT;
			return out;
		}
	}
}


PG_FUNCTION_INFO_V1(dump_fsm_edges);

/*
 * dump_fsm_edges exposes MonitorFSM[] as a flat set of concrete
 * (pos, current_state, assigned_state) edges -- one row per actual
 * (reportedState, assignedState) pair a row can produce, fully resolved
 * (never NULL, unlike dump_fsm()'s own pattern-summary columns, which stay
 * unresolved for human display). This is the keeper-cross-check surface the
 * design doc's check_fsm_reachability() proposal needs: pgautofailover.
 * check_fsm_reachability(jsonb) anti-joins this against a keeper's own
 * KeeperFSM[] edges (see KeeperFSMToJSON(), src/bin/pg_autoctl/fsm.c) to
 * find any monitor transition with no matching keeper edge -- exactly the
 * bug class issue #774 was.
 *
 * Two potential edges per row, resolved independently: activeNode's own
 * (statePattern -> activeNodeAssignedState) when the row actually assigns
 * one, and primaryNode's own (statePattern -> otherNodeAssignedState) when
 * it does. candidateNode never has an assignment slot of its own (see
 * MonitorFSMTransition's own comment), so it never contributes an edge.
 * pgKind is deliberately not modeled here -- see this function's own header
 * comment in the design doc discussion; every Citus-specific KeeperFSM[]
 * edge already has a NODE_KIND_ANY counterpart with the same
 * (current, assigned) shape (fsm_mermaid.c's own comment establishes this
 * as an invariant this codebase already relies on elsewhere), so a flat,
 * pgKind-blind edge set on both sides is already correct.
 *
 * Two categories of edges are deliberately never emitted, both confirmed by
 * running pg_autoctl inspect fsm check for real and tracing every one of the
 * mismatches it reported back to its actual root cause rather than assuming:
 *
 * - Reflexive (current == assigned) edges. keeper_fsm_reach_assigned_state()
 *   (src/bin/pg_autoctl/fsm.c) returns true the moment
 *   current_role == assigned_role, before ever consulting KeeperFSM[] --
 *   confirmed by reading that function directly. A self-loop can therefore
 *   never have (or need) a matching keeper edge; including one here would
 *   always report a false gap. This alone explained every mismatch on pos
 *   363, 403, 405, 409 during that live run.
 *
 * - MONITOR_FSM_SECTION_API_TRIGGERED rows entirely. Every one of these is
 *   reached from an operator-facing SQL wrapper (remove_node,
 *   perform_failover, start_maintenance, ...) that resolves activeNode to a
 *   specific, already-validated role (almost always the primary) via
 *   hand-written C *before* dispatch ever runs -- ProceedGroupStateForApiTrigger's
 *   own comment, and several of these rows' own comments ("there's nothing
 *   else for activeNode to be here but the primary itself", "activeNode IS
 *   the primary here"), document this explicitly. The row's own
 *   NodeStatePattern for these (typically ANY, or a BoolPattern condition
 *   like isInPrimaryState with no accompanying state-set restriction) is
 *   consequently far broader than what's actually reachable: it was never
 *   meant to double as a full reachability precondition, because that
 *   precondition already lives in hand-written C outside this table, per
 *   this design's own "pre/post side effects stay hand-written, not modeled
 *   as a row" principle. Expanding it here (as this function otherwise
 *   correctly does for the ordinary heartbeat-driven sections) manufactures
 *   the exact same kind of false gap for every one of these ten rows, all
 *   confirmed by that same live run.
 *
 * Every other mismatch that same run found (early_checks 209/211,
 * reporting_node 303/325/333/339/347/349/351) stayed reachable from a live
 * node's own genuine reported state with no such structural excuse, so
 * they're deliberately NOT filtered here -- each is a real candidate for
 * individual investigation against the keeper's actual KeeperFSM[] rows,
 * not a known-safe artifact of this function's own edge derivation.
 */
Datum
dump_fsm_edges(PG_FUNCTION_ARGS)
{
	ReturnSetInfo *rsinfo = (ReturnSetInfo *) fcinfo->resultinfo;

	if (rsinfo == NULL || !IsA(rsinfo, ReturnSetInfo))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("set-valued function called in context that "
						"cannot accept a set")));
	}

	if (!(rsinfo->allowedModes & SFRM_Materialize))
	{
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("materialize mode required, but it is not "
						"allowed in this context")));
	}

	TupleDesc tupdesc;

	if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR,
				(errmsg("function returning record called in context "
						"that cannot accept type record")));
	}

	MemoryContext perQueryContext = rsinfo->econtext->ecxt_per_query_memory;
	MemoryContext oldContext = MemoryContextSwitchTo(perQueryContext);

	Tuplestorestate *tupstore = tuplestore_begin_heap(true, false, work_mem);

	rsinfo->returnMode = SFRM_Materialize;
	rsinfo->setResult = tupstore;
	rsinfo->setDesc = tupdesc;

	MemoryContextSwitchTo(oldContext);

	for (int i = 0; i < MonitorFSM_SIZE; i++)
	{
		const MonitorFSMTransition *rule = &MonitorFSM[i];

		if (rule->section == MONITOR_FSM_SECTION_API_TRIGGERED)
		{
			continue;
		}

		if (rule->activeNodeAssignedState.kind == GOAL_STATE_SET)
		{
			int count;
			ReplicationState *states =
				NodeStatePatternResolveFromStates(&rule->activeNode.statePattern, &count);

			for (int j = 0; j < count; j++)
			{
				Datum values[3];
				bool isNull[3] = { false };

				if (states[j] == rule->activeNodeAssignedState.state)
				{
					continue;
				}

				values[0] = Int32GetDatum(rule->pos);
				values[1] = ObjectIdGetDatum(ReplicationStateGetEnum(states[j]));
				values[2] = ObjectIdGetDatum(
					ReplicationStateGetEnum(rule->activeNodeAssignedState.state));

				tuplestore_putvalues(tupstore, tupdesc, values, isNull);
			}
		}

		if (rule->otherNodeAssignedState.kind == GOAL_STATE_SET)
		{
			int count;
			ReplicationState *states =
				NodeStatePatternResolveFromStates(&rule->primaryNode.statePattern,
												  &count);

			for (int j = 0; j < count; j++)
			{
				Datum values[3];
				bool isNull[3] = { false };

				if (states[j] == rule->otherNodeAssignedState.state)
				{
					continue;
				}

				values[0] = Int32GetDatum(rule->pos);
				values[1] = ObjectIdGetDatum(ReplicationStateGetEnum(states[j]));
				values[2] = ObjectIdGetDatum(
					ReplicationStateGetEnum(rule->otherNodeAssignedState.state));

				tuplestore_putvalues(tupstore, tupdesc, values, isNull);
			}
		}
	}

	return (Datum) 0;
}


/*
 * ProceedGroupStateFromContext is the core FSM logic, operating entirely on
 * the pre-built GroupStateContext.  It does not touch the database for reads;
 * writes (AssignGoalState, NotifyStateChange) still go to the DB.
 *
 * This separation lets test code inject a synthetic context and exercise the
 * FSM without a live database connection.
 *
 * Single-shot, two straight-line lookups at most -- matching the design
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

	if (FindAndDispatchMonitorFSMRule(ctx, &earlyNac, MonitorFSM_EarlyChecksStart,
									  MonitorFSM_FromContextStart))
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
 * BuildMSFailoverNodeActiveContext computes the facts the MS-failover
 * cluster's own declarative rows (MonitorFSM_MSFailoverStart onwards) need.
 * candidateNode is NULL at exactly one call site (TryFanOutReportLsnRow,
 * wrapping BuildCandidateList's own fan-out loop, which hasn't selected a
 * candidate yet) -- everywhere else (both call sites TryMSFailoverDeclarativeRow
 * wraps) it's non-NULL, called from within ProceedGroupStateForMSFailover's
 * own "nodeBeingPromoted != NULL" branch. Either way
 * candidatePromotionInProgress is exactly (candidateNode != NULL), and the
 * activeNodeAllWalSourcesUnhealthy computation below is skipped whenever
 * candidateNode is NULL, so passing NULL never risks matching it against
 * the wrong node.
 */
static void
BuildMSFailoverNodeActiveContext(GroupStateContext *ctx, AutoFailoverNode *activeNode,
								 AutoFailoverNode *candidateNode, NodeActiveContext *nac)
{
	memset(nac, 0, sizeof(NodeActiveContext));

	BuildNodeStatus(ctx, activeNode, &nac->activeNode);
	BuildNodeStatus(ctx, candidateNode, &nac->candidateNode);

	nac->inMSFailoverCluster = true;
	nac->guardDataLossEnabled = GuardDataLoss;
	nac->candidatePromotionInProgress = (candidateNode != NULL);

	if (candidateNode != NULL && candidateNode->nodeId == activeNode->nodeId)
	{
		nac->activeNodeAllWalSourcesUnhealthy =
			WalSourceNodesAreAllUnhealthy(ctx, ctx->groupNodeList, activeNode);
	}
}


/*
 * TryMSFailoverDeclarativeRow attempts the MS-failover cluster's own two
 * declarative rows (pos 363/365) for activeNode/candidateNode, returning
 * whether one matched and was dispatched. Callers only invoke this from
 * exactly the hand-written C condition the matched row's own conditions
 * mirror (see each call site's own comment) -- this is not a new source of
 * behavior, only a new, attributed path to the same AssignGoalState call
 * the hand-written code already made unconditionally at that point. A
 * false return (row's own conditions didn't line up with what the caller's
 * condition already established -- should not happen, but this file's own
 * "no dead ends in a hot path" principle applies here too) falls back to
 * the caller's own pre-existing plain AssignGoalState call, never to an
 * ereport(ERROR): unlike the operator-triggered API_TRIGGERED section,
 * this is reached from the ordinary node_active() heartbeat path, where a
 * hard error is not an acceptable failure mode.
 */
static bool
TryMSFailoverDeclarativeRow(GroupStateContext *ctx, AutoFailoverNode *activeNode,
							AutoFailoverNode *candidateNode)
{
	NodeActiveContext msNac;

	BuildMSFailoverNodeActiveContext(ctx, activeNode, candidateNode, &msNac);

	return FindAndDispatchMonitorFSMRule(ctx, &msNac, MonitorFSM_MSFailoverStart,
										 MonitorFSM_PrimaryNodeSectionStart);
}


/*
 * TryFanOutReportLsnRow attempts to dispatch BuildCandidateList's own
 * fan-out (pos 367/369/371/373) for a single node that the hand-written
 * loop has already determined is a legitimate report_lsn target. A thin
 * wrapper over TryMSFailoverDeclarativeRow with candidateNode=NULL, which
 * correctly leaves candidatePromotionInProgress false and skips the
 * activeNodeAllWalSourcesUnhealthy computation (see
 * BuildMSFailoverNodeActiveContext's own comment) -- so it can never
 * accidentally match pos 363/365/375/377/379, all of which require
 * activeNode to already be in report_lsn, which these fan-out nodes never
 * are yet. Falls back to the caller's own pre-existing AssignGoalState call
 * on no match, exactly like TryMSFailoverDeclarativeRow itself.
 */
static bool
TryFanOutReportLsnRow(GroupStateContext *ctx, AutoFailoverNode *node)
{
	return TryMSFailoverDeclarativeRow(ctx, node, NULL);
}


/*
 * DispatchMonitorFSMRuleByPos dispatches the single MonitorFSM[] row whose
 * .pos equals the given value, unconditionally -- no RuleMatches check.
 * Used exactly once, by PromoteSelectedNode (see pos 375/377's own
 * comment), for the one pair of rows in the whole table first-match-wins
 * can never disambiguate on its own: the caller has already made the real
 * choice (an internal LSN comparison no BoolPattern can express) before
 * calling this. Returns false if no row has that .pos -- should never
 * happen for a literal, hand-maintained pos value, but the caller still
 * falls back to its own pre-existing AssignGoalState call rather than
 * ereport(ERROR), matching TryMSFailoverDeclarativeRow's own "no dead ends
 * in a hot path" principle.
 */
static bool
DispatchMonitorFSMRuleByPos(GroupStateContext *ctx, NodeActiveContext *nac, int pos)
{
	for (int i = 0; i < MonitorFSM_SIZE; i++)
	{
		if (MonitorFSM[i].pos == pos)
		{
			DispatchMonitorFSMRule(ctx, nac, &MonitorFSM[i]);
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
					/*
					 * Dispatched through MonitorFSM[]'s own MS-failover
					 * declarative row (pos 363) when possible, for
					 * dump_fsm() visibility and rule_pos attribution --
					 * falling back to the plain AssignGoalState call below
					 * only if that row's own conditions somehow didn't
					 * line up with the ones just checked above (should
					 * never happen; see TryMSFailoverDeclarativeRow's own
					 * comment).
					 */
					if (!TryMSFailoverDeclarativeRow(ctx, activeNode, activeNode))
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
					}

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

			return ProceedWithMSFailover(ctx, activeNode, nodeBeingPromoted);
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

			return ProceedWithMSFailover(ctx, activeNode, nodeBeingPromoted);
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

		return PromoteSelectedNode(ctx, selectedNode,
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

			if (!TryFanOutReportLsnRow(ctx, node))
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of " NODE_FORMAT
					" to report_lsn to find the failover candidate",
					NODE_FORMAT_ARGS(node));

				AssignGoalState(node, REPLICATION_STATE_REPORT_LSN, message);
			}

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
ProceedWithMSFailover(GroupStateContext *ctx, AutoFailoverNode *activeNode,
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
		/*
		 * Dispatched through MonitorFSM[]'s own MS-failover declarative row
		 * (pos 365) when possible -- see TryMSFailoverDeclarativeRow's own
		 * comment; falls back to the plain AssignGoalState call below only
		 * if that row's own conditions somehow didn't line up with the
		 * ones just checked above.
		 */
		if (!TryMSFailoverDeclarativeRow(ctx, activeNode, candidateNode))
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
		}

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
PromoteSelectedNode(GroupStateContext *ctx,
					AutoFailoverNode *selectedNode,
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
		NodeActiveContext promotionNac;

		memset(&promotionNac, 0, sizeof(NodeActiveContext));
		BuildNodeStatus(ctx, selectedNode, &promotionNac.activeNode);

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

		if (!DispatchMonitorFSMRuleByPos(ctx, &promotionNac, 375))
		{
			AssignGoalState(selectedNode,
							REPLICATION_STATE_PREPARE_PROMOTION,
							message);
		}

		/* leave the other nodes in ReportLSN state for now */
		return true;
	}
	else
	{
		char message[BUFSIZE] = { 0 };
		NodeActiveContext promotionNac;

		memset(&promotionNac, 0, sizeof(NodeActiveContext));
		BuildNodeStatus(ctx, selectedNode, &promotionNac.activeNode);

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

		if (!DispatchMonitorFSMRuleByPos(ctx, &promotionNac, 377))
		{
			AssignGoalState(selectedNode,
							REPLICATION_STATE_FAST_FORWARD, message);
		}

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
