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
										  AutoFailoverNodeState *currentNodeState);
static void JoinAutoFailoverFormation(AutoFailoverFormation *formation,
									  char *nodeName, char *nodeHost, int nodePort,
									  uint64 sysIdentifier, char *nodeCluster,
									  AutoFailoverNodeState *currentNodeState);
static int AssignGroupId(AutoFailoverFormation *formation,
						 char *nodeHost, int nodePort,
						 ReplicationState *initialState);

static bool RemoveNode(AutoFailoverNode *currentNode, bool force);

/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_node);
PG_FUNCTION_INFO_V1(node_active);
PG_FUNCTION_INFO_V1(update_node_metadata);
PG_FUNCTION_INFO_V1(get_nodes);
PG_FUNCTION_INFO_V1(get_primary);
PG_FUNCTION_INFO_V1(get_other_node);
PG_FUNCTION_INFO_V1(get_other_nodes);
PG_FUNCTION_INFO_V1(remove_node);
PG_FUNCTION_INFO_V1(remove_node_by_nodeid);
PG_FUNCTION_INFO_V1(remove_node_by_host);
PG_FUNCTION_INFO_V1(perform_failover);
PG_FUNCTION_INFO_V1(perform_promotion);
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
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(2);

	Name dbnameName = PG_GETARG_NAME(3);
	const char *expectedDBName = NameStr(*dbnameName);

	text *nodeNameText = PG_GETARG_TEXT_P(4);
	char *nodeName = text_to_cstring(nodeNameText);

	uint64 sysIdentifier = PG_GETARG_INT64(5);

	int64 currentNodeId = PG_GETARG_INT64(6);
	int32 currentGroupId = PG_GETARG_INT32(7);
	Oid currentReplicationStateOid = PG_GETARG_OID(8);

	text *nodeKindText = PG_GETARG_TEXT_P(9);
	char *nodeKind = text_to_cstring(nodeKindText);
	FormationKind expectedFormationKind =
		FormationKindFromNodeKindString(nodeKind);
	int candidatePriority = PG_GETARG_INT32(10);
	bool replicationQuorum = PG_GETARG_BOOL(11);

	text *nodeClusterText = PG_GETARG_TEXT_P(12);
	char *nodeCluster = text_to_cstring(nodeClusterText);

	AutoFailoverNodeState currentNodeState = { 0 };

	currentNodeState.nodeId = currentNodeId;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedLSN = 0;
	currentNodeState.candidatePriority = candidatePriority;
	currentNodeState.replicationQuorum = replicationQuorum;

	LockFormation(formationId, ExclusiveLock);

	AutoFailoverFormation *formation = GetFormation(formationId);

	/*
	 * The default formationId is "default" and of kind FORMATION_KIND_PGSQL.
	 * It might get used to manage a formation though. Check about that here,
	 * and when the first node registered is a Citus node, update the target
	 * formation to be of kind Citus, actually.
	 */
	if (formation == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						errmsg("formation \"%s\" does not exist", formationId),
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
			formation->kind = expectedFormationKind;
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

	/*
	 * The register_node() function is STRICT but users may have skipped the
	 * --name option on the create command line. We still want to avoid having
	 * to scan all the 10 parameters for ISNULL tests, so instead our client
	 * sends an empty string for the nodename.
	 */
	JoinAutoFailoverFormation(formation,
							  strcmp(nodeName, "") == 0 ? NULL : nodeName,
							  nodeHost,
							  nodePort,
							  sysIdentifier,
							  nodeCluster,
							  &currentNodeState);

	AutoFailoverNode *pgAutoFailoverNode = GetAutoFailoverNode(nodeHost, nodePort);
	if (pgAutoFailoverNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("node %s:%d with dbname \"%s\" could not be registered in "
						"formation \"%s\", could not get information for node that was inserted",
						nodeHost, nodePort, expectedDBName,
						formationId)));
	}
	else
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Registering " NODE_FORMAT
			" to formation \"%s\" "
			"with replication quorum %s and candidate priority %d [%d]",
			NODE_FORMAT_ARGS(pgAutoFailoverNode),
			pgAutoFailoverNode->formationId,
			pgAutoFailoverNode->replicationQuorum ? "true" : "false",
			pgAutoFailoverNode->candidatePriority,
			currentNodeState.candidatePriority);
	}

	/*
	 * When adding a second sync node to a formation that has
	 * number_sync_standbys set to zero (the default value for single node and
	 * single standby formations), we switch the default value to 1
	 * automatically.
	 */
	if (pgAutoFailoverNode->goalState == REPLICATION_STATE_WAIT_STANDBY &&
		formation->number_sync_standbys == 0)
	{
		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(formationId, currentNodeState.groupId);
		List *standbyNodesList = AutoFailoverOtherNodesList(primaryNode);
		int syncStandbyNodeCount = CountSyncStandbys(standbyNodesList);

		/*
		 * number_sync_standbys = 0 is a special case in our FSM, because we
		 * have special handling of a missing standby then, switching to
		 * wait_primary to disable synchronous replication when the standby is
		 * not available.
		 *
		 * For other values (N) of number_sync_standbys, we require N+1 known
		 * sync standby nodes, so that you can lose a standby at any point in
		 * time and still accept writes.
		 *
		 * The default value for number_sync_standbys with two standby nodes is
		 * 1. Because it was set to zero when adding the first standby, we need
		 * to increment the value when adding a second standby node that
		 * participates in the replication quorum (a "sync standby" node).
		 */
		if (syncStandbyNodeCount == 2)
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
				"Setting number_sync_standbys to %d for formation %s "
				"now that we have %d/%d standby nodes set with replication-quorum.",
				formation->number_sync_standbys,
				formation->formationId,
				syncStandbyNodeCount, list_length(standbyNodesList));
		}
	}

	AutoFailoverNodeState *assignedNodeState =
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

	TupleDesc resultDescriptor = NULL;
	Datum values[6];
	bool isNulls[6];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int64GetDatum(assignedNodeState->nodeId);
	values[1] = Int32GetDatum(assignedNodeState->groupId);
	values[2] = ObjectIdGetDatum(
		ReplicationStateGetEnum(pgAutoFailoverNode->goalState));
	values[3] = Int32GetDatum(assignedNodeState->candidatePriority);
	values[4] = BoolGetDatum(assignedNodeState->replicationQuorum);
	values[5] = CStringGetTextDatum(pgAutoFailoverNode->nodeName);

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

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
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	int64 currentNodeId = PG_GETARG_INT64(1);
	int32 currentGroupId = PG_GETARG_INT32(2);
	Oid currentReplicationStateOid = PG_GETARG_OID(3);
	bool currentPgIsRunning = PG_GETARG_BOOL(4);

	int32 currentTLI = PG_GETARG_INT32(5);
	XLogRecPtr currentLSN = PG_GETARG_LSN(6);

	text *currentPgsrSyncStateText = PG_GETARG_TEXT_P(7);
	char *currentPgsrSyncState = text_to_cstring(currentPgsrSyncStateText);

	AutoFailoverNodeState currentNodeState = { 0 };

	currentNodeState.nodeId = currentNodeId;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedTLI = currentTLI;
	currentNodeState.reportedLSN = currentLSN;
	currentNodeState.pgsrSyncState = SyncStateFromString(currentPgsrSyncState);
	currentNodeState.pgIsRunning = currentPgIsRunning;

	AutoFailoverNodeState *assignedNodeState =
		NodeActive(formationId, &currentNodeState);

	Oid newReplicationStateOid =
		ReplicationStateGetEnum(assignedNodeState->replicationState);

	TupleDesc resultDescriptor = NULL;
	Datum values[5];
	bool isNulls[5];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int64GetDatum(assignedNodeState->nodeId);
	values[1] = Int32GetDatum(assignedNodeState->groupId);
	values[2] = ObjectIdGetDatum(newReplicationStateOid);
	values[3] = Int32GetDatum(assignedNodeState->candidatePriority);
	values[4] = BoolGetDatum(assignedNodeState->replicationQuorum);

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * NodeActive reports the current state of a node and returns the assigned state.
 */
static AutoFailoverNodeState *
NodeActive(char *formationId, AutoFailoverNodeState *currentNodeState)
{
	AutoFailoverNode *pgAutoFailoverNode = GetAutoFailoverNodeById(
		currentNodeState->nodeId);

	if (pgAutoFailoverNode == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("couldn't find node with nodeid %lld",
							   (long long) currentNodeState->nodeId)));
	}
	else if (strcmp(pgAutoFailoverNode->formationId, formationId) != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						errmsg("node %lld does not belong to formation %s",
							   (long long) currentNodeState->nodeId,
							   formationId)));
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
			char message[BUFSIZE] = { 0 };

			if (pgAutoFailoverNode->goalState == REPLICATION_STATE_REPORT_LSN)
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"New state is reported by " NODE_FORMAT
					" with LSN %X/%X: %s",
					NODE_FORMAT_ARGS(pgAutoFailoverNode),
					(uint32) (pgAutoFailoverNode->reportedLSN >> 32),
					(uint32) pgAutoFailoverNode->reportedLSN,
					ReplicationStateGetName(currentNodeState->replicationState));
			}
			else
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"New state is reported by " NODE_FORMAT
					": \"%s\"",
					NODE_FORMAT_ARGS(pgAutoFailoverNode),
					ReplicationStateGetName(currentNodeState->replicationState));
			}

			pgAutoFailoverNode->reportedState = currentNodeState->replicationState;
			pgAutoFailoverNode->pgsrSyncState = currentNodeState->pgsrSyncState;
			pgAutoFailoverNode->reportedLSN = currentNodeState->reportedLSN;

			NotifyStateChange(pgAutoFailoverNode, message);
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
									currentNodeState->reportedTLI,
									currentNodeState->reportedLSN);
	}

	LockNodeGroup(formationId, currentNodeState->groupId, ExclusiveLock);

	ProceedGroupState(pgAutoFailoverNode);

	AutoFailoverNodeState *assignedNodeState =
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
						  char *nodeName, char *nodeHost, int nodePort,
						  uint64 sysIdentifier, char *nodeCluster,
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

	/* either a Citus groupId was asked for, or we're in group 0 for pgsql */
	if (currentNodeState->groupId >= 0)
	{
		groupId = currentNodeState->groupId;

		/*
		 * Now that we have a groupId, take an exclusive lock to avoid race
		 * conditions during registration, as the initial target state depends
		 * on the other nodes in the same group.
		 */
		LockNodeGroup(formation->formationId, groupId, ExclusiveLock);

		List *groupNodeList =
			AutoFailoverNodeGroup(formation->formationId, groupId);

		/*
		 * Target group is empty: to make it simple to reason about the roles
		 * in a group, we only ever accept a primary node first. Then, any
		 * other node in the same group should be a standby. That's easy.
		 */
		if (list_length(groupNodeList) == 0 &&
			currentNodeState->candidatePriority > 0)
		{
			initialState = REPLICATION_STATE_SINGLE;
		}

		/* target group already has a primary, any other node is a standby */
		else if (formation->opt_secondary)
		{
			initialState = REPLICATION_STATE_WAIT_STANDBY;

			/* if we have a primary node, pg_basebackup from it */
			AutoFailoverNode *primaryNode =
				GetPrimaryNodeInGroup(
					formation->formationId,
					currentNodeState->groupId);

			/* we might be in the middle of a failover */
			List *nodesGroupList =
				AutoFailoverNodeGroup(
					formation->formationId,
					currentNodeState->groupId);

			/* if we don't have a primary, look for a node being promoted */
			AutoFailoverNode *nodeBeingPromoted = NULL;

			/* we might have an upstream node that's not a failover candidate */
			bool foundUpstreamNode = false;

			if (primaryNode == NULL)
			{
				nodeBeingPromoted =
					FindCandidateNodeBeingPromoted(
						nodesGroupList);
			}

			/*
			 * If we don't have a primary node and we also don't have a node
			 * being promoted, it might be that all we have is a list of
			 * nodes with candidatePriority zero.
			 *
			 * When that happens, those nodes are assigned REPORT_LSN, in case
			 * a candidate could be promoted (and maybe fast-forwarded).
			 *
			 * If we find even a single node in REPORT_LSN and with candidate
			 * priority zero, we have an upstream node for creating a new
			 * node, that can then be promoted as the new primary.
			 */
			if (primaryNode == NULL && nodeBeingPromoted == NULL)
			{
				ListCell *nodeCell = NULL;

				foreach(nodeCell, nodesGroupList)
				{
					AutoFailoverNode *node =
						(AutoFailoverNode *) lfirst(nodeCell);

					if (node->candidatePriority == 0 &&
						IsCurrentState(node, REPLICATION_STATE_REPORT_LSN))
					{
						foundUpstreamNode = true;
						break;
					}
				}

				if (foundUpstreamNode)
				{
					initialState = REPLICATION_STATE_REPORT_LSN;
				}
			}

			/*
			 * If we can't figure it out, have the client handle the situation.
			 */
			if (!(primaryNode || nodeBeingPromoted || foundUpstreamNode))
			{
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_IN_USE),
						 errmsg("JoinAutoFailoverFormation couldn't find the "
								" primary node in formation \"%s\", group %d",
								formation->formationId,
								currentNodeState->groupId),
						 errhint("Retry registering in a moment")));
			}
		}

		/* formation->opt_secondary is false */
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("Formation \"%s\" does not allow secondary nodes",
							formation->formationId),
					 errhint("use pg_autoctl enable secondary")));
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
						formation->kind,
						currentNodeState->nodeId,
						groupId,
						nodeName,
						nodeHost,
						nodePort,
						sysIdentifier,
						initialState,
						currentNodeState->replicationState,
						currentNodeState->candidatePriority,
						currentNodeState->replicationQuorum,
						nodeCluster);

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
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	int32 groupId = PG_GETARG_INT32(1);


	TupleDesc resultDescriptor = NULL;
	Datum values[4];
	bool isNulls[4];

	AutoFailoverNode *primaryNode =
		GetPrimaryOrDemotedNodeInGroup(formationId, groupId);

	if (primaryNode == NULL)
	{
		ereport(ERROR, (errmsg("group has no writable node right now")));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int64GetDatum(primaryNode->nodeId);
	values[1] = CStringGetTextDatum(primaryNode->nodeName);
	values[2] = CStringGetTextDatum(primaryNode->nodeHost);
	values[3] = Int32GetDatum(primaryNode->nodePort);

	TypeFuncClass resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


typedef struct get_nodes_fctx
{
	List *nodesList;
} get_nodes_fctx;

/*
 * get_nodes returns all the node in a group, if any.
 */
Datum
get_nodes(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

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

			fctx->nodesList = AutoFailoverAllNodesInGroup(formationId, groupId);
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
		Datum values[6];
		bool isNulls[6];

		AutoFailoverNode *node = (AutoFailoverNode *) linitial(fctx->nodesList);

		memset(values, 0, sizeof(values));
		memset(isNulls, false, sizeof(isNulls));

		values[0] = Int64GetDatum(node->nodeId);
		values[1] = CStringGetTextDatum(node->nodeName);
		values[2] = CStringGetTextDatum(node->nodeHost);
		values[3] = Int32GetDatum(node->nodePort);
		values[4] = LSNGetDatum(node->reportedLSN);
		values[5] = BoolGetDatum(CanTakeWritesInState(node->reportedState));

		TypeFuncClass resultTypeClass = get_call_result_type(fcinfo, NULL,
															 &resultDescriptor);
		if (resultTypeClass != TYPEFUNC_COMPOSITE)
		{
			ereport(ERROR, (errmsg("return type must be a row type")));
		}

		HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
		Datum resultDatum = HeapTupleGetDatum(resultTuple);

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
	checkPgAutoFailoverVersion();

	FuncCallContext *funcctx;
	get_nodes_fctx *fctx;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		int64 nodeId = PG_GETARG_INT64(0);


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
		AutoFailoverNode *activeNode = GetAutoFailoverNodeById(nodeId);
		if (activeNode == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
							errmsg("node %lld is not registered",
								   (long long) nodeId)));
		}

		if (PG_NARGS() == 1)
		{
			fctx->nodesList = AutoFailoverOtherNodesList(activeNode);
		}
		else if (PG_NARGS() == 2)
		{
			Oid currentReplicationStateOid = PG_GETARG_OID(1);
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
		Datum values[6];
		bool isNulls[6];

		AutoFailoverNode *node = (AutoFailoverNode *) linitial(fctx->nodesList);

		memset(values, 0, sizeof(values));
		memset(isNulls, false, sizeof(isNulls));

		values[0] = Int64GetDatum(node->nodeId);
		values[1] = CStringGetTextDatum(node->nodeName);
		values[2] = CStringGetTextDatum(node->nodeHost);
		values[3] = Int32GetDatum(node->nodePort);
		values[4] = LSNGetDatum(node->reportedLSN);
		values[5] = BoolGetDatum(CanTakeWritesInState(node->reportedState));

		TypeFuncClass resultTypeClass = get_call_result_type(fcinfo, NULL,
															 &resultDescriptor);
		if (resultTypeClass != TYPEFUNC_COMPOSITE)
		{
			ereport(ERROR, (errmsg("return type must be a row type")));
		}

		HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
		Datum resultDatum = HeapTupleGetDatum(resultTuple);

		/* prepare next SRF call */
		fctx->nodesList = list_delete_first(fctx->nodesList);

		SRF_RETURN_NEXT(funcctx, PointerGetDatum(resultDatum));
	}

	SRF_RETURN_DONE(funcctx);
}


/*
 * remove_node is not supported anymore, but we might want to be able to have
 * the pgautofailover.so for 1.1 co-exists with the SQL definitions for 1.0 at
 * least during an upgrade, or to test upgrades.
 */
Datum
remove_node(PG_FUNCTION_ARGS)
{
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("pgautofailover.remove_node is no longer supported")));
}


/*
 * remove_node removes the given node from the monitor.
 */
Datum
remove_node_by_nodeid(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int64 nodeId = PG_GETARG_INT64(0);
	bool force = PG_GETARG_BOOL(1);

	AutoFailoverNode *currentNode = GetAutoFailoverNodeById(nodeId);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("couldn't find node with nodeid %lld",
							   (long long) nodeId)));
	}

	PG_RETURN_BOOL(RemoveNode(currentNode, force));
}


/*
 * remove_node removes the given node from the monitor.
 */
Datum
remove_node_by_host(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *nodeHostText = PG_GETARG_TEXT_P(0);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(1);
	bool force = PG_GETARG_BOOL(2);

	AutoFailoverNode *currentNode = GetAutoFailoverNode(nodeHost, nodePort);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("couldn't find node with "
							   "hostname \"%s\" and port %d",
							   nodeHost, nodePort)));
	}

	PG_RETURN_BOOL(RemoveNode(currentNode, force));
}


/* RemoveNode removes the given node from the monitor. */
static bool
RemoveNode(AutoFailoverNode *currentNode, bool force)
{
	ListCell *nodeCell = NULL;
	char message[BUFSIZE] = { 0 };

	if (currentNode == NULL)
	{
		return false;
	}

	LockFormation(currentNode->formationId, ExclusiveLock);

	AutoFailoverFormation *formation = GetFormation(currentNode->formationId);

	/* when removing the primary, initiate a failover */
	bool currentNodeIsPrimary = CanTakeWritesInState(currentNode->goalState);

	/* get the list of the other nodes */
	List *otherNodesGroupList = AutoFailoverOtherNodesList(currentNode);

	/* and the first other node to trigger our first FSM transition */
	AutoFailoverNode *firstStandbyNode =
		otherNodesGroupList == NIL ? NULL : linitial(otherNodesGroupList);

	/*
	 * To remove a node is a 2-step process.
	 *
	 *  1. pgautofailover.remove_node() sets the goal state to DROPPED
	 *  2. pgautofailover.node_active() reports that the goal state is reached
	 *
	 * From the client side though, if a crash happens after having called the
	 * node_active() function but before having stored the state, it might be
	 * useful to call pgautofailover.remove_node() again.
	 *
	 * When pgautofailover.remove_node() is called on a node that has already
	 * reached the DROPPED state, we proceed to remove it.
	 */
	if (IsCurrentState(currentNode, REPLICATION_STATE_DROPPED) || force)
	{
		/* time to actually remove the current node */
		RemoveAutoFailoverNode(currentNode);

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Removing " NODE_FORMAT " from formation \"%s\" and group %d",
			NODE_FORMAT_ARGS(currentNode),
			currentNode->formationId,
			currentNode->groupId);

		return true;
	}

	/* if the removal is already in progress, politely ignore the request */
	if (currentNode->goalState == REPLICATION_STATE_DROPPED)
	{
		return true;
	}

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
				"Setting goal state of " NODE_FORMAT
				" to report_lsn after primary node removal.",
				NODE_FORMAT_ARGS(node));

			SetNodeGoalState(node, REPLICATION_STATE_REPORT_LSN, message);
		}
	}

	/*
	 * Mark the node as being dropped, so that the pg_autoctl node-active
	 * process can implement further actions at drop time.
	 */
	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of " NODE_FORMAT
		" from formation \"%s\" and group %d to \"dropped\""
		" to implement node removal.",
		NODE_FORMAT_ARGS(currentNode),
		currentNode->formationId,
		currentNode->groupId);

	SetNodeGoalState(currentNode, REPLICATION_STATE_DROPPED, message);

	/*
	 * Adjust number-sync-standbys if necessary.
	 *
	 * otherNodesGroupList is the list of all the remaining nodes, and that
	 * includes the current primary, which might be setup with replication
	 * quorum set to true (and probably is).
	 */
	int countSyncStandbys = CountSyncStandbys(otherNodesGroupList) - 1;

	if (countSyncStandbys < (formation->number_sync_standbys + 1))
	{
		formation->number_sync_standbys = countSyncStandbys - 1;

		if (formation->number_sync_standbys < 0)
		{
			formation->number_sync_standbys = 0;
		}

		if (!SetFormationNumberSyncStandbys(formation->formationId,
											formation->number_sync_standbys))
		{
			ereport(ERROR,
					(errmsg("couldn't set the formation \"%s\" "
							"number_sync_standbys to %d now that a "
							"standby node has been removed",
							currentNode->formationId,
							formation->number_sync_standbys)));
		}

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting number_sync_standbys to %d for formation \"%s\" "
			"now that we have %d standby nodes set with replication-quorum.",
			formation->number_sync_standbys,
			formation->formationId,
			countSyncStandbys);
	}

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
			GetPrimaryNodeInGroup(currentNode->formationId,
								  currentNode->groupId);

		if (primaryNode)
		{
			ReplicationState goalState = primaryNode->goalState;

			(void) ProceedGroupState(primaryNode);

			/*
			 * When the removal of a secondary node has no impact on the
			 * primary node state, we still need to change the replication
			 * settings to adjust to the possibly new
			 * synchronous_standby_names, so we force APPLY_SETTINGS in that
			 * case.
			 */
			if (primaryNode->goalState == goalState &&
				goalState != REPLICATION_STATE_APPLY_SETTINGS)
			{
				LogAndNotifyMessage(
					message, BUFSIZE,
					"Setting goal state of " NODE_FORMAT
					" to apply_settings after removing standby " NODE_FORMAT
					" from formation %s.",
					NODE_FORMAT_ARGS(primaryNode),
					NODE_FORMAT_ARGS(currentNode),
					formation->formationId);

				SetNodeGoalState(primaryNode,
								 REPLICATION_STATE_APPLY_SETTINGS, message);
			}
		}
	}

	return true;
}


/*
 * perform_failover promotes the secondary in the given group
 */
Datum
perform_failover(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	int32 groupId = PG_GETARG_INT32(1);

	LockFormation(formationId, ShareLock);
	LockNodeGroup(formationId, groupId, ExclusiveLock);

	List *groupNodeList = AutoFailoverNodeGroup(formationId, groupId);
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
	AutoFailoverNode *primaryNode =
		GetNodeToFailoverFromInGroup(formationId, groupId);

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
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);

		if (list_length(standbyNodesGroupList) != 1)
		{
			ereport(ERROR,
					(errmsg("couldn't find the standby node in "
							"formation \"%s\", group %d with primary node "
							NODE_FORMAT,
							formationId, groupId,
							NODE_FORMAT_ARGS(primaryNode))));
		}

		AutoFailoverNode *secondaryNode = linitial(standbyNodesGroupList);

		if (secondaryNode->goalState != REPLICATION_STATE_SECONDARY)
		{
			const char *secondaryState =
				ReplicationStateGetName(secondaryNode->goalState);

			ereport(ERROR,
					(errmsg(
						 "standby " NODE_FORMAT
						 " is in state \"%s\", "
						 "which prevents the node for being a failover candidate",
						 NODE_FORMAT_ARGS(secondaryNode),
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
					 errdetail(NODE_FORMAT
							   " has reported state \"%s\" and"
							   " is assigned state \"%s\","
							   " and " NODE_FORMAT
							   " has reported state \"%s\""
							   " and is assigned state \"%s\"",
							   NODE_FORMAT_ARGS(primaryNode),
							   ReplicationStateGetName(primaryNode->reportedState),
							   ReplicationStateGetName(primaryNode->goalState),
							   NODE_FORMAT_ARGS(secondaryNode),
							   ReplicationStateGetName(secondaryNode->reportedState),
							   ReplicationStateGetName(secondaryNode->goalState)),
					 errhint("a stable state must be observed to "
							 "perform a manual failover")));
		}

		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to draining and " NODE_FORMAT
			" to prepare_promotion after a user-initiated failover.",
			NODE_FORMAT_ARGS(primaryNode),
			NODE_FORMAT_ARGS(secondaryNode));

		SetNodeGoalState(primaryNode,
						 REPLICATION_STATE_DRAINING, message);

		SetNodeGoalState(secondaryNode,
						 REPLICATION_STATE_PREPARE_PROMOTION, message);
	}
	else
	{
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);
		AutoFailoverNode *firstStandbyNode = linitial(standbyNodesGroupList);
		char message[BUFSIZE] = { 0 };

		/* so we have at least one candidate, let's get started */
		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			"at LSN %X/%X to draining after a user-initiated failover.",
			NODE_FORMAT_ARGS(primaryNode),
			(uint32) (primaryNode->reportedLSN >> 32),
			(uint32) primaryNode->reportedLSN);

		SetNodeGoalState(primaryNode, REPLICATION_STATE_DRAINING, message);

		/*
		 * When a failover is performed with all the nodes up and running, the
		 * old primary is often in the best situation to win the election. In
		 * that case, we trick the candidate priority in a way that makes the
		 * node lose the election.
		 *
		 * We undo this change in priority once the election completes.
		 */
		if (primaryNode)
		{
			char message[BUFSIZE] = { 0 };

			primaryNode->candidatePriority -= CANDIDATE_PRIORITY_INCREMENT;

			ReportAutoFailoverNodeReplicationSetting(
				primaryNode->nodeId,
				primaryNode->nodeHost,
				primaryNode->nodePort,
				primaryNode->candidatePriority,
				primaryNode->replicationQuorum);

			LogAndNotifyMessage(
				message, BUFSIZE,
				"Updating candidate priority to %d for " NODE_FORMAT,
				primaryNode->candidatePriority,
				NODE_FORMAT_ARGS(primaryNode));

			NotifyStateChange(primaryNode, message);
		}

		/* now proceed with the failover, starting with the first standby */
		(void) ProceedGroupState(firstStandbyNode);
	}

	PG_RETURN_VOID();
}


/*
 * promote promotes a given target node in a group.
 */
Datum
perform_promotion(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);


	AutoFailoverNode *currentNode =
		GetAutoFailoverNodeByName(formationId, nodeName);

	if (currentNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("node \"%s\" is not registered in formation \"%s\"",
						nodeName, formationId)));
	}

	LockFormation(formationId, ShareLock);
	LockNodeGroup(formationId, currentNode->groupId, ExclusiveLock);

	/*
	 * If the current node is the primary, that's done.
	 */
	if (IsCurrentState(currentNode, REPLICATION_STATE_SINGLE) ||
		IsCurrentState(currentNode, REPLICATION_STATE_PRIMARY))
	{
		/* return false: no promotion is happening */
		ereport(NOTICE,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot perform promotion: node %s in formation %s "
						"is already a primary.",
						nodeName, formationId)));
		PG_RETURN_BOOL(false);
	}

	/*
	 * If the node is not a primary, it needs to be in the SECONDARY state or
	 * in the REPORT_LSN state. In the case where none of the nodes are a
	 * candidate for failover, and the primary has been lost, all the remaining
	 * nodes are assigned REPORT_LSN, and we make it possible to then manually
	 * promote one of them.
	 *
	 * When we call perform_failover() to implement the actual failover
	 * orchestration, this condition is going to be checked again, but in a
	 * different way.
	 *
	 * For instance, the target could be in MAINTENANCE and perform_failover
	 * would still be able to implement a failover given another secondary node
	 * being around.
	 */
	if (!IsCurrentState(currentNode, REPLICATION_STATE_SECONDARY) &&
		!IsCurrentState(currentNode, REPLICATION_STATE_REPORT_LSN))
	{
		/* return false: no promotion is happening */
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg(
					 "cannot perform promotion: node %s in formation %s "
					 "has reported state \"%s\" and is assigned state \"%s\", "
					 "promotion can only be performed when "
					 "in state \"secondary\".",
					 nodeName, formationId,
					 ReplicationStateGetName(currentNode->reportedState),
					 ReplicationStateGetName(currentNode->goalState))));
	}

	/*
	 * If we have only two nodes in the group, then perform a failover.
	 */
	List *groupNodesList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);

	int totalNodesCount = list_length(groupNodesList);

	if (totalNodesCount <= 2)
	{
		DirectFunctionCall2(perform_failover,
							CStringGetTextDatum(formationId),
							Int32GetDatum(currentNode->groupId));

		/* if we reach this point, then a failover is in progress */
		PG_RETURN_BOOL(true);
	}
	else
	{
		char message[BUFSIZE] = { 0 };

		/*
		 * In the general case, we perform a little trick:
		 *
		 *  - first increment the node's candidate-priority by 100,
		 *
		 *  - then call perform_failover,
		 *
		 *  - when the node reaches WAIT_PRIMARY again, after promotion, reset
		 *    its candidate priority.
		 */
		currentNode->candidatePriority += CANDIDATE_PRIORITY_INCREMENT;

		ReportAutoFailoverNodeReplicationSetting(
			currentNode->nodeId,
			currentNode->nodeHost,
			currentNode->nodePort,
			currentNode->candidatePriority,
			currentNode->replicationQuorum);

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Updating candidate priority to %d for " NODE_FORMAT,
			currentNode->candidatePriority,
			NODE_FORMAT_ARGS(currentNode));

		NotifyStateChange(currentNode, message);

		/*
		 * In case of errors in the perform_failover function, we ereport an
		 * ERROR and that causes the transaction to fail (ROLLBACK). In that
		 * case, the UPDATE of the candidate priority in the
		 * pgautofailover.node table is also cancelled, and the notification
		 * above is not sent either.
		 */
		DirectFunctionCall2(perform_failover,
							CStringGetTextDatum(formationId),
							Int32GetDatum(currentNode->groupId));

		/* if we reach this point, then a failover is in progress */
		PG_RETURN_BOOL(true);
	}

	/* can't happen, keep compiler happy */
	PG_RETURN_BOOL(false);
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
	checkPgAutoFailoverVersion();

	int64 nodeId = PG_GETARG_INT64(0);

	AutoFailoverNode *primaryNode = NULL;

	List *secondaryStates = list_make2_int(REPLICATION_STATE_SECONDARY,
										   REPLICATION_STATE_CATCHINGUP);


	char message[BUFSIZE];

	AutoFailoverNode *currentNode = GetAutoFailoverNodeById(nodeId);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	AutoFailoverFormation *formation = GetFormation(currentNode->formationId);

	List *groupNodesList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);

	int totalNodesCount = list_length(groupNodesList);

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

	/*
	 * We now need to have the primary node identified, and the list of the
	 * secondary nodes (not including those already in maintenance), to decide
	 * if we can proceed.
	 */
	if (IsCurrentState(currentNode, REPLICATION_STATE_PRIMARY))
	{
		primaryNode = currentNode;
	}
	else
	{
		primaryNode = GetPrimaryNodeInGroup(currentNode->formationId,
											currentNode->groupId);

		if (primaryNode == NULL)
		{
			ereport(ERROR,
					(errmsg("couldn't find the primary node in formation \"%s\", "
							"group %d",
							currentNode->formationId, currentNode->groupId)));
		}
	}

	/*
	 * We need to always have at least formation->number_sync_standbys nodes in
	 * the SECONDARY state participating in the quorum, otherwise writes may be
	 * blocked on the primary. In case when we know we will have to block
	 * writes, warn our user.
	 *
	 * As they might still need to operate this maintenance operation, we won't
	 * forbid it by erroring out, though.
	 */
	List *secondaryNodesList =
		AutoFailoverOtherNodesListInState(primaryNode,
										  REPLICATION_STATE_SECONDARY);

	int candidatesCount = CountHealthyCandidates(secondaryNodesList);
	int secondaryNodesCount = CountHealthySyncStandbys(secondaryNodesList);

	if (formation->number_sync_standbys > 0 &&
		secondaryNodesCount <= formation->number_sync_standbys &&
		IsHealthySyncStandby(currentNode))
	{
		ereport(WARNING,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("Starting maintenance on " NODE_FORMAT
						" will block writes on the primary " NODE_FORMAT,
						NODE_FORMAT_ARGS(currentNode),
						NODE_FORMAT_ARGS(primaryNode)),
				 errdetail("we now have %d "
						   "healthy node(s) left in the \"secondary\" state "
						   "and formation \"%s\" number-sync-standbys requires "
						   "%d sync standbys",
						   secondaryNodesCount - 1,
						   formation->formationId,
						   formation->number_sync_standbys)));
	}

	/*
	 * Now that we cleared that adding another node in MAINTENANCE is
	 * compatible with our service expectations from
	 * formation->number_sync_standbys, we may proceed.
	 *
	 * We proceed in different ways when asked to put a primary or a secondary
	 * to maintenance: in the case of a primary, we must failover.
	 */
	if (IsCurrentState(currentNode, REPLICATION_STATE_PRIMARY))
	{
		List *standbyNodesGroupList = AutoFailoverOtherNodesList(currentNode);
		AutoFailoverNode *firstStandbyNode = linitial(standbyNodesGroupList);
		char message[BUFSIZE] = { 0 };

		/*
		 * We need at least one candidate node to initiate a failover and allow
		 * the primary to reach maintenance.
		 */
		if (candidatesCount < 1)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("Starting maintenance on " NODE_FORMAT
							" in state \"%s\" is not currently possible",
							NODE_FORMAT_ARGS(currentNode),
							ReplicationStateGetName(currentNode->reportedState)),
					 errdetail("there is currently %d candidate nodes available",
							   candidatesCount)));
		}

		if (totalNodesCount == 2)
		{
			/*
			 * Set the primary to prepare_maintenance now, and if we have a
			 * single secondary we assign it prepare_promotion, otherwise we
			 * need to elect a secondary, same as in perform_failover.
			 */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to prepare_maintenance "
				"after a user-initiated start_maintenance call.",
				NODE_FORMAT_ARGS(currentNode));

			SetNodeGoalState(currentNode,
							 REPLICATION_STATE_PREPARE_MAINTENANCE, message);

			AutoFailoverNode *otherNode = firstStandbyNode;

			/*
			 * We put the only secondary node straight to prepare_replication.
			 */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to prepare_maintenance and " NODE_FORMAT
				" to prepare_promotion "
				"after a user-initiated start_maintenance call.",
				NODE_FORMAT_ARGS(currentNode),
				NODE_FORMAT_ARGS(otherNode));

			SetNodeGoalState(otherNode,
							 REPLICATION_STATE_PREPARE_PROMOTION, message);
		}
		else
		{
			/* put the primary directly to maintenance */
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to maintenance "
				"after a user-initiated start_maintenance call.",
				NODE_FORMAT_ARGS(currentNode));

			SetNodeGoalState(currentNode,
							 REPLICATION_STATE_PREPARE_MAINTENANCE, message);

			/* now proceed with the failover, starting with the first standby */
			(void) ProceedGroupState(firstStandbyNode);
		}

		PG_RETURN_BOOL(true);
	}

	/*
	 * Only allow a secondary to get to MAINTENANCE when the primary is in the
	 * PRIMARY state.
	 */
	else if (IsStateIn(currentNode->reportedState, secondaryStates) &&
			 IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY))
	{
		/*
		 * In most cases we can simply put a secondary directly into
		 * maintenance mode. However, when putting the last secondary node
		 * that's part of the replication quorum to maintenance, we disable
		 * sync rep on the primary by switching it to wait_primary. Otherwise
		 * the primary won't be able to accept writes until the monitor assigns
		 * it wait_primary. This way we're nice about it and don't bring the
		 * secondary down before that happens. Because we didn't change the
		 * state of any standby node yet, we get there when the count is one
		 * (not zero).
		 */
		if (formation->number_sync_standbys == 0 && secondaryNodesCount == 1 &&
			IsHealthySyncStandby(currentNode))
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to wait_primary and " NODE_FORMAT
				" to wait_maintenance "
				"after a user-initiated start_maintenance call.",
				NODE_FORMAT_ARGS(primaryNode),
				NODE_FORMAT_ARGS(currentNode));
			SetNodeGoalState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY, message);
			SetNodeGoalState(currentNode, REPLICATION_STATE_WAIT_MAINTENANCE, message);
		}
		else
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to maintenance "
				"after a user-initiated start_maintenance call.",
				NODE_FORMAT_ARGS(currentNode));
			SetNodeGoalState(currentNode, REPLICATION_STATE_MAINTENANCE, message);
		}
	}
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot start maintenance: current state for "
						NODE_FORMAT
						" is \"%s\", expected \"secondary\" or \"catchingup\", "
						"and current state for primary " NODE_FORMAT
						" is \"%s\" ➜ \"%s\" ",
						NODE_FORMAT_ARGS(currentNode),
						ReplicationStateGetName(currentNode->reportedState),
						NODE_FORMAT_ARGS(primaryNode),
						ReplicationStateGetName(primaryNode->reportedState),
						ReplicationStateGetName(primaryNode->goalState))));
	}

	PG_RETURN_BOOL(true);
}


/*
 * stop_maintenance brings a node back from maintenance to a participating
 * member of the formation. Depending on the state of the formation it's either
 * assigned catchingup or report_lsn.
 *
 * This operation is only allowed on a node that's in the maintenance state.
 */
Datum
stop_maintenance(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int64 nodeId = PG_GETARG_INT64(0);

	char message[BUFSIZE] = { 0 };

	AutoFailoverNode *currentNode = GetAutoFailoverNodeById(nodeId);
	if (currentNode == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	List *groupNodesList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);

	int totalNodesCount = list_length(groupNodesList);

	if (!IsCurrentState(currentNode, REPLICATION_STATE_MAINTENANCE) &&
		!(totalNodesCount > 2 &&
		  IsCurrentState(currentNode, REPLICATION_STATE_PREPARE_MAINTENANCE)))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot stop maintenance when current state for "
						NODE_FORMAT
						" is not \"maintenance\"",
						NODE_FORMAT_ARGS(currentNode)),
				 errdetail("Current reported state is \"%s\" and "
						   "assigned state is \"%s\"",
						   ReplicationStateGetName(currentNode->reportedState),
						   ReplicationStateGetName(currentNode->goalState))));
	}

	/*
	 * We need to find the primary node even if we are in the middle of a
	 * failover, and it's already set to draining. That way we may rejoin the
	 * cluster, report our LSN, and help proceed to reach a consistent state.
	 */
	AutoFailoverNode *primaryNode =
		GetPrimaryOrDemotedNodeInGroup(currentNode->formationId,
									   currentNode->groupId);

	/*
	 * When there is no primary, we might be in trouble, we just want to join
	 * the possibily ongoing election.
	 */
	if (totalNodesCount == 1)
	{
		(void) ProceedGroupState(currentNode);

		PG_RETURN_BOOL(true);
	}
	else if (primaryNode == NULL && totalNodesCount == 2)
	{
		ereport(ERROR,
				(errmsg("couldn't find the primary node in formation \"%s\", "
						"group %d",
						currentNode->formationId, currentNode->groupId)));
	}
	else if (primaryNode == NULL && totalNodesCount > 2)
	{
		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to report_lsn  after a user-initiated stop_maintenance call.",
			NODE_FORMAT_ARGS(currentNode));

		SetNodeGoalState(currentNode, REPLICATION_STATE_REPORT_LSN, message);

		PG_RETURN_BOOL(true);
	}

	/*
	 * When a failover is in progress and stop_maintenance() is called (by
	 * means of pg_autoctl disable maintenance or otherwise), then we should
	 * join the crew on REPORT_LSN: the last known primary can be presumed
	 * down.
	 */
	if (IsFailoverInProgress(groupNodesList))
	{
		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup  after a user-initiated stop_maintenance call.",
			NODE_FORMAT_ARGS(currentNode));

		SetNodeGoalState(currentNode, REPLICATION_STATE_REPORT_LSN, message);
	}
	else
	{
		LogAndNotifyMessage(
			message, BUFSIZE,
			"Setting goal state of " NODE_FORMAT
			" to catchingup  after a user-initiated stop_maintenance call.",
			NODE_FORMAT_ARGS(currentNode));

		SetNodeGoalState(currentNode, REPLICATION_STATE_CATCHINGUP, message);
	}

	PG_RETURN_BOOL(true);
}


/*
 * set_node_candidate_priority sets node candidate priority property
 */
Datum
set_node_candidate_priority(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);

	int candidatePriority = PG_GETARG_INT32(2);


	ListCell *nodeCell = NULL;
	int nonZeroCandidatePriorityNodeCount = 0;

	AutoFailoverNode *currentNode =
		GetAutoFailoverNodeByName(formationId, nodeName);

	if (currentNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("node \"%s\" is not registered in formation \"%s\"",
						nodeName, formationId)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	List *nodesGroupList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);
	int nodesCount = list_length(nodesGroupList);

	if (candidatePriority < 0 ||
		candidatePriority > MAX_USER_DEFINED_CANDIDATE_PRIORITY)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("invalid value for candidate_priority \"%d\" "
							   "expected an integer value between 0 and %d",
							   candidatePriority,
							   MAX_USER_DEFINED_CANDIDATE_PRIORITY)));
	}

	if (strcmp(currentNode->nodeCluster, "default") != 0 &&
		candidatePriority != 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid value for candidate_priority: "
						"read-replica nodes in a citus cluster must always "
						"have candidate priority set to zero")));
	}

	if (candidatePriority == 0 && currentNode->candidatePriority != 0)
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
			ereport(NOTICE,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("setting candidate priority to zero, preventing "
							"automated failover"),
					 errdetail("Group %d in formation \"%s\" have no "
							   "failover candidate.",
							   currentNode->groupId, formationId)));
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
			"Updating candidate priority to %d for " NODE_FORMAT,
			currentNode->candidatePriority,
			NODE_FORMAT_ARGS(currentNode));

		NotifyStateChange(currentNode, message);
	}
	else
	{
		char message[BUFSIZE];

		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(currentNode->formationId,
								  currentNode->groupId);

		/*
		 * If we allow setting changes during APPLY_SETTINGS we open the door
		 * for race conditions where we can't be sure that the latest changes
		 * have been applied.
		 *
		 * If we don't currently have a primary node anyway, we can just
		 * proceed with the change.
		 */
		if (primaryNode &&
			!IsCurrentState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS))
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to apply_settings after updating " NODE_FORMAT
				" candidate priority to %d.",
				NODE_FORMAT_ARGS(primaryNode),
				NODE_FORMAT_ARGS(currentNode),
				currentNode->candidatePriority);

			SetNodeGoalState(primaryNode,
							 REPLICATION_STATE_APPLY_SETTINGS, message);
		}

		/* if primaryNode is not NULL, then current state is APPLY_SETTINGS */
		else if (primaryNode)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot set candidate priority when current state "
							"for primary " NODE_FORMAT
							" is \"%s\"",
							NODE_FORMAT_ARGS(primaryNode),
							ReplicationStateGetName(primaryNode->reportedState))));
		}

		/* other case is that we failed to find a primary node, proceed */
	}

	PG_RETURN_BOOL(true);
}


/*
 * set_node_replication_quorum sets node replication quorum property
 */
Datum
set_node_replication_quorum(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	text *nodeNameText = PG_GETARG_TEXT_P(1);
	char *nodeName = text_to_cstring(nodeNameText);

	bool replicationQuorum = PG_GETARG_BOOL(2);


	AutoFailoverNode *currentNode =
		GetAutoFailoverNodeByName(formationId, nodeName);

	if (currentNode == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("node \"%s\" is not registered in formation \"%s\"",
						nodeName, formationId)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	List *nodesGroupList =
		AutoFailoverNodeGroup(currentNode->formationId, currentNode->groupId);
	int nodesCount = list_length(nodesGroupList);

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
			"Updating replicationQuorum to %s for " NODE_FORMAT,
			currentNode->replicationQuorum ? "true" : "false",
			NODE_FORMAT_ARGS(currentNode));

		NotifyStateChange(currentNode, message);
	}
	else
	{
		char message[BUFSIZE];

		AutoFailoverNode *primaryNode =
			GetPrimaryNodeInGroup(currentNode->formationId,
								  currentNode->groupId);

		/*
		 * If we allow setting changes during APPLY_SETTINGS we open the door
		 * for race conditions where we can't be sure that the latest changes
		 * have been applied.
		 *
		 * If we don't currently have a primary node anyway, we can just
		 * proceed with the change.
		 */
		if (primaryNode &&
			!IsCurrentState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS))
		{
			LogAndNotifyMessage(
				message, BUFSIZE,
				"Setting goal state of " NODE_FORMAT
				" to apply_settings after updating " NODE_FORMAT
				" replication quorum to %s.",
				NODE_FORMAT_ARGS(primaryNode),
				NODE_FORMAT_ARGS(currentNode),
				currentNode->replicationQuorum ? "true" : "false");

			SetNodeGoalState(primaryNode,
							 REPLICATION_STATE_APPLY_SETTINGS, message);
		}

		/* if primaryNode is not NULL, then current state is APPLY_SETTINGS */
		else if (primaryNode)
		{
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot set replication quorum when current state "
							"for primary " NODE_FORMAT
							" is \"%s\"",
							NODE_FORMAT_ARGS(primaryNode),
							ReplicationStateGetName(primaryNode->reportedState))));
		}

		/* other case is that we failed to find a primary node, proceed */
	}

	PG_RETURN_BOOL(true);
}


/*
 * update_node_metadata allows to update a node's nodename, hostname, and port.
 *
 * The pg_autoctl client fetches the list of "other" nodes on each iteration
 * and will take it from there that they need to update their HBA rules when
 * the hostname has changed.
 */
Datum
update_node_metadata(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int64 nodeid = 0;
	char *nodeName = NULL;
	char *nodeHost = NULL;
	int32 nodePort = 0;


	if (PG_ARGISNULL(0))
	{
		ereport(ERROR,
				(errmsg("udpate_node_metadata requires a non-null nodeid")));
	}
	else
	{
		nodeid = PG_GETARG_INT64(0);
	}

	AutoFailoverNode *currentNode = GetAutoFailoverNodeById(nodeid);

	if (currentNode == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("node %lld is not registered",
							   (long long) nodeid)));
	}

	LockFormation(currentNode->formationId, ShareLock);
	LockNodeGroup(currentNode->formationId, currentNode->groupId, ExclusiveLock);

	/*
	 * When arguments are NULL, replace them with the current value of the node
	 * metadata, so that the UPDATE statement then is a noop on that field.
	 */
	if (PG_ARGISNULL(1))
	{
		nodeName = currentNode->nodeName;
	}
	else
	{
		text *nodeNameText = PG_GETARG_TEXT_P(1);

		nodeName = text_to_cstring(nodeNameText);
	}

	if (PG_ARGISNULL(2))
	{
		nodeHost = currentNode->nodeHost;
	}
	else
	{
		text *nodeHostText = PG_GETARG_TEXT_P(2);

		nodeHost = text_to_cstring(nodeHostText);
	}

	if (PG_ARGISNULL(3))
	{
		nodePort = currentNode->nodePort;
	}
	else
	{
		nodePort = PG_GETARG_INT32(3);
	}

	UpdateAutoFailoverNodeMetadata(currentNode->nodeId,
								   nodeName, nodeHost, nodePort);

	PG_RETURN_BOOL(true);
}


/*
 * synchronous_standby_names returns the synchronous_standby_names parameter
 * value for a given Postgres service group in a given formation.
 */
Datum
synchronous_standby_names(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	int32 groupId = PG_GETARG_INT32(1);

	AutoFailoverFormation *formation = GetFormation(formationId);

	List *nodesGroupList = AutoFailoverNodeGroup(formationId, groupId);
	int nodesCount = list_length(nodesGroupList);

	/*
	 * When there's no nodes registered yet, there's no pg_autoctl process that
	 * needs the information anyway. Return NULL.
	 */
	if (nodesCount == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("no nodes found in group %d of formation \"%s\"",
						groupId, formationId)));
	}

	/* when we have a SINGLE node we disable synchronous replication */
	if (nodesCount == 1)
	{
		PG_RETURN_TEXT_P(cstring_to_text(""));
	}

	/* when we have more than one node, fetch the primary */
	AutoFailoverNode *primaryNode = GetPrimaryNodeInGroup(formationId, groupId);

	List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);

	/*
	 * Single standby case, we assume formation->number_sync_standbys == 0
	 */
	if (nodesCount == 2)
	{
		AutoFailoverNode *secondaryNode = linitial(standbyNodesGroupList);

		if (secondaryNode != NULL &&
			secondaryNode->replicationQuorum &&
			secondaryNode->goalState == REPLICATION_STATE_SECONDARY)
		{
			/* enable synchronous replication */
			StringInfo sbnames = makeStringInfo();

			appendStringInfo(sbnames,
							 "ANY 1 (pgautofailover_standby_%lld)",
							 (long long) secondaryNode->nodeId);

			PG_RETURN_TEXT_P(cstring_to_text(sbnames->data));
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
	 *   - syncStandbyNodesGroupList contains only nodes that participates in
	 *     the replication quorum
	 *
	 *   - then we build synchronous_standby_names with the following model:
	 *
	 *       ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)
	 *
	 *     The num_sync number is the formation number_sync_standbys property.
	 */
	{
		List *syncStandbyNodesGroupList =
			GroupListSyncStandbys(standbyNodesGroupList);

		int count = list_length(syncStandbyNodesGroupList);

		if (count == 0 ||
			IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
		{
			/*
			 *  If no standby participates in the replication Quorum, we
			 * disable synchronous replication.
			 */
			PG_RETURN_TEXT_P(cstring_to_text(""));
		}
		else
		{
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

			appendStringInfo(sbnames, "ANY %d (", number_sync_standbys);

			foreach(nodeCell, syncStandbyNodesGroupList)
			{
				AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

				appendStringInfo(sbnames,
								 "%spgautofailover_standby_%lld",
								 firstNode ? "" : ", ",
								 (long long) node->nodeId);

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
