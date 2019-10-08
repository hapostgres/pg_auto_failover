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

static AutoFailoverNode * GetWritableNode(char *formationId, int32 groupId);
static bool CanTakeWritesInState(ReplicationState state);
static bool IsStateIn(ReplicationState state, List *allowedStates);


/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_node);
PG_FUNCTION_INFO_V1(node_active);
PG_FUNCTION_INFO_V1(get_primary);
PG_FUNCTION_INFO_V1(get_other_node);
PG_FUNCTION_INFO_V1(remove_node);
PG_FUNCTION_INFO_V1(perform_failover);
PG_FUNCTION_INFO_V1(start_maintenance);
PG_FUNCTION_INFO_V1(stop_maintenance);

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

	AutoFailoverFormation *formation = NULL;
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	AutoFailoverNodeState currentNodeState = { 0 };
	AutoFailoverNodeState *assignedNodeState = NULL;

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[3];
	bool isNulls[3];

	checkPgAutoFailoverVersion();

	currentNodeState.nodeId = -1;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedLSN = 0;

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
	Datum values[3];
	bool isNulls[3];

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

	if (currentNodeState->groupId >= 0)
	{
		List *groupNodeList = NIL;

		/* the node prefers a particular group */
		groupId = currentNodeState->groupId;

		groupNodeList = AutoFailoverNodeGroup(formation->formationId, groupId);
		if (list_length(groupNodeList) == 0)
		{
			initialState = REPLICATION_STATE_SINGLE;
		}
		else if (formation->opt_secondary && list_length(groupNodeList) == 1)
		{
			initialState = REPLICATION_STATE_WAIT_STANDBY;
		}
		else
		{
			ereport(ERROR, (errmsg("group %d already has %d members", groupId,
								   list_length(groupNodeList))));
		}
	}
	else
	{
		groupId = AssignGroupId(formation, nodeName, nodePort, &initialState);
	}

	AddAutoFailoverNode(formation->formationId, groupId, nodeName, nodePort,
						initialState, currentNodeState->replicationState);

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
	Datum values[2];
	bool isNulls[2];

	checkPgAutoFailoverVersion();

	primaryNode = GetWritableNode(formationId, groupId);
	if (primaryNode == NULL)
	{
		ereport(ERROR, (errmsg("group has no writable node right now")));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(primaryNode->nodeName);
	values[1] = Int32GetDatum(primaryNode->nodePort);

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
 * GetWritableNode returns the writable node in the specified group, if any.
 */
static AutoFailoverNode *
GetWritableNode(char *formationId, int32 groupId)
{
	AutoFailoverNode *writableNode = NULL;
	List *groupNodeList = NIL;
	ListCell *nodeCell = NULL;

	groupNodeList = AutoFailoverNodeGroup(formationId, groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (CanTakeWritesInState(currentNode->reportedState))
		{
			writableNode = currentNode;
			break;
		}
	}

	return writableNode;
}


/*
 * CanTakeWritesInState returns whether a node can take writes when in
 * the given state.
 */
static bool
CanTakeWritesInState(ReplicationState state)
{
	return state == REPLICATION_STATE_SINGLE ||
		   state == REPLICATION_STATE_PRIMARY ||
		   state == REPLICATION_STATE_WAIT_PRIMARY;
}


/*
 * get_other_node returns the other node in a group, if any.
 */
Datum
get_other_node(PG_FUNCTION_ARGS)
{
	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *activeNode = NULL;
	AutoFailoverNode *otherNode = NULL;

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[2];
	bool isNulls[2];

	checkPgAutoFailoverVersion();

	activeNode = GetAutoFailoverNode(nodeName, nodePort);
	if (activeNode == NULL)
	{
		ereport(ERROR,
				(errmsg("node %s:%d is not registered", nodeName, nodePort)));
	}

	otherNode = OtherNodeInGroup(activeNode);
	if (otherNode == NULL)
	{
		ereport(ERROR,
				(errmsg("node %s:%d is alone in the group", nodeName, nodePort)));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(otherNode->nodeName);
	values[1] = Int32GetDatum(otherNode->nodePort);

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
		"Setting goal state of %s:%d to draining and %s:%d to"
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
					  message);

	PG_RETURN_BOOL(true);
}
