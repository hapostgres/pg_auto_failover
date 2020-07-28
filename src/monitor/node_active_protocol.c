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
#include "access/xact.h"

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
										  char *nodeHost, int32 nodePort,
										  AutoFailoverNodeState *currentNodeState);
static void JoinAutoFailoverFormation(AutoFailoverFormation *formation,
									  char *nodeHost, int nodePort,
									  uint64 sysIdentifier,
									  AutoFailoverNodeState *currentNodeState);
static int AssignGroupId(AutoFailoverFormation *formation,
						 char *nodeHost, int nodePort,
						 ReplicationState *initialState);


/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_node);
PG_FUNCTION_INFO_V1(node_active);
PG_FUNCTION_INFO_V1(get_nodes);
PG_FUNCTION_INFO_V1(get_primary);
PG_FUNCTION_INFO_V1(get_other_node);
PG_FUNCTION_INFO_V1(get_other_nodes);
PG_FUNCTION_INFO_V1(remove_node);
PG_FUNCTION_INFO_V1(perform_failover);
PG_FUNCTION_INFO_V1(start_maintenance);
PG_FUNCTION_INFO_V1(stop_maintenance);
PG_FUNCTION_INFO_V1(set_node_candidate_priority);
PG_FUNCTION_INFO_V1(set_node_replication_quorum);
PG_FUNCTION_INFO_V1(synchronous_standby_names);


/*
 * register_node adds a node to a given formation
 *
 * At register time the monitor connects to the node to check that nodehost and
 * nodeport are valid, and it does a SELECT pg_is_in_recovery() to help decide
 * what initial role to attribute the entering node.
 */
Datum
register_node(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(2);
	Name dbnameName = PG_GETARG_NAME(3);
	uint64 sysIdentifier = PG_GETARG_INT64(4);
	const char *expectedDBName = NameStr(*dbnameName);

	int32 currentGroupId = PG_GETARG_INT32(5);
	Oid currentReplicationStateOid = PG_GETARG_OID(6);

	text *nodeKindText = PG_GETARG_TEXT_P(7);
	char *nodeKind = text_to_cstring(nodeKindText);
	FormationKind expectedFormationKind =
		FormationKindFromNodeKindString(nodeKind);
	int candidatePriority = PG_GETARG_INT32(8);
	bool replicationQuorum = PG_GETARG_BOOL(9);

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
							nodeHost, nodePort, nodeKind,
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
					(errmsg("node %s:%d with dbname \"%s\" can not be "
							"registered in formation \"%s\" "
							"which expects dbname \"%s\"",
							nodeHost, nodePort, expectedDBName,
							formationId,
							formation->dbname)));
		}
	}

	JoinAutoFailoverFormation(formation,
							  nodeHost,
							  nodePort,
							  sysIdentifier,
							  &currentNodeState);
	LockNodeGroup(formationId, currentNodeState.groupId, ExclusiveLock);

	pgAutoFailoverNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (pgAutoFailoverNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("node %s:%d with dbname \"%s\" could not be registered in "
						"formation \"%s\", could not get information for node that was inserted",
						nodeHost, nodePort, expectedDBName,
						formationId)));
	}

	if (pgAutoFailoverNode == NULL)
	{
		/* that's a bug, really, maybe we could use an Assert() instead */
		ereport(ERROR,
				(errmsg("couldn't find the newly registered node %s:%d "
						"in formation \"%s\", group %d",
						nodeHost, nodePort,
						formationId, currentNodeState.groupId)));
	}

	/*
	 * When adding a second node to a formation that has number_sync_standbys
	 * set to zero (the default value for single node and single standby
	 * formations), we switch the default value to 1 automatically.
	 */
	if (formation->number_sync_standbys == 0)
	{
		List *allNodes = AllAutoFailoverNodes(formationId);

		/* second standby to be registered */
		if (list_length(allNodes) == 3)
		{
			char message[BUFSIZE] = { 0 };

			formation->number_sync_standbys = 1;

			if (!SetFormationNumberSyncStandbys(formationId, 1))
			{
				ereport(ERROR,
						(errmsg("couldn't set the formation \"%s\" "
								"number_sync_standbys to 1 now that a third "
								"node has been added",
								formationId)));
			}

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting number_sync_standbys to %d for formation %s.",
				formation->number_sync_standbys,
				formation->formationId);
		}
	}

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
							nodeHost, nodePort, currentState, goalState)));
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
	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(2);

	int32 currentNodeId = PG_GETARG_INT32(3);
	int32 currentGroupId = PG_GETARG_INT32(4);
	Oid currentReplicationStateOid = PG_GETARG_OID(5);

	bool currentPgIsRunning = PG_GETARG_BOOL(6);
	XLogRecPtr currentLSN = PG_GETARG_LSN(7);
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
		NodeActive(formationId, nodeHost, nodePort, &currentNodeState);

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
NodeActive(char *formationId, char *nodeHost, int32 nodePort,
		   AutoFailoverNodeState *currentNodeState)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	AutoFailoverNodeState *assignedNodeState = NULL;

	pgAutoFailoverNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (pgAutoFailoverNode == NULL)
	{
		ereport(ERROR, (errmsg("node %s:%d is not registered",
							   nodeHost, nodePort)));
	}
	else if (strcmp(pgAutoFailoverNode->formationId, formationId) != 0)
	{
		ereport(ERROR, (errmsg("node %s:%d does not belong to formation %s",
							   nodeHost, nodePort, formationId)));
	}
	else if (currentNodeState->nodeId != pgAutoFailoverNode->nodeId &&
			 currentNodeState->nodeId != -1)
	{
		ereport(ERROR,
				(errmsg("node %s:%d with nodeid %d was removed",
						nodeHost, nodePort, currentNodeState->nodeId),
				 errhint("Remove your state file to re-register the node.")));
	}
	else
	{
		LockFormation(formationId, ShareLock);

		if (pgAutoFailoverNode->reportedState != currentNodeState->replicationState)
		{
			/*
			 * The keeper is reporting that it achieved the assigned goal
			 * state, supposedly. Log the new reported state as an event, and
			 * notify it.
			 */
			char message[BUFSIZE];

			if (pgAutoFailoverNode->goalState == REPLICATION_STATE_REPORT_LSN)
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"Node %d (%s:%d) reported new state \"%s\" with LSN %X/%X",
					pgAutoFailoverNode->nodeId,
					pgAutoFailoverNode->nodeHost,
					pgAutoFailoverNode->nodePort,
					ReplicationStateGetName(currentNodeState->replicationState),
					(uint32) (pgAutoFailoverNode->reportedLSN >> 32),
					(uint32) pgAutoFailoverNode->reportedLSN);
			}
			else
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"Node %d (%s:%d) reported new state \"%s\"",
					pgAutoFailoverNode->nodeId,
					pgAutoFailoverNode->nodeHost,
					pgAutoFailoverNode->nodePort,
					ReplicationStateGetName(currentNodeState->replicationState));
			}

			NotifyStateChange(currentNodeState->replicationState,
							  pgAutoFailoverNode->goalState,
							  formationId,
							  pgAutoFailoverNode->groupId,
							  pgAutoFailoverNode->nodeId,
							  pgAutoFailoverNode->nodeHost,
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
		ReportAutoFailoverNodeState(pgAutoFailoverNode->nodeHost,
									pgAutoFailoverNode->nodePort,
									currentNodeState->replicationState,
									currentNodeState->pgIsRunning,
									currentNodeState->pgsrSyncState,
									currentNodeState->reportedLSN);
	}

	LockNodeGroup(formationId, currentNodeState->groupId, ExclusiveLock);

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
						  char *nodeHost, int nodePort, uint64 sysIdentifier,
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
							nodeHost, nodePort,
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
		else if (formation->opt_secondary)
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
			primaryNode =
				GetPrimaryNodeInGroup(
					formation->formationId,
					currentNodeState->groupId);

			if (primaryNode == NULL)
			{
				/*
				 * We have list_length(groupNodeList) >= 1 and yet we don't
				 * have any node that is in a writable state: this means the
				 * primary node was assigned SINGLE but did not report yet.
				 */
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_IN_USE),
						 errmsg("JoinAutoFailoverFormation couldn't find the "
								" primary node in formation \"%s\", group %d",
								formation->formationId,
								currentNodeState->groupId),
						 errhint("Retry registering in a moment")));
			}

			/*
			 * We can only accept a single new standby at a time. Spend some
			 * cycles on grabbing more information for a user-friendly report.
			 */
			if (IsInWaitOrJoinState(primaryNode))
			{
				const char *primaryState =
					ReplicationStateGetName(primaryNode->reportedState);

				AutoFailoverNode *standbyNode =
					FindFailoverNewStandbyNode(groupNodeList);

				/* race condition: the standby might be SECONDARY already */
				if (standbyNode != NULL)
				{
					ereport(ERROR,
							(errcode(ERRCODE_OBJECT_IN_USE),
							 errmsg("primary node %s:%d is already in state %s",
									primaryNode->nodeHost,
									primaryNode->nodePort,
									primaryState),
							 errdetail("Only one standby can be registered "
									   "at a time in pg_auto_failover, and "
									   "node %d (%s:%d) is currently being "
									   "registered.",
									   standbyNode->nodeId,
									   standbyNode->nodeHost,
									   standbyNode->nodePort),
							 errhint("Retry registering in a moment")));
				}
				else
				{
					/* sadly we don't have details in that case */
					ereport(ERROR,
							(errcode(ERRCODE_OBJECT_IN_USE),
							 errmsg("primary node %s:%d is already in state %s",
									primaryNode->nodeHost,
									primaryNode->nodePort,
									primaryState),
							 errhint("Retry registering in a moment")));
				}
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
		groupId = AssignGroupId(formation, nodeHost, nodePort, &initialState);
	}

	AddAutoFailoverNode(formation->formationId,
						groupId,
						nodeHost,
						nodePort,
						sysIdentifier,
						initialState,
						currentNodeState->replicationState,
						currentNodeState->candidatePriority,
						currentNodeState->replicationQuorum);

	currentNodeState->groupId = groupId;
}


/*
 * AssignGroupId assigns a group ID to a new node and returns it.
 */
static int
AssignGroupId(AutoFailoverFormation *formation, char *nodeHost, int nodePort,
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

	primaryNode = GetPrimaryOrDemotedNodeInGroup(formationId, groupId);
	if (primaryNode == NULL)
	{
		ereport(ERROR, (errmsg("group has no writable node right now")));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(primaryNode->nodeId);
	values[1] = CStringGetTextDatum(primaryNode->nodeHost);
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
		values[1] = CStringGetTextDatum(node->nodeHost);
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
 * get_other_node is not supported anymore, but we might want to be able to
 * have the pgautofailover.so for 1.1 co-exists with the SQL definitions for
 * 1.0 at least during an upgrade, or to test upgrades.
 */
Datum
get_other_node(PG_FUNCTION_ARGS)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("pgautofailover.get_other_node is no longer supported")));
}


/*
 * get_other_nodes returns the other node in a group, if any.
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
		text *nodeHostText = PG_GETARG_TEXT_P(0);
		char *nodeHost = text_to_cstring(nodeHostText);
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
		activeNode = GetAutoFailoverNode(nodeHost, nodePort);
		if (activeNode == NULL)
		{
			ereport(ERROR,
					(errmsg("node %s:%d is not registered", nodeHost, nodePort)));
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
		values[1] = CStringGetTextDatum(node->nodeHost);
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
	text *nodeHostText = PG_GETARG_TEXT_P(0);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	bool currentNodeIsPrimary = false;

	List *otherNodesGroupList = NIL;
	ListCell *nodeCell = NULL;
	AutoFailoverNode *firstStandbyNode = NULL;

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ExclusiveLock);

	/* when removing the primary, initiate a failover */
	currentNodeIsPrimary = StateBelongsToPrimary(currentNode->reportedState);

	/* get the list of the other nodes */
	otherNodesGroupList = AutoFailoverOtherNodesList(currentNode);

	/* and the first other node to trigger our first FSM transition */
	firstStandbyNode =
		otherNodesGroupList == NIL ? NULL : linitial(otherNodesGroupList);

	/* review the FSM for every other node, when removing the primary */
	if (currentNodeIsPrimary)
	{
		foreach(nodeCell, otherNodesGroupList)
		{
			char message[BUFSIZE] = { 0 };
			AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

			if (node == NULL)
			{
				/* shouldn't happen */
				ereport(ERROR, (errmsg("BUG: node is NULL")));
				continue;
			}

			/* skip nodes that are currently in maintenance */
			if (IsInMaintenance(node))
			{
				continue;
			}

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of node %d (%s:%d) to report_lsn "
				"after primary node removal.",
				node->nodeId, node->nodeHost, node->nodePort);

			SetNodeGoalState(node->nodeHost, node->nodePort,
							 REPLICATION_STATE_REPORT_LSN);

			NotifyStateChange(node->reportedState,
							  REPLICATION_STATE_REPORT_LSN,
							  node->formationId,
							  node->groupId,
							  node->nodeId,
							  node->nodeHost,
							  node->nodePort,
							  node->pgsrSyncState,
							  node->reportedLSN,
							  node->candidatePriority,
							  node->replicationQuorum,
							  message);
		}
	}

	/* time to actually remove the primary */
	RemoveAutoFailoverNode(nodeHost, nodePort);

	/* now proceed with the failover, starting with the first standby */
	if (currentNodeIsPrimary)
	{
		/* if we have at least one other node in the group, proceed */
		if (firstStandbyNode)
		{
			(void) ProceedGroupState(firstStandbyNode);
		}
	}
	else
	{
		/* find the primary, if any, and have it realize a node has left */
		AutoFailoverNode *primaryNode =
			GetNodeToFailoverFromInGroup(currentNode->formationId,
										 currentNode->groupId);

		if (primaryNode)
		{
			(void) ProceedGroupState(primaryNode);
		}
	}

	PG_RETURN_BOOL(true);
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

	AutoFailoverNode *primaryNode = NULL;

	char message[BUFSIZE] = { 0 };

	checkPgAutoFailoverVersion();

	LockFormation(formationId, ShareLock);
	LockNodeGroup(formationId, groupId, ExclusiveLock);

	groupNodeList = AutoFailoverNodeGroup(formationId, groupId);
	if (list_length(groupNodeList) < 2)
	{
		ereport(ERROR,
				(errmsg("cannot fail over: group %d in formation %s "
						"currently has %d node registered",
						groupId, formationId, list_length(groupNodeList)),
				 errdetail("At least 2 nodes are required "
						   "to implement a failover")));
	}

	/* get a current primary node that we can failover from (accepts writes) */
	primaryNode = GetNodeToFailoverFromInGroup(formationId, groupId);

	if (primaryNode == NULL)
	{
		ereport(ERROR,
				(errmsg("couldn't find the primary node in formation \"%s\", "
						"group %d", formationId, groupId)));
	}

	/*
	 * When we have only two nodes, we can failover directly to the secondary
	 * node, provided its current state allows for that.
	 *
	 * When we have more than two nodes, then we need to check that we have at
	 * least one candidate for failover and initiate the REPORT_LSN dance to
	 * make the failover happen.
	 */
	if (list_length(groupNodeList) == 2)
	{
		AutoFailoverNode *secondaryNode = NULL;
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);

		if (list_length(standbyNodesGroupList) != 1)
		{
			ereport(ERROR,
					(errmsg("couldn't find the standby node in "
							"formation \"%s\", group %d with primary node "
							"%d (%s:%d)",
							formationId, groupId,
							primaryNode->nodeId,
							primaryNode->nodeHost,
							primaryNode->nodePort)));
		}

		secondaryNode = linitial(standbyNodesGroupList);

		if (secondaryNode->goalState != REPLICATION_STATE_SECONDARY)
		{
			const char *secondaryState =
				ReplicationStateGetName(secondaryNode->goalState);

			ereport(ERROR,
					(errmsg("standby node %d (%s:%d) is in state \"%s\", which "
							"prevents the node for being a failover candidate",
							secondaryNode->nodeId,
							secondaryNode->nodeHost,
							secondaryNode->nodePort,
							secondaryState)));
		}

		/*
		 * In order to safely proceed we need to ensure that the primary node
		 * has reached the primary state fully already. In the transition to
		 * PRIMARY we actually wait until the current LSN observed on the
		 * primary has made it to the secondary, which is a needed guarantee
		 * for avoiding data loss.
		 */
		if (!IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) ||
			!IsCurrentState(secondaryNode, REPLICATION_STATE_SECONDARY))
		{
			ereport(ERROR,
					(errmsg(
						 "cannot fail over: primary node is not in a stable state"),
					 errdetail("node %d (%s:%d) has reported state \"%s\" and "
							   "is assigned state \"%s\", "
							   "and node %d (%s:%d) has reported state \"%s\" "
							   "and is assigned state \"%s\"",
							   primaryNode->nodeId,
							   primaryNode->nodeHost,
							   primaryNode->nodePort,
							   ReplicationStateGetName(primaryNode->reportedState),
							   ReplicationStateGetName(primaryNode->goalState),
							   secondaryNode->nodeId,
							   secondaryNode->nodeHost,
							   secondaryNode->nodePort,
							   ReplicationStateGetName(secondaryNode->reportedState),
							   ReplicationStateGetName(secondaryNode->goalState)),
					 errhint("a stable state must be observed to "
							 "perform a manual failover")));
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to draining "
			"and node %d (%s:%d) to prepare_promotion "
			"after a user-initiated failover.",
			primaryNode->nodeId,
			primaryNode->nodeHost,
			primaryNode->nodePort,
			secondaryNode->nodeId,
			secondaryNode->nodeHost,
			secondaryNode->nodePort);

		SetNodeGoalState(primaryNode->nodeHost, primaryNode->nodePort,
						 REPLICATION_STATE_DRAINING);

		NotifyStateChange(primaryNode->reportedState,
						  REPLICATION_STATE_DRAINING,
						  primaryNode->formationId,
						  primaryNode->groupId,
						  primaryNode->nodeId,
						  primaryNode->nodeHost,
						  primaryNode->nodePort,
						  primaryNode->pgsrSyncState,
						  primaryNode->reportedLSN,
						  primaryNode->candidatePriority,
						  primaryNode->replicationQuorum,
						  message);

		SetNodeGoalState(secondaryNode->nodeHost, secondaryNode->nodePort,
						 REPLICATION_STATE_PREPARE_PROMOTION);

		NotifyStateChange(secondaryNode->reportedState,
						  REPLICATION_STATE_PREPARE_PROMOTION,
						  secondaryNode->formationId,
						  secondaryNode->groupId,
						  secondaryNode->nodeId,
						  secondaryNode->nodeHost,
						  secondaryNode->nodePort,
						  secondaryNode->pgsrSyncState,
						  secondaryNode->reportedLSN,
						  secondaryNode->candidatePriority,
						  secondaryNode->replicationQuorum,
						  message);
	}
	else
	{
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
		AutoFailoverNode *firstStandbyNode = linitial(standbyNodesGroupList);
		char message[BUFSIZE];

		/* so we have at least one candidate, let's get started */
		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to draining "
			"after a user-initiated failover.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort);

		SetNodeGoalState(primaryNode->nodeHost, primaryNode->nodePort,
						 REPLICATION_STATE_DRAINING);

		NotifyStateChange(primaryNode->reportedState,
						  REPLICATION_STATE_DRAINING,
						  primaryNode->formationId,
						  primaryNode->groupId,
						  primaryNode->nodeId,
						  primaryNode->nodeHost,
						  primaryNode->nodePort,
						  primaryNode->pgsrSyncState,
						  primaryNode->reportedLSN,
						  primaryNode->candidatePriority,
						  primaryNode->replicationQuorum,
						  message);

		/* now proceed with the failover, starting with the first standby */
		(void) ProceedGroupState(firstStandbyNode);
	}

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
	text *nodeHostText = PG_GETARG_TEXT_P(0);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	AutoFailoverNode *primaryNode = NULL;

	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);

	List *groupNodesList = NIL;
	int nodesCount = 0;

	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	groupNodesList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);
	nodesCount = list_length(groupNodesList);

	/* check pre-conditions for the current node (secondary) */
	if (currentNode->reportedState == REPLICATION_STATE_MAINTENANCE ||
		currentNode->goalState == REPLICATION_STATE_MAINTENANCE)
	{
		/* if we're already in maintenance, we're good */
		PG_RETURN_BOOL(true);
	}

	/*
	 * We allow to go to maintenance in the following cases only:
	 *
	 *  - current node is a primary, and we then promote the secondary
	 *  - current node is a secondary or is catching up
	 */
	if (!(IsCurrentState(currentNode, REPLICATION_STATE_PRIMARY) ||
		  (IsStateIn(currentNode->reportedState, secondaryStates))))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: node %s:%d has reported state "
						"\"%s\" and is assigned state \"%s\", "
						"expected either \"primary\", "
						"\"secondary\" or \"catchingup\"",
						currentNode->nodeHost, currentNode->nodePort,
						ReplicationStateGetName(currentNode->reportedState),
						ReplicationStateGetName(currentNode->goalState))));
	}

	if (IsCurrentState(currentNode, REPLICATION_STATE_PRIMARY))
	{
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(currentNode);
		AutoFailoverNode *firstStandbyNode = linitial(standbyNodesGroupList);
		char message[BUFSIZE];

		/*
		 * Set the primary to prepare_maintenance now, and if we have a single
		 * secondary we assign it prepare_promotion, otherwise we need to elect
		 * a secondary, same as in perform_failover.
		 */
		SetNodeGoalState(currentNode->nodeHost, currentNode->nodePort,
						 REPLICATION_STATE_PREPARE_MAINTENANCE);

		NotifyStateChange(currentNode->reportedState,
						  REPLICATION_STATE_PREPARE_MAINTENANCE,
						  currentNode->formationId,
						  currentNode->groupId,
						  currentNode->nodeId,
						  currentNode->nodeHost,
						  currentNode->nodePort,
						  currentNode->pgsrSyncState,
						  currentNode->reportedLSN,
						  currentNode->candidatePriority,
						  currentNode->replicationQuorum,
						  message);

		if (nodesCount == 2)
		{
			AutoFailoverNode *otherNode = firstStandbyNode;

			/*
			 * We put the only secondary node straight to prepare_replication.
			 */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of %s:%d to prepare_maintenance "
				"and %s:%d to prepare_promotion "
				"after a user-initiated start_maintenance call.",
				currentNode->nodeHost, currentNode->nodePort,
				otherNode->nodeHost, otherNode->nodePort);

			SetNodeGoalState(otherNode->nodeHost, otherNode->nodePort,
							 REPLICATION_STATE_PREPARE_PROMOTION);

			NotifyStateChange(otherNode->reportedState,
							  REPLICATION_STATE_PREPARE_PROMOTION,
							  otherNode->formationId,
							  otherNode->groupId,
							  otherNode->nodeId,
							  otherNode->nodeHost,
							  otherNode->nodePort,
							  otherNode->pgsrSyncState,
							  otherNode->reportedLSN,
							  otherNode->candidatePriority,
							  otherNode->replicationQuorum,
							  message);
		}
		else
		{
			/* now proceed with the failover, starting with the first standby */
			(void) ProceedGroupState(firstStandbyNode);
		}
	}
	else if (IsStateIn(currentNode->reportedState, secondaryStates))
	{
		ReplicationState primaryGoalState =
			nodesCount == 2
			? REPLICATION_STATE_WAIT_PRIMARY
			: REPLICATION_STATE_JOIN_PRIMARY;

		/* the primary needs to be in a stable state to allow for maintenance */
		primaryNode = GetPrimaryNodeInGroup(currentNode->formationId,
											currentNode->groupId);

		if (primaryNode == NULL)
		{
			ereport(ERROR,
					(errmsg("couldn't find the primary node in formation \"%s\", "
							"group %d",
							currentNode->formationId, currentNode->groupId)));
		}


		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to %s and %s:%d to "
			"wait_maintenance after a user-initiated start_maintenance call.",
			primaryNode->nodeHost, primaryNode->nodePort,
			ReplicationStateGetName(primaryGoalState),
			currentNode->nodeHost, currentNode->nodePort);

		SetNodeGoalState(primaryNode->nodeHost, primaryNode->nodePort,
						 primaryGoalState);

		NotifyStateChange(primaryNode->reportedState,
						  primaryGoalState,
						  primaryNode->formationId,
						  primaryNode->groupId,
						  primaryNode->nodeId,
						  primaryNode->nodeHost,
						  primaryNode->nodePort,
						  primaryNode->pgsrSyncState,
						  primaryNode->reportedLSN,
						  primaryNode->candidatePriority,
						  primaryNode->replicationQuorum,
						  message);

		SetNodeGoalState(currentNode->nodeHost, currentNode->nodePort,
						 REPLICATION_STATE_WAIT_MAINTENANCE);

		NotifyStateChange(currentNode->reportedState,
						  REPLICATION_STATE_WAIT_MAINTENANCE,
						  currentNode->formationId,
						  currentNode->groupId,
						  currentNode->nodeId,
						  currentNode->nodeHost,
						  currentNode->nodePort,
						  currentNode->pgsrSyncState,
						  currentNode->reportedLSN,
						  currentNode->candidatePriority,
						  currentNode->replicationQuorum,
						  message);
	}
	else
	{
		/* should never happen... but still */
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: current state for node %s:%d "
						"is \"%s\", expected either \"primary\", "
						"\"secondary\" or \"catchingup\"",
						currentNode->nodeHost, currentNode->nodePort,
						ReplicationStateGetName(currentNode->reportedState))));
	}

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
	text *nodeHostText = PG_GETARG_TEXT_P(0);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(1);

	AutoFailoverNode *currentNode = NULL;
	AutoFailoverNode *primaryNode = NULL;

	char message[BUFSIZE];

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	if (!IsCurrentState(currentNode, REPLICATION_STATE_MAINTENANCE))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot stop maintenance when current state for "
						"node %s:%d is not \"maintenance\"",
						currentNode->nodeHost, currentNode->nodePort)));
	}

	/*
	 * We need to find the primary node even if we are in the middle of a
	 * failover, and it's already set to draining. That way we may rejoin the
	 * cluster, report our LSN, and help proceed to reach a consistent state.
	 */
	primaryNode =
		GetPrimaryOrDemotedNodeInGroup(currentNode->formationId,
									   currentNode->groupId);

	if (primaryNode == NULL)
	{
		ereport(ERROR,
				(errmsg("couldn't find the primary node in formation \"%s\", "
						"group %d",
						currentNode->formationId, currentNode->groupId)));
	}

	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of %s:%d to catchingup  "
		"after a user-initiated stop_maintenance call.",
		currentNode->nodeHost, currentNode->nodePort);

	SetNodeGoalState(currentNode->nodeHost, currentNode->nodePort,
					 REPLICATION_STATE_CATCHINGUP);

	NotifyStateChange(currentNode->reportedState,
					  REPLICATION_STATE_CATCHINGUP,
					  currentNode->formationId,
					  currentNode->groupId,
					  currentNode->nodeId,
					  currentNode->nodeHost,
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
	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(2);
	int candidatePriority = PG_GETARG_INT32(3);

	AutoFailoverNode *currentNode = NULL;

	List *nodesGroupList = NIL;
	ListCell *nodeCell = NULL;
	int nodesCount = 0;
	int nonZeroCandidatePriorityNodeCount = 0;

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNodeWithId(nodeId, nodeHost, nodePort);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errmsg("node %d is not registered", nodeId)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	nodesGroupList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);
	nodesCount = list_length(nodesGroupList);

	if (candidatePriority < 0 || candidatePriority > 100)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("invalid value for candidate_priority \"%d\" "
							   "expected an integer value between 0 and 100",
							   candidatePriority)));
	}

	if (candidatePriority == 0)
	{
		/*
		 * We need to ensure we have at least two nodes with a non-zero
		 * candidate priority, otherwise we can't failover. Those two nodes
		 * include the current primary.
		 */
		foreach(nodeCell, nodesGroupList)
		{
			AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

			if (node->candidatePriority > 0)
			{
				nonZeroCandidatePriorityNodeCount++;
			}
		}

		/* account for the change we're asked to implement */
		nonZeroCandidatePriorityNodeCount -= 1;

		if (nonZeroCandidatePriorityNodeCount < 2)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot set candidate priority to zero, we must "
							"have at least two nodes with non-zero candidate "
							"priority to allow for a failover")));
		}
	}

	currentNode->candidatePriority = candidatePriority;

	ReportAutoFailoverNodeReplicationSetting(
		currentNode->nodeId,
		currentNode->nodeHost,
		currentNode->nodePort,
		currentNode->candidatePriority,
		currentNode->replicationQuorum);

	if (nodesCount == 1)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Updating candidate priority to %d for node %d (%s:%d)",
			currentNode->candidatePriority,
			currentNode->nodeId,
			currentNode->nodeHost,
			currentNode->nodePort);
	}
	else
	{
		char message[BUFSIZE];

		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(currentNode->formationId,
								  currentNode->groupId);

		if (primaryNode == NULL)
		{
			ereport(ERROR,
					(errmsg("couldn't find the primary node in "
							"formation \"%s\", group %d",
							currentNode->formationId, currentNode->groupId)));
		}

		if (!IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot set candidate priority when current state "
							"for primary node %d (%s:%d) is \"%s\"",
							primaryNode->nodeId,
							primaryNode->nodeHost,
							primaryNode->nodePort,
							ReplicationStateGetName(primaryNode->reportedState)),
					 errdetail("The primary node so must be in state \"primary\" "
							   "to be able to apply configuration changes to "
							   "its synchronous_standby_names setting")));
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of node %d (%s:%d) to apply_settings "
			"after updating node %d (%s:%d) candidate priority to %d.",
			primaryNode->nodeId, primaryNode->nodeHost, primaryNode->nodePort,
			currentNode->nodeId, currentNode->nodeHost, currentNode->nodePort,
			currentNode->candidatePriority);

		SetNodeGoalState(primaryNode->nodeHost, primaryNode->nodePort,
						 REPLICATION_STATE_APPLY_SETTINGS);

		NotifyStateChange(primaryNode->reportedState,
						  REPLICATION_STATE_APPLY_SETTINGS,
						  primaryNode->formationId,
						  primaryNode->groupId,
						  primaryNode->nodeId,
						  primaryNode->nodeHost,
						  primaryNode->nodePort,
						  primaryNode->pgsrSyncState,
						  primaryNode->reportedLSN,
						  primaryNode->candidatePriority,
						  primaryNode->replicationQuorum,
						  message);
	}

	PG_RETURN_BOOL(true);
}


/*
 * set_node_replication_quorum sets node replication quorum property
 */
Datum
set_node_replication_quorum(PG_FUNCTION_ARGS)
{
	int32 nodeid = PG_GETARG_INT32(0);
	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(2);
	bool replicationQuorum = PG_GETARG_BOOL(3);

	AutoFailoverNode *currentNode = NULL;
	List *nodesGroupList = NIL;
	int nodesCount = 0;

	checkPgAutoFailoverVersion();

	currentNode = GetAutoFailoverNodeWithId(nodeid, nodeHost, nodePort);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errmsg("node %d is not registered", nodeid)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	nodesGroupList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);
	nodesCount = list_length(nodesGroupList);

	currentNode->replicationQuorum = replicationQuorum;

	ReportAutoFailoverNodeReplicationSetting(currentNode->nodeId,
											 currentNode->nodeHost,
											 currentNode->nodePort,
											 currentNode->candidatePriority,
											 currentNode->replicationQuorum);

	/* we need to see the result of that operation in the next query */
	CommandCounterIncrement();

	/* it's not always possible to opt-out from replication-quorum */
	if (!currentNode->replicationQuorum)
	{
		AutoFailoverFormation *formation =
			GetFormation(currentNode->formationId);

		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(formation->formationId, currentNode->groupId);

		int standbyCount = 0;

		if (primaryNode == NULL)
		{
			/* maybe we could use an Assert() instead? */
			ereport(ERROR,
					(errmsg("Couldn't find the primary node in "
							"formation \"%s\", group %d",
							formation->formationId, currentNode->groupId)));
		}

		if (!FormationNumSyncStandbyIsValid(formation,
											primaryNode,
											currentNode->groupId,
											&standbyCount))
		{
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("can't set replication quorum to false"),
					 errdetail("At least %d standby nodes are required "
							   "in formation %s with number_sync_standbys = %d, "
							   "and only %d would be participating in "
							   "the replication quorum",
							   formation->number_sync_standbys + 1,
							   formation->formationId,
							   formation->number_sync_standbys,
							   standbyCount)));
		}
	}

	if (nodesCount == 1)
	{
		char message[BUFSIZE];

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Updating replicationQuorum to %s for  %d (%s:%d)",
			currentNode->replicationQuorum ? "true" : "false",
			currentNode->nodeId,
			currentNode->nodeHost,
			currentNode->nodePort);
	}
	else
	{
		char message[BUFSIZE];

		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(currentNode->formationId,
								  currentNode->groupId);

		if (primaryNode == NULL)
		{
			ereport(ERROR,
					(errmsg("couldn't find the primary node in "
							"formation \"%s\", group %d",
							currentNode->formationId, currentNode->groupId)));
		}

		if (!IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot set replication quorum when current state "
							"for primary node %s:%d is \"%s\"",
							primaryNode->nodeHost, primaryNode->nodePort,
							ReplicationStateGetName(primaryNode->reportedState)),
					 errdetail("The primary node so must be in state \"primary\" "
							   "to be able to apply configuration changes to "
							   "its synchronous_standby_names setting")));
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of %s:%d to apply_settings "
			"after updating replication quorum to %s for node %s:%d.",
			primaryNode->nodeHost, primaryNode->nodePort,
			currentNode->replicationQuorum ? "true" : "false",
			currentNode->nodeHost, currentNode->nodePort);

		SetNodeGoalState(primaryNode->nodeHost, primaryNode->nodePort,
						 REPLICATION_STATE_APPLY_SETTINGS);

		NotifyStateChange(primaryNode->reportedState,
						  REPLICATION_STATE_APPLY_SETTINGS,
						  primaryNode->formationId,
						  primaryNode->groupId,
						  primaryNode->nodeId,
						  primaryNode->nodeHost,
						  primaryNode->nodePort,
						  primaryNode->pgsrSyncState,
						  primaryNode->reportedLSN,
						  primaryNode->candidatePriority,
						  primaryNode->replicationQuorum,
						  message);
	}

	PG_RETURN_BOOL(true);
}


/*
 * synchronous_standby_names returns the synchronous_standby_names parameter
 * value for a given Postgres service group in a given formation.
 */
Datum
synchronous_standby_names(PG_FUNCTION_ARGS)
{
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	int32 groupId = PG_GETARG_INT32(1);

	AutoFailoverFormation *formation = GetFormation(formationId);

	AutoFailoverNode *primaryNode = NULL;
	List *standbyNodesGroupList = NIL;

	List *nodesGroupList = AutoFailoverNodeGroup(formationId, groupId);
	int nodesCount = list_length(nodesGroupList);

	checkPgAutoFailoverVersion();

	/*
	 * When there's no nodes registered yet, there's no pg_autoctl process that
	 * needs the information anyway. Return NULL.
	 */
	if (nodesCount == 0)
	{
		PG_RETURN_NULL();
	}

	/* when we have a SINGLE node we disable synchronous replication */
	if (nodesCount == 1)
	{
		PG_RETURN_TEXT_P(cstring_to_text(""));
	}

	/* when we have more than one node, fetch the primary */
	primaryNode = GetPrimaryNodeInGroup(formationId, groupId);

	if (primaryNode == NULL)
	{
		ereport(ERROR,
				(errmsg("Couldn't find the primary node in formation \"%s\", "
						"group %d", formationId, groupId)));
	}

	standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);

	/*
	 * Single standby case, we assume formation->number_sync_standbys == 0
	 */
	if (nodesCount == 2)
	{
		AutoFailoverNode *secondaryNode = linitial(standbyNodesGroupList);

		if (secondaryNode != NULL &&
			secondaryNode->replicationQuorum &&
			IsCurrentState(secondaryNode, REPLICATION_STATE_SECONDARY))
		{
			/* enable synchronous replication */
			PG_RETURN_TEXT_P(cstring_to_text("*"));
		}
		else
		{
			/* disable synchronous replication */
			PG_RETURN_TEXT_P(cstring_to_text(""));
		}
	}

	/*
	 * General case now, we have multiple standbys each with a candidate
	 * priority, and with replicationQuorum (bool: true or false).
	 *
	 *   - candidateNodesGroupList contains only nodes that have a
	 *     candidatePriority greater than zero
	 *
	 *   - we skip nodes that have replicationQuorum set to false
	 *
	 *   - then we build synchronous_standby_names with one of the two
	 *     following models:
	 *
	 *       ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)
	 *       FIRST 1 (pgautofailover_standby_2, pgautofailover_standby_3)
	 *
	 *     We use ANY when all the standby nodes have the same
	 *     candidatePriority, and we use FIRST otherwise.
	 *
	 *     The num_sync number is the formation number_sync_standbys property.
	 */
	{
		List *syncStandbyNodesGroupList =
			GroupListSyncStandbys(standbyNodesGroupList);

		int count = list_length(syncStandbyNodesGroupList);

		if (count == 0)
		{
			/*
			 *  If no standby participates in the replication Quorum, we
			 * disable synchronous replication.
			 */
			PG_RETURN_TEXT_P(cstring_to_text(""));
		}
		else
		{
			bool allTheSamePriority =
				AllNodesHaveSameCandidatePriority(syncStandbyNodesGroupList);

			/*
			 * We accept number_sync_standbys to be set to zero to enable our
			 * failover trade-off, but won't send a synchronous_standby_names
			 * setting with ANY 0 () or FIRST 0 (), that would not make sense.
			 */
			int number_sync_standbys =
				formation->number_sync_standbys == 0
				? 1
				: formation->number_sync_standbys;

			StringInfo sbnames = makeStringInfo();
			ListCell *nodeCell = NULL;
			bool firstNode = true;

			appendStringInfo(sbnames,
							 "%s %d (",
							 allTheSamePriority ? "ANY" : "FIRST",
							 number_sync_standbys);

			foreach(nodeCell, syncStandbyNodesGroupList)
			{
				AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

				appendStringInfo(sbnames,
								 "%spgautofailover_standby_%d",
								 firstNode ? "" : ", ",
								 node->nodeId);

				if (firstNode)
				{
					firstNode = false;
				}
			}
			appendStringInfoString(sbnames, ")");

			PG_RETURN_TEXT_P(cstring_to_text(sbnames->data));
		}
	}
}
