/*-------------------------------------------------------------------------
 *
 * src/monitor/node_active_protocol.c
 *
 * Implementation of the functions used to communicate with PostgreSQL
 * nodes.
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

#include "access/htup_details.h"
#include "access/xlogdefs.h"
#include "catalog/pg_enum.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "parser/parse_type.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/pg_lsn.h"
#include "utils/syscache.h"


/* private function forward declarations */
static AutoFailoverNodeState * NodeActive(char *formationId,
										  char *nodeName, int32 nodePort,
										  AutoFailoverNodeState *currentNodeState);
static void JoinAutoFailoverFormation(AutoFailoverFormation *formation,
									  char *nodeName, int nodePort,
									  AutoFailoverNodeState *currentNodeState);
static int AssignGroupId(AutoFailoverFormation *formation,
						 char *nodeName, int nodePort,
						 ReplicationState *initialState);

static bool IsStateIn(ReplicationState state, List *allowedStates);


/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_node);
PG_FUNCTION_INFO_V1(node_active);
PG_FUNCTION_INFO_V1(get_nodes);
PG_FUNCTION_INFO_V1(get_primary);
PG_FUNCTION_INFO_V1(get_other_nodes);
PG_FUNCTION_INFO_V1(remove_node);
PG_FUNCTION_INFO_V1(perform_failover);
PG_FUNCTION_INFO_V1(start_maintenance);
PG_FUNCTION_INFO_V1(stop_maintenance);
PG_FUNCTION_INFO_V1(set_node_candidate_priority);
PG_FUNCTION_INFO_V1(set_node_replication_quorum);

/*
 * register_node adds a node to a given formation
 *
 * At register time the monitor connects to the node to check that nodename and
 * nodeport are valid, and it does a SELECT pg_is_in_recovery() to help decide
 * what initial role to attribute the entering node.
 */
Datum
register_node(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(2);
	Name dbnameName = PG_GETARG_NAME(3);
	const char *expectedDBName = NameStr(*dbnameName);

	int32 currentGroupId = PG_GETARG_INT32(4);
	Oid currentReplicationStateOid = PG_GETARG_OID(5);

	text *nodeKindText = PG_GETARG_TEXT_P(6);
	char *nodeKind = text_to_cstring(nodeKindText);
	FormationKind expectedFormationKind =
		FormationKindFromNodeKindString(nodeKind);
	int candidatePriority = PG_GETARG_INT32(7);
	bool replicationQuorum = PG_GETARG_BOOL(8);

	AutoFailoverFormation *formation = NULL;
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	AutoFailoverNodeState currentNodeState = { 0 };
	AutoFailoverNodeState *assignedNodeState = NULL;

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[5];
	bool isNulls[5];

	checkPgAutoFailoverVersion();

	currentNodeState.nodeId = -1;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedLSN = 0;
	currentNodeState.candidatePriority = candidatePriority;
	currentNodeState.replicationQuorum = replicationQuorum;

	LockFormation(formationId, ExclusiveLock);

	formation = GetFormation(formationId);

	/*
	 * The default formationId is "default" and of kind FORMATION_KIND_PGSQL.
	 * It might get used to manage a formation though. Check about that here,
	 * and when the first node registered is a Citus node, update the target
	 * formation to be of kind Citus, actually.
	 */
	if (formation == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						errmsg("formation \"%s\" does not exists", formationId),
						errhint("Use `pg_autoctl create formation` "
								"to create the target formation first")));
	}

	if (formation->kind != expectedFormationKind)
	{
		List *allNodes = AllAutoFailoverNodes(formationId);

		if (list_length(allNodes) == 0)
		{
			/* first node in the list, let's switch to citus */
			SetFormationKind(formationId, expectedFormationKind);
		}
		else
		{
			ereport(ERROR,
					(errmsg("node %s:%d of kind \"%s\" can not be registered in "
							"formation \"%s\" of kind \"%s\"",
							nodeName, nodePort, nodeKind,
							formationId,
							FormationKindToString(formation->kind))));
		}
	}

	if (strncmp(formation->dbname, expectedDBName, NAMEDATALEN) != 0)
	{
		List *allNodes = AllAutoFailoverNodes(formationId);

		if (list_length(allNodes) == 0)
		{
			/* first node in the list, rename database and update formation */
			SetFormationDBName(formationId, expectedDBName);
			strlcpy(formation->dbname, expectedDBName, NAMEDATALEN);
		}
		else
		{
			ereport(ERROR,
					(errmsg("node %s:%d with dbname \"%s\" can not be registered in "
							"formation \"%s\" which expects dbname \"%s\"",
							nodeName, nodePort, expectedDBName,
							formationId,
							formation->dbname)));
		}
	}

	JoinAutoFailoverFormation(formation, nodeName, nodePort, &currentNodeState);
	LockNodeGroup(formationId, currentNodeState.groupId, ExclusiveLock);

	pgAutoFailoverNode = GetAutoFailoverNode(nodeName, nodePort);

	assignedNodeState =
		(AutoFailoverNodeState *) palloc0(sizeof(AutoFailoverNodeState));
	assignedNodeState->nodeId = pgAutoFailoverNode->nodeId;
	assignedNodeState->groupId = pgAutoFailoverNode->groupId;
	assignedNodeState->replicationState = pgAutoFailoverNode->goalState;
	assignedNodeState->candidatePriority = pgAutoFailoverNode->candidatePriority;
	assignedNodeState->replicationQuorum = pgAutoFailoverNode->replicationQuorum;

	/*
	 * Check that the state selected by the monitor matches the state required
	 * by the keeper, if any. REPLICATION_STATE_INITIAL means the monitor can
	 * pick whatever is needed now, depending on the groupId.
	 *
	 * The keeper might be confronted to an already existing Postgres instance
	 * that is running as a primary (not in recovery), and so asking to
	 * register as a SINGLE. Better error out than ask the keeper to remove
	 * some unknown data.
	 */
	if (currentNodeState.replicationState != REPLICATION_STATE_INITIAL)
	{
		if (currentNodeState.replicationState != pgAutoFailoverNode->goalState)
		{
			const char *currentState =
				ReplicationStateGetName(currentNodeState.replicationState);
			const char *goalState =
				ReplicationStateGetName(pgAutoFailoverNode->goalState);

			ereport(ERROR,
					(errmsg("node %s:%d can not be registered in state %s, "
							"it should be in state %s",
							nodeName, nodePort, currentState, goalState)));
		}
	}

	ProceedGroupState(pgAutoFailoverNode);

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(assignedNodeState->nodeId);
	values[1] = Int32GetDatum(assignedNodeState->groupId);
	values[2] = ObjectIdGetDatum(
		ReplicationStateGetEnum(pgAutoFailoverNode->goalState));
	values[3] = Int32GetDatum(assignedNodeState->candidatePriority);
	values[4] = BoolGetDatum(assignedNodeState->replicationQuorum);

	resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * node_active is the main entry-point for the HA state machine. Nodes
 * periodically call this function from the moment they start to communicate
 * their state to the monitor to obtain their assigned state.
 */
Datum
node_active(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(2);

	int32 currentNodeId = PG_GETARG_INT32(3);
	int32 currentGroupId = PG_GETARG_INT32(4);
	Oid currentReplicationStateOid = PG_GETARG_OID(5);

	bool currentPgIsRunning = PG_GETARG_BOOL(6);
	XLogRecPtr	currentLSN = PG_GETARG_LSN(7);
	text *currentPgsrSyncStateText = PG_GETARG_TEXT_P(8);
	char *currentPgsrSyncState = text_to_cstring(currentPgsrSyncStateText);

	AutoFailoverNodeState currentNodeState = { 0 };
	AutoFailoverNodeState *assignedNodeState = NULL;
	Oid newReplicationStateOid = InvalidOid;

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[5];
	bool isNulls[5];

	checkPgAutoFailoverVersion();

	currentNodeState.nodeId = currentNodeId;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedLSN = currentLSN;
	currentNodeState.pgsrSyncState = SyncStateFromString(currentPgsrSyncState);
	currentNodeState.pgIsRunning = currentPgIsRunning;
	assignedNodeState =
		NodeActive(formationId, nodeName, nodePort, &currentNodeState);

	newReplicationStateOid =
		ReplicationStateGetEnum(assignedNodeState->replicationState);

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(assignedNodeState->nodeId);
	values[1] = Int32GetDatum(assignedNodeState->groupId);
	values[2] = ObjectIdGetDatum(newReplicationStateOid);
	values[3] = Int32GetDatum(assignedNodeState->candidatePriority);
	values[4] = BoolGetDatum(assignedNodeState->replicationQuorum);

	resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * NodeActive reports the current state of a node and returns the assigned state.
 */
static AutoFailoverNodeState *
NodeActive(char *formationId, char *nodeName, int32 nodePort,
		   AutoFailoverNodeState *currentNodeState)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	AutoFailoverNodeState *assignedNodeState = NULL;

	pgAutoFailoverNode = GetAutoFailoverNode(nodeName, nodePort);
	if (pgAutoFailoverNode == NULL)
	{
		ereport(ERROR, (errmsg("node %s:%d is not registered",
							   nodeName, nodePort)));
	}
	else if (strcmp(pgAutoFailoverNode->formationId, formationId) != 0)
	{
		ereport(ERROR, (errmsg("node %s:%d does not belong to formation %s",
							   nodeName, nodePort, formationId)));
	}
	else if (currentNodeState->nodeId != pgAutoFailoverNode->nodeId &&
			 currentNodeState->nodeId != -1)
	{
		ereport(ERROR,
				(errmsg("node %s:%d with nodeid %d was removed",
						nodeName, nodePort, currentNodeState->nodeId),
				 errhint("Remove your state file to re-register the node.")));
	}
	else
	{
		LockFormation(formationId, ShareLock);

		if (pgAutoFailoverNode->reportedState
			!= currentNodeState->replicationState)
		{
			/*
			 * The keeper is reporting that it achieved the assigned goal
			 * state, supposedly. Log the new reported state as an event, and
			 * notify it.
			 */
			char message[BUFSIZE];

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Node %s:%d reported new state %s",
				pgAutoFailoverNode->nodeName, pgAutoFailoverNode->nodePort,
				ReplicationStateGetName(currentNodeState->replicationState));

			NotifyStateChange(currentNodeState->replicationState,
							  pgAutoFailoverNode->goalState,
							  formationId,
							  pgAutoFailoverNode->groupId,
							  pgAutoFailoverNode->nodeId,
							  pgAutoFailoverNode->nodeName,
							  pgAutoFailoverNode->nodePort,
							  currentNodeState->pgsrSyncState,
							  currentNodeState->reportedLSN,
							  pgAutoFailoverNode->candidatePriority,
							  pgAutoFailoverNode->replicationQuorum,
							  message);
		}

		/*
		 * Report the current state. The state might not have changed, but in
		 * that case we still update the last report time.
		 */
		ReportAutoFailoverNodeState(pgAutoFailoverNode->nodeName,
									pgAutoFailoverNode->nodePort,
									currentNodeState->replicationState,
									currentNodeState->pgIsRunning,
									currentNodeState->pgsrSyncState,
									currentNodeState->reportedLSN);
	}

	LockNodeGroup(formationId, currentNodeState->groupId, ExclusiveLock);

	pgAutoFailoverNode = GetAutoFailoverNode(nodeName, nodePort);

	ProceedGroupState(pgAutoFailoverNode);

	assignedNodeState =
		(AutoFailoverNodeState *) palloc0(sizeof(AutoFailoverNodeState));
	assignedNodeState->nodeId = pgAutoFailoverNode->nodeId;
	assignedNodeState->groupId = pgAutoFailoverNode->groupId;
	assignedNodeState->replicationState = pgAutoFailoverNode->goalState;
	assignedNodeState->candidatePriority = pgAutoFailoverNode->candidatePriority;
	assignedNodeState->replicationQuorum = pgAutoFailoverNode->replicationQuorum;

	return assignedNodeState;
}


/*
 * JoinAutoFailoverFormation adds a new node to a AutoFailover formation.
 */
static void
JoinAutoFailoverFormation(AutoFailoverFormation *formation,
						  char *nodeName, int nodePort,
						  AutoFailoverNodeState *currentNodeState)
{
	int groupId = -1;
	ReplicationState initialState = REPLICATION_STATE_UNKNOWN;

	/* in a Postgres formation, we have a single groupId, and it's groupId 0 */
	if (formation->kind == FORMATION_KIND_PGSQL)
	{
		/*
		 * Register with groupId -1 to get one assigned by the monitor, or with
		 * the groupId you know you want to join. In a Postgres (pgsql)
		 * formation it's all down to groupId 0 anyway.
		 */
		if (currentNodeState->groupId > 0)
		{
			ereport(ERROR,
					(errmsg("node %s:%d can not be registered in group %d "
							"in formation \"%s\" of type pgsql",
							nodeName, nodePort,
							currentNodeState->groupId, formation->formationId),
					 errdetail("in a pgsql formation, there can be only one "
							   "group, with groupId 0")));
		}
		groupId = currentNodeState->groupId = 0;
	}

	/* a group number was asked for in the registration call */
	if (currentNodeState->groupId >= 0)
	{
		List *groupNodeList = NIL;

		/* the node prefers a particular group */
		groupId = currentNodeState->groupId;

		groupNodeList = AutoFailoverNodeGroup(formation->formationId, groupId);

		/*
		 * Target group is empty: to make it simple to reason about the roles
		 * in a group, we only ever accept a primary node first. Then, any
		 * other node in the same group should be a standby. That's easy.
		 */
		if (list_length(groupNodeList) == 0)
		{
			initialState = REPLICATION_STATE_SINGLE;
		}
		/* target group already has a primary, any other node is a standby */
		else if (formation->opt_secondary && list_length(groupNodeList) >= 1)
		{
			AutoFailoverNode *primaryNode = NULL;

			initialState = REPLICATION_STATE_WAIT_STANDBY;

			/*
			 * We can only accept a single WAIT_STANDBY at a time, because of
			 * the way the FSM works. When the primary reports a goalState of
			 * WAIT_PRIMARY, we can advance the WAIT_STANDBY node to CATCHING
			 * UP. The FSM protocol and decision making is per state, and we
			 * wouldn't know which standby to advance if there were more than
			 * one in state WAIT_STANDBY at any given time.
			 *
			 * As a consequence, if the primary node is already in WAIT_PRIMARY
			 * or in JOIN_PRIMARY state, then we can't accept a new standby
			 * yet. Only one new standby at a time.
			 *
			 * We detect the situation here and report error code 55006 so that
			 * pg_autoctl knows to retry registering.
			 */

			primaryNode = GetWritableNodeInGroup(formation->formationId,
												 currentNodeState->groupId);

			if (IsInWaitOrJoinState(primaryNode))
			{
				AutoFailoverNode *standbyNode =
					FindFailoverNewStandbyNode(groupNodeList);

				Assert(standbyNode != NULL);

				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_IN_USE),
						 errmsg("primary node %s:%d is already in state %s",
								primaryNode->nodeName, primaryNode->nodePort,
								ReplicationStateGetName(primaryNode->goalState)),
						 errdetail("Only one standby can be registered at a "
								   "time in pg_auto_failover, and "
								   "node %d (%s:%d) is currently being "
								   "registered.",
								   standbyNode->nodeId,
								   standbyNode->nodeName,
								   standbyNode->nodePort),
						 errhint("Retry registering in a moment")));
			}
		}
	}
	else
	{
		/*
		 * In a Citus formation, the register policy is to build a set of
		 * workers with each a primary and a secondary, including the
		 * coordinator.
		 *
		 * That's the policy implemented in AssignGroupId.
		 */
		groupId = AssignGroupId(formation, nodeName, nodePort, &initialState);
	}

	AddAutoFailoverNode(formation->formationId, groupId, nodeName, nodePort,
						initialState, currentNodeState->replicationState,
						currentNodeState->candidatePriority,
						currentNodeState->replicationQuorum);

	currentNodeState->groupId = groupId;
}


/*
 * AssignGroupId assigns a group ID to a new node and returns it.
 */
static int
AssignGroupId(AutoFailoverFormation *formation, char *nodeName, int nodePort,
			  ReplicationState *initialState)
{
	int groupId = -1;
	int candidateGroupId =
		/*
		 * a Citus formation's coordinator always asks for groupId 0, and the
		 * workers are not allowed to ask for groupId 0. So here, when the
		 * formation is a citus formation, then candidateGroupId begins at 1.
		 */
		formation->kind == FORMATION_KIND_CITUS ? 1 : 0;

	do {
		List *groupNodeList =
			AutoFailoverNodeGroup(formation->formationId, candidateGroupId);

		if (list_length(groupNodeList) == 0)
		{
			groupId = candidateGroupId;
			*initialState = REPLICATION_STATE_SINGLE;
		}
		else if (formation->opt_secondary && list_length(groupNodeList) == 1)
		{
			groupId = candidateGroupId;
			*initialState = REPLICATION_STATE_WAIT_STANDBY;
		}
		else
		{
			candidateGroupId++;
		}
	} while (groupId == -1);

	return groupId;
}


/*
 * get_primary returns the node in a group which currently takes writes.
 */
Datum
get_primary(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	int32 groupId = PG_GETARG_INT32(1);

	AutoFailoverNode *primaryNode = NULL;

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[3];
	bool isNulls[3];

	checkPgAutoFailoverVersion();

	primaryNode = GetWritableNodeInGroup(formationId, groupId);
	if (primaryNode == NULL)
	{
		ereport(ERROR, (errmsg("group has no writable node right now")));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(primaryNode->nodeId);
	values[1] = CStringGetTextDatum(primaryNode->nodeName);
	values[2] = Int32GetDatum(primaryNode->nodePort);

	resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


typedef struct get_nodes_fctx
{
	List *nodesList;
} get_nodes_fctx;

/*
 * get_other_node returns the other node in a group, if any.
 */
Datum
get_nodes(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	get_nodes_fctx *fctx;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		text *formationIdText = PG_GETARG_TEXT_P(0);
		char *formationId = text_to_cstring(formationIdText);

		if (PG_ARGISNULL(0))
		{
			ereport(ERROR, (errmsg("formation_id must not be null")));
		}

		checkPgAutoFailoverVersion();

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (get_nodes_fctx *) palloc(sizeof(get_nodes_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		if (PG_ARGISNULL(1))
		{
			fctx->nodesList = AllAutoFailoverNodes(formationId);
		}
		else
		{
			int32 groupId = PG_GETARG_INT32(1);

			fctx->nodesList = AutoFailoverNodeGroup(formationId, groupId);
		}

		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;

	if (fctx->nodesList != NIL)
	{
		TupleDesc resultDescriptor = NULL;
		TypeFuncClass resultTypeClass = 0;
		Datum resultDatum = 0;
		HeapTuple resultTuple = NULL;
		Datum values[5];
		bool isNulls[5];

		AutoFailoverNode *node = (AutoFailoverNode *) linitial(fctx->nodesList);

		memset(values, 0, sizeof(values));
		memset(isNulls, false, sizeof(isNulls));

		values[0] = Int32GetDatum(node->nodeId);
		values[1] = CStringGetTextDatum(node->nodeName);
		values[2] = Int32GetDatum(node->nodePort);
		values[3] = LSNGetDatum(node->reportedLSN);
		values[4] = BoolGetDatum(CanTakeWritesInState(node->reportedState));

		resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
		if (resultTypeClass != TYPEFUNC_COMPOSITE)
		{
			ereport(ERROR, (errmsg("return type must be a row type")));
		}

		resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
		resultDatum = HeapTupleGetDatum(resultTuple);

		/* prepare next SRF call */
		fctx->nodesList = list_delete_first(fctx->nodesList);

		SRF_RETURN_NEXT(funcctx, PointerGetDatum(resultDatum));
	}

	SRF_RETURN_DONE(funcctx);
}


/*
 * get_other_node returns the other node in a group, if any.
 */
Datum
get_other_nodes(PG_FUNCTION_ARGS)
{
	FuncCallContext *funcctx;
	get_nodes_fctx *fctx;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		text *nodeNameText = PG_GETARG_TEXT_P(0);
		char *nodeName = text_to_cstring(nodeNameText);
		int32 nodePort = PG_GETARG_INT32(1);

		AutoFailoverNode *activeNode = NULL;

		checkPgAutoFailoverVersion();

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		 * switch to memory context appropriate for multiple function calls
		 */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* allocate memory for user context */
		fctx = (get_nodes_fctx *) palloc(sizeof(get_nodes_fctx));

		/*
		 * Use fctx to keep state from call to call. Seed current with the
		 * original start value
		 */
		activeNode = GetAutoFailoverNode(nodeName, nodePort);
		if (activeNode == NULL)
		{
			ereport(ERROR,
					(errmsg("node %s:%d is not registered", nodeName, nodePort)));
		}

		if (PG_NARGS() == 2)
		{
			fctx->nodesList = AutoFailoverOtherNodesList(activeNode);
		}
		else if (PG_NARGS() == 3)
		{
			Oid currentReplicationStateOid = PG_GETARG_OID(2);
			ReplicationState currentState =
				EnumGetReplicationState(currentReplicationStateOid);

			fctx->nodesList =
				AutoFailoverOtherNodesListInState(activeNode, currentState);
		}
		else
		{
			/* that's a bug in the SQL exposure of that function */
			ereport(ERROR,
					(errmsg("unsupported number of arguments (%d)",
							PG_NARGS())));
		}

		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();

	/*
	 * get the saved state and use current as the result for this iteration
	 */
	fctx = funcctx->user_fctx;

	if (fctx->nodesList != NIL)
	{
		TupleDesc resultDescriptor = NULL;
		TypeFuncClass resultTypeClass = 0;
		Datum resultDatum = 0;
		HeapTuple resultTuple = NULL;
		Datum values[5];
		bool isNulls[5];

		AutoFailoverNode *node = (AutoFailoverNode *) linitial(fctx->nodesList);

		memset(values, 0, sizeof(values));
		memset(isNulls, false, sizeof(isNulls));

		values[0] = Int32GetDatum(node->nodeId);
		values[1] = CStringGetTextDatum(node->nodeName);
		values[2] = Int32GetDatum(node->nodePort);
		values[3] = LSNGetDatum(node->reportedLSN);
		values[4] = BoolGetDatum(CanTakeWritesInState(node->reportedState));

		resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
		if (resultTypeClass != TYPEFUNC_COMPOSITE)
		{
			ereport(ERROR, (errmsg("return type must be a row type")));
		}

		resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
		resultDatum = HeapTupleGetDatum(resultTuple);

		/* prepare next SRF call */
		fctx->nodesList = list_delete_first(fctx->nodesList);

		SRF_RETURN_NEXT(funcctx, PointerGetDatum(resultDatum));
	}

	SRF_RETURN_DONE(funcctx);
}


/*
 * remove_node removes the given node from the monitor.
 */
Datum
remove_node(PG_FUNCTION_ARGS)
{
	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	AutoFailoverNode *otherNode = NULL;

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeName, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ExclusiveLock);

	otherNode = OtherNodeInGroup(currentNode);

	RemoveAutoFailoverNode(nodeName, nodePort);

	if (otherNode != NULL)
	{
		ProceedGroupState(otherNode);
	}

	PG_RETURN_BOOL(true);
}


/* returns true if state is equal to any of allowedStates */
static bool
IsStateIn(ReplicationState state, List *allowedStates)
{
	ListCell *cell = NULL;

	foreach(cell, allowedStates)
	{
		ReplicationState allowedState = (ReplicationState) lfirst_int(cell);
		if (state == allowedState)
		{
			return true;
		}
	}

	return false;
}


/*
 * perform_failover promotes the secondary in the given group
 */
Datum
perform_failover(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	int32 groupId = PG_GETARG_INT32(1);

	List *groupNodeList = NULL;

	AutoFailoverNode *firstNode = NULL;
	AutoFailoverNode *secondNode = NULL;

	AutoFailoverNode *primaryNode = NULL;
	AutoFailoverNode *secondaryNode = NULL;

	List *primaryStates = list_make2_int(REPLICATION_STATE_PRIMARY,
										 REPLICATION_STATE_WAIT_PRIMARY);
	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);

	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	LockFormation(formationId, ShareLock);
	LockNodeGroup(formationId, groupId, ExclusiveLock);

	groupNodeList = AutoFailoverNodeGroup(formationId, groupId);
	if (list_length(groupNodeList) != 2)
	{
		ereport(ERROR, (errmsg("cannot fail over: group does not have 2 nodes")));
	}

	firstNode = linitial(groupNodeList);
	secondNode = lsecond(groupNodeList);

	if (IsStateIn(firstNode->goalState, primaryStates) &&
		IsStateIn(firstNode->reportedState, primaryStates))
	{
		primaryNode = firstNode;
	}
	else if (IsStateIn(secondNode->reportedState, primaryStates) &&
			 IsStateIn(secondNode->goalState, primaryStates))
	{
		primaryNode = secondNode;
	}
	else
	{
		ereport(ERROR, (errmsg("cannot fail over: there is no primary node")));
	}

	if (IsStateIn(firstNode->reportedState, secondaryStates) &&
		IsStateIn(firstNode->goalState, secondaryStates))
	{
		secondaryNode = firstNode;
	}
	else if (IsStateIn(secondNode->reportedState, secondaryStates) &&
			 IsStateIn(secondNode->goalState, secondaryStates))
	{
		secondaryNode = secondNode;
	}
	else
	{
		ereport(ERROR, (errmsg("cannot fail over: there is no secondary node")));
	}

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of %s:%d to draining and %s:%d to "
		"prepare_promotion after a user-initiated failover.",
		primaryNode->nodeName, primaryNode->nodePort,
		secondaryNode->nodeName, secondaryNode->nodePort);

	SetNodeGoalState(primaryNode->nodeName, primaryNode->nodePort,
					 REPLICATION_STATE_DRAINING);

	NotifyStateChange(primaryNode->reportedState,
					  REPLICATION_STATE_DRAINING,
					  primaryNode->formationId,
					  primaryNode->groupId,
					  primaryNode->nodeId,
					  primaryNode->nodeName,
					  primaryNode->nodePort,
					  primaryNode->pgsrSyncState,
					  primaryNode->reportedLSN,
					  primaryNode->candidatePriority,
					  primaryNode->replicationQuorum,
					  message);

	SetNodeGoalState(secondaryNode->nodeName, secondaryNode->nodePort,
					 REPLICATION_STATE_PREPARE_PROMOTION);

	NotifyStateChange(secondaryNode->reportedState,
					  REPLICATION_STATE_PREPARE_PROMOTION,
					  secondaryNode->formationId,
					  secondaryNode->groupId,
					  secondaryNode->nodeId,
					  secondaryNode->nodeName,
					  secondaryNode->nodePort,
					  secondaryNode->pgsrSyncState,
					  secondaryNode->reportedLSN,
					  secondaryNode->candidatePriority,
					  secondaryNode->replicationQuorum,
					  message);

	PG_RETURN_VOID();
}


/*
 * start_maintenance sets the given node in maintenance state.
 *
 * This operation is only allowed on a secondary node. To do so on a primary
 * node, first failover so that it's now a secondary.
 */
Datum
start_maintenance(PG_FUNCTION_ARGS)
{
	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	AutoFailoverNode *otherNode = NULL;

	char message[BUFSIZE];

	List *primaryStates = list_make2_int(REPLICATION_STATE_PRIMARY,
										 REPLICATION_STATE_WAIT_PRIMARY);
	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeName, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	otherNode = OtherNodeInGroup(currentNode);

	if (otherNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: group does not have 2 nodes")));
	}

	if (currentNode->reportedState == REPLICATION_STATE_MAINTENANCE
		|| currentNode->goalState == REPLICATION_STATE_MAINTENANCE)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: "
						"node %s:%d is already in maintenance",
						currentNode->nodeName, currentNode->nodePort)));
	}

	if (! (IsStateIn(currentNode->reportedState, secondaryStates)
		   && currentNode->reportedState == currentNode->goalState))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: current state for node %s:%d "
						"is \"%s\", expected either "
						"\"secondary\" or \"catchingup\"",
						currentNode->nodeName, currentNode->nodePort,
						ReplicationStateGetName(currentNode->goalState))));
	}

	if (!(IsStateIn(otherNode->goalState, primaryStates)
		  && currentNode->reportedState == currentNode->goalState))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: current state for node %s:%d "
						"is \"%s\", expected either "
						"\"primary\" or \"wait_primary\"",
						otherNode->nodeName, otherNode->nodePort,
						ReplicationStateGetName(currentNode->goalState))));
	}

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of %s:%d to wait_primary and %s:%d to"
		"maintenance after a user-initiated start_maintenance call.",
		otherNode->nodeName, otherNode->nodePort,
		currentNode->nodeName, currentNode->nodePort);

	SetNodeGoalState(otherNode->nodeName, otherNode->nodePort,
					 REPLICATION_STATE_WAIT_PRIMARY);

	NotifyStateChange(otherNode->reportedState,
					  REPLICATION_STATE_WAIT_PRIMARY,
					  otherNode->formationId,
					  otherNode->groupId,
					  otherNode->nodeId,
					  otherNode->nodeName,
					  otherNode->nodePort,
					  otherNode->pgsrSyncState,
					  otherNode->reportedLSN,
					  otherNode->candidatePriority,
					  otherNode->replicationQuorum,
					  message);

	SetNodeGoalState(currentNode->nodeName, currentNode->nodePort,
					 REPLICATION_STATE_MAINTENANCE);

	NotifyStateChange(currentNode->reportedState,
					  REPLICATION_STATE_MAINTENANCE,
					  currentNode->formationId,
					  currentNode->groupId,
					  currentNode->nodeId,
					  currentNode->nodeName,
					  currentNode->nodePort,
					  currentNode->pgsrSyncState,
					  currentNode->reportedLSN,
					  currentNode->candidatePriority,
					  currentNode->replicationQuorum,
					  message);

	PG_RETURN_BOOL(true);
}


/*
 * stop_maintenance sets the given node back in catchingup state.
 *
 * This operation is only allowed on a secondary node. To do so on a primary
 * node, first failover so that it's now a secondary.
 */
Datum
stop_maintenance(PG_FUNCTION_ARGS)
{
	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	AutoFailoverNode *otherNode = NULL;

	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeName, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	otherNode = OtherNodeInGroup(currentNode);

	if (otherNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot stop maintenance: group does not have 2 nodes")));
	}

	if (!IsCurrentState(currentNode, REPLICATION_STATE_MAINTENANCE))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot stop maintenance when current state for "
						"node %s:%d is not \"maintenance\"",
						currentNode->nodeName, currentNode->nodePort)));
	}

	if (!IsCurrentState(otherNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot stop maintenance when current state for "
						"node %s:%d is \"%s\"",
						otherNode->nodeName, otherNode->nodePort,
						ReplicationStateGetName(otherNode->goalState))));
	}

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of %s:%d to catchingup  "
		"after a user-initiated stop_maintenance call.",
		currentNode->nodeName, currentNode->nodePort);

	SetNodeGoalState(currentNode->nodeName, currentNode->nodePort,
					 REPLICATION_STATE_CATCHINGUP);

	NotifyStateChange(currentNode->reportedState,
					  REPLICATION_STATE_CATCHINGUP,
					  currentNode->formationId,
					  currentNode->groupId,
					  currentNode->nodeId,
					  currentNode->nodeName,
					  currentNode->nodePort,
					  currentNode->pgsrSyncState,
					  currentNode->reportedLSN,
					  currentNode->candidatePriority,
					  currentNode->replicationQuorum,
					  message);

	PG_RETURN_BOOL(true);
}


/*
 * set_node_candidate_priority sets node candidate priority property
 */
Datum
set_node_candidate_priority(PG_FUNCTION_ARGS)
{
	int32 nodeId = PG_GETARG_INT32(0);
	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(2);
	int candidatePriority = PG_GETARG_INT32(3);
	AutoFailoverNode *currentNode = NULL;
	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNodeWithId(nodeId, nodeName, nodePort);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errmsg("node %d is not registered", nodeId)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	if (candidatePriority < 0 || candidatePriority > 100)
	{
		ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
						errmsg("invalid value for candidate_priority \"%d\" "
							   "expected an integer value between 0 and 100",
							   candidatePriority)));
	}


	currentNode->candidatePriority = candidatePriority;


	ReportAutoFailoverNodeReplicationSetting(
			currentNode->nodeId,
			currentNode->nodeName,
			currentNode->nodePort,
			currentNode->candidatePriority,
			currentNode->replicationQuorum);

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Updating candidatePriority.");

	NotifyStateChange(currentNode->reportedState,
					  currentNode->goalState,
					  currentNode->formationId,
					  currentNode->groupId,
					  currentNode->nodeId,
					  currentNode->nodeName,
					  currentNode->nodePort,
					  currentNode->pgsrSyncState,
					  currentNode->reportedLSN,
					  currentNode->candidatePriority,
					  currentNode->replicationQuorum,
					  message);

	PG_RETURN_BOOL(true);
}


/*
 * set_node_replication_quorum sets node replication quorum property
 */
Datum
set_node_replication_quorum(PG_FUNCTION_ARGS)
{
	int32 nodeid = PG_GETARG_INT32(0);
	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(2);
	bool replicationQuorum = PG_GETARG_BOOL(3);
	AutoFailoverNode *currentNode = NULL;
	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNodeWithId(nodeid, nodeName, nodePort);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errmsg("node %d is not registered", nodeid)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	currentNode->replicationQuorum = replicationQuorum;


	ReportAutoFailoverNodeReplicationSetting(currentNode->nodeId,
			currentNode->nodeName,
			currentNode->nodePort,
			currentNode->candidatePriority,
			currentNode->replicationQuorum);

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Updating replicationQuorum.");

	NotifyStateChange(currentNode->reportedState,
					  currentNode->goalState,
					  currentNode->formationId,
					  currentNode->groupId,
					  currentNode->nodeId,
					  currentNode->nodeName,
					  currentNode->nodePort,
					  currentNode->pgsrSyncState,
					  currentNode->reportedLSN,
					  currentNode->candidatePriority,
					  currentNode->replicationQuorum,
					  message);

	PG_RETURN_BOOL(true);
}
