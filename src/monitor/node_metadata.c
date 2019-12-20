/*-------------------------------------------------------------------------
 *
 * src/monitor/node_metadata.c
 *
 * Implementation of functions related to health check metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"
#include "nodes/pg_list.h"

/* list_qsort is only in Postgres 11 and 12 */
#include "version_compat.h"

#include "health_check.h"
#include "metadata.h"
#include "node_metadata.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "access/xlogdefs.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_type.h"
#include "commands/sequence.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/pg_lsn.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"


/*
 * AllAutoFailoverNodes returns all AutoFailover nodes in a formation as a
 * list.
 */
List *
AllAutoFailoverNodes(char *formationId)
{
	List *nodeList = NIL;
	MemoryContext callerContext = CurrentMemoryContext;
	MemoryContext spiContext = NULL;

	Oid argTypes[] = {
		TEXTOID /* formationid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId)  /* formationid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;
	uint32 rowNumber = 0;

	const char *selectQuery =
			SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE " WHERE formationid = $1";

	SPI_connect();

	spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
									  NULL, false, 0);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	spiContext = MemoryContextSwitchTo(callerContext);

	for (rowNumber = 0; rowNumber < SPI_processed; rowNumber++)
	{
		HeapTuple heapTuple = SPI_tuptable->vals[rowNumber];
		AutoFailoverNode *pgAutoFailoverNode =
			TupleToAutoFailoverNode(SPI_tuptable->tupdesc, heapTuple);

		nodeList = lappend(nodeList, pgAutoFailoverNode);
	}

	MemoryContextSwitchTo(spiContext);

	SPI_finish();

	return nodeList;
}


/*
 * TupleToAutoFailoverNode constructs a AutoFailoverNode from a heap tuple.
 */
AutoFailoverNode *
TupleToAutoFailoverNode(TupleDesc tupleDescriptor, HeapTuple heapTuple)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	bool isNull = false;

	Datum formationId = heap_getattr(heapTuple,
									 Anum_pgautofailover_node_formationid,
									 tupleDescriptor, &isNull);
	Datum nodeId = heap_getattr(heapTuple, Anum_pgautofailover_node_nodeid,
								tupleDescriptor, &isNull);
	Datum groupId = heap_getattr(heapTuple, Anum_pgautofailover_node_groupid,
								 tupleDescriptor, &isNull);
	Datum nodeName = heap_getattr(heapTuple, Anum_pgautofailover_node_nodename,
								  tupleDescriptor, &isNull);
	Datum nodePort = heap_getattr(heapTuple, Anum_pgautofailover_node_nodeport,
								  tupleDescriptor, &isNull);
	Datum goalState = heap_getattr(heapTuple, Anum_pgautofailover_node_goalstate,
								   tupleDescriptor, &isNull);
	Datum reportedState = heap_getattr(heapTuple,
									   Anum_pgautofailover_node_reportedstate,
									   tupleDescriptor, &isNull);
	Datum pgIsRunning = heap_getattr(heapTuple,
									 Anum_pgautofailover_node_reportedpgisrunning,
									 tupleDescriptor, &isNull);
	Datum pgsrSyncState = heap_getattr(heapTuple,
									   Anum_pgautofailover_node_reportedrepstate,
									   tupleDescriptor, &isNull);
	Datum reportTime = heap_getattr(heapTuple, Anum_pgautofailover_node_reporttime,
									tupleDescriptor, &isNull);
	Datum walReportTime = heap_getattr(heapTuple,
										Anum_pgautofailover_node_walreporttime,
										tupleDescriptor, &isNull);
	Datum health = heap_getattr(heapTuple, Anum_pgautofailover_node_health,
								tupleDescriptor, &isNull);
	Datum healthCheckTime = heap_getattr(heapTuple,
										 Anum_pgautofailover_node_healthchecktime,
										 tupleDescriptor, &isNull);
	Datum stateChangeTime = heap_getattr(heapTuple,
										 Anum_pgautofailover_node_statechangetime,
										 tupleDescriptor, &isNull);
	Datum reportedLSN = heap_getattr(heapTuple, Anum_pgautofailover_node_reportedLSN,
								   tupleDescriptor, &isNull);
	Datum candidatePriority = heap_getattr(heapTuple,
										 Anum_pgautofailover_node_candidate_priority,
										 tupleDescriptor, &isNull);
	Datum replicationQuorum = heap_getattr(heapTuple,
										 Anum_pgautofailover_node_replication_quorum,
										 tupleDescriptor, &isNull);

	Oid goalStateOid = DatumGetObjectId(goalState);
	Oid reportedStateOid = DatumGetObjectId(reportedState);

	pgAutoFailoverNode = (AutoFailoverNode *) palloc0(sizeof(AutoFailoverNode));
	pgAutoFailoverNode->formationId = TextDatumGetCString(formationId);
	pgAutoFailoverNode->nodeId = DatumGetInt32(nodeId);
	pgAutoFailoverNode->groupId = DatumGetInt32(groupId);
	pgAutoFailoverNode->nodeName = TextDatumGetCString(nodeName);
	pgAutoFailoverNode->nodePort = DatumGetInt32(nodePort);
	pgAutoFailoverNode->goalState = EnumGetReplicationState(goalStateOid);
	pgAutoFailoverNode->reportedState = EnumGetReplicationState(reportedStateOid);
	pgAutoFailoverNode->pgIsRunning = DatumGetBool(pgIsRunning);
	pgAutoFailoverNode->pgsrSyncState =
		SyncStateFromString(TextDatumGetCString(pgsrSyncState));
	pgAutoFailoverNode->reportTime = DatumGetTimestampTz(reportTime);
	pgAutoFailoverNode->walReportTime = DatumGetTimestampTz(walReportTime);
	pgAutoFailoverNode->health = DatumGetInt32(health);
	pgAutoFailoverNode->healthCheckTime = DatumGetTimestampTz(healthCheckTime);
	pgAutoFailoverNode->stateChangeTime = DatumGetTimestampTz(stateChangeTime);
	pgAutoFailoverNode->reportedLSN = DatumGetLSN(reportedLSN);
	pgAutoFailoverNode->candidatePriority = DatumGetInt32(candidatePriority);
	pgAutoFailoverNode->replicationQuorum = DatumGetBool(replicationQuorum);

	return pgAutoFailoverNode;
}


/*
 * AutoFailoverNodeGroup returns all nodes in the given formation and
 * group as a list.
 */
List *
AutoFailoverNodeGroup(char *formationId, int groupId)
{
	List *nodeList = NIL;
	MemoryContext callerContext = CurrentMemoryContext;
	MemoryContext spiContext = NULL;

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT4OID  /* groupid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationid */
		Int32GetDatum(groupId)            /* groupid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;
	uint32 rowNumber = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE formationid = $1 AND groupid = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
									  NULL, false, 0);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	spiContext = MemoryContextSwitchTo(callerContext);

	for (rowNumber = 0; rowNumber < SPI_processed; rowNumber++)
	{
		HeapTuple heapTuple = SPI_tuptable->vals[rowNumber];
		AutoFailoverNode *pgAutoFailoverNode =
			TupleToAutoFailoverNode(SPI_tuptable->tupdesc, heapTuple);

		nodeList = lappend(nodeList, pgAutoFailoverNode);
	}

	MemoryContextSwitchTo(spiContext);

	SPI_finish();

	return nodeList;
}


/*
 * AutoFailoverOtherNodesList returns a list of all the other nodes in the same
 * formation and group as the given one.
 */
List *
AutoFailoverOtherNodesList(AutoFailoverNode *pgAutoFailoverNode)
{
	ListCell *nodeCell = NULL;
	List *groupNodeList = NIL;
	List *otherNodesList = NIL;

	if (pgAutoFailoverNode == NULL)
	{
		return NIL;
	}

	groupNodeList = AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
										  pgAutoFailoverNode->groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (otherNode != NULL &&
			otherNode->nodeId != pgAutoFailoverNode->nodeId)
		{
			otherNodesList = lappend(otherNodesList, otherNode);
		}
	}

	return otherNodesList;
}


/*
 * AutoFailoverOtherNodesList returns a list of all the other nodes in the same
 * formation and group as the given one.
 */
List *
AutoFailoverOtherNodesListInState(AutoFailoverNode *pgAutoFailoverNode,
								  ReplicationState currentState)
{
	ListCell *nodeCell = NULL;
	List *groupNodeList = NIL;
	List *otherNodesList = NIL;

	if (pgAutoFailoverNode == NULL)
	{
		return NIL;
	}

	groupNodeList = AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
										  pgAutoFailoverNode->groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (otherNode != NULL &&
			otherNode->nodeId != pgAutoFailoverNode->nodeId &&
			otherNode->goalState == currentState)
		{
			otherNodesList = lappend(otherNodesList, otherNode);
		}
	}

	return otherNodesList;
}


/*
 * GetPrimaryNodeInGroup returns the node in the group with a role that only a
 * primary can have.
 */
AutoFailoverNode *
GetPrimaryNodeInGroup(char *formationId, int32 groupId)
{
	AutoFailoverNode *primaryNode = NULL;
	List *groupNodeList = NIL;
	ListCell *nodeCell = NULL;

	groupNodeList = AutoFailoverNodeGroup(formationId, groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (StateBelongsToPrimary(currentNode->reportedState))
		{
			primaryNode = currentNode;
			break;
		}
	}

	return primaryNode;
}


/*
 * FindFailoverNewStandbyNode returns the first node found in given list that
 * is a new standby, so that we can process each standby one after the other.
 */
AutoFailoverNode *
FindFailoverNewStandbyNode(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	AutoFailoverNode *standbyNode = NULL;

	/* find the standby for errdetail */
	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (IsCurrentState(otherNode, REPLICATION_STATE_WAIT_STANDBY) ||
			IsCurrentState(otherNode, REPLICATION_STATE_CATCHINGUP))
		{
			standbyNode = otherNode;
		}
	}

	return standbyNode;
}


/*
 * FindMostAdvancedStandby returns the node in groupNodeList that has the most
 * advanced LSN.
 */
AutoFailoverNode *
FindMostAdvancedStandby(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	AutoFailoverNode *mostAdvancedNode = NULL;

	/* find the standby for errdetail */
	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (mostAdvancedNode == NULL ||
			mostAdvancedNode->reportedLSN < node->reportedLSN)
		{
			mostAdvancedNode = node;
		}
	}

	return mostAdvancedNode;
}


/*
 * pgautofailover_node_candidate_priority_compare
 *	  qsort comparator for sorting node lists by candidate priority
 */
static int
pgautofailover_node_candidate_priority_compare(const void *a, const void *b)
{
	AutoFailoverNode *node1 = (AutoFailoverNode *) lfirst(*(ListCell **) a);
	AutoFailoverNode *node2 = (AutoFailoverNode *) lfirst(*(ListCell **) b);

	if (node1->candidatePriority > node2->candidatePriority)
	{
		return -1;
	}

	if (node1->candidatePriority < node2->candidatePriority)
	{
		return 1;
	}

	return 0;
}


/*
 * GroupListCandidates returns a list of nodes in groupNodeList that are all
 * candidates for failover (those with AutoFailoverNode.candidatePriority > 0),
 * sorted by candidatePriority.
 */
List *
GroupListCandidates(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	List *candidateNodesList = NIL;
	List *sortedNodeList =
		list_qsort(groupNodeList,
				   pgautofailover_node_candidate_priority_compare);

	foreach(nodeCell, sortedNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->candidatePriority > 0)
		{
			candidateNodesList = lappend(candidateNodesList, node);
		}
	}
	list_free(sortedNodeList);

	return candidateNodesList;
}


/*
 * GroupListSyncStandbys returns a list of nodes in groupNodeList that are all
 * candidates for failover (those with AutoFailoverNode.replicationQuorum set
 * to true), sorted by candidatePriority.
 */
List *
GroupListSyncStandbys(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	List *syncStandbyNodesList = NIL;
	List *sortedNodeList =
		list_qsort(groupNodeList,
				   pgautofailover_node_candidate_priority_compare);

	foreach(nodeCell, sortedNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->replicationQuorum)
		{
			syncStandbyNodesList = lappend(syncStandbyNodesList, node);
		}
	}
	list_free(sortedNodeList);

	return syncStandbyNodesList;
}


/*
 * AllNodesHaveSameCandidatePriority returns true when all the nodes in the
 * given list have the same candidate priority.
 */
bool
AllNodesHaveSameCandidatePriority(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	int candidatePriority =
		((AutoFailoverNode *)linitial(groupNodeList))->candidatePriority;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->candidatePriority != candidatePriority )
		{
			return false;
		}
	}
	return true;
}


/*
 * GetAutoFailoverNode returns a single AutoFailover node by hostname and port.
 */
AutoFailoverNode *
GetAutoFailoverNode(char *nodeName, int nodePort)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID, /* nodename */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		CStringGetTextDatum(nodeName), /* nodename */
		Int32GetDatum(nodePort)        /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE nodename = $1 AND nodeport = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
									  NULL, false, 1);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	if (SPI_processed > 0)
	{
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);
		pgAutoFailoverNode = TupleToAutoFailoverNode(SPI_tuptable->tupdesc,
													 SPI_tuptable->vals[0]);
		MemoryContextSwitchTo(spiContext);
	}
	else
	{
		pgAutoFailoverNode = NULL;
	}

	SPI_finish();

	return pgAutoFailoverNode;
}


/*
 * GetAutoFailoverNodeWithId returns a single AutoFailover
 * identified by node id, node name and node port.
 */
AutoFailoverNode *
GetAutoFailoverNodeWithId(int nodeid, char *nodeName, int nodePort)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		INT4OID, /* nodeport */
		TEXTOID, /* nodename */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		Int32GetDatum(nodeid),         /* nodeid */
		CStringGetTextDatum(nodeName), /* nodename */
		Int32GetDatum(nodePort)        /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE nodeid = $1 and nodename = $2 AND nodeport = $3";

	SPI_connect();

	spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
									  NULL, false, 1);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	if (SPI_processed > 0)
	{
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);
		pgAutoFailoverNode = TupleToAutoFailoverNode(SPI_tuptable->tupdesc,
													 SPI_tuptable->vals[0]);
		MemoryContextSwitchTo(spiContext);
	}
	else
	{
		pgAutoFailoverNode = NULL;
	}

	SPI_finish();

	return pgAutoFailoverNode;
}


/*
 * OtherNodeInGroup returns the other node in a primary-secondary group, or
 * NULL if the group consists of 1 node.
 */
AutoFailoverNode *
OtherNodeInGroup(AutoFailoverNode *pgAutoFailoverNode)
{
	ListCell *nodeCell = NULL;
	List *groupNodeList =
		AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
							  pgAutoFailoverNode->groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (otherNode->nodeId != pgAutoFailoverNode->nodeId)
		{
			return otherNode;
		}
	}

	return NULL;
}


/*
 * GetWritableNode returns the writable node in the specified group, if any.
 */
AutoFailoverNode *
GetWritableNodeInGroup(char *formationId, int32 groupId)
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
 * AddAutoFailoverNode adds a new AutoFailoverNode to pgautofailover.node with
 * the given properties.
 *
 * We use simple_heap_update instead of SPI to avoid recursing into triggers.
 */
int
AddAutoFailoverNode(char *formationId, int groupId, char *nodeName, int nodePort,
					ReplicationState goalState,
					ReplicationState reportedState,
					int candidatePriority,
					bool replicationQuorum)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid reportedStateOid = ReplicationStateGetEnum(reportedState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT4OID, /* groupid */
		TEXTOID, /* nodename */
		INT4OID, /* nodeport */
		replicationStateTypeOid, /* goalstate */
		replicationStateTypeOid, /* reportedstate */
		INT4OID, /* candidate_priority */
		BOOLOID  /* replication_quorum */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),   /* formationid */
		Int32GetDatum(groupId),			    /* groupid */
		CStringGetTextDatum(nodeName),      /* nodename */
		Int32GetDatum(nodePort),		    /* nodeport */
		ObjectIdGetDatum(goalStateOid),	    /* goalstate */
		ObjectIdGetDatum(reportedStateOid), /* reportedstate */
		Int32GetDatum(candidatePriority),   /* candidate_priority */
		BoolGetDatum(replicationQuorum)	/* replication_quorum */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;
	int nodeId = 0;

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_NODE_TABLE
		" (formationid, groupid, nodename, nodeport, goalstate, reportedstate, candidatepriority, replicationquorum)"
		" VALUES ($1, $2, $3, $4, $5, $6, $7, $8) RETURNING nodeid";

	SPI_connect();

	spiStatus = SPI_execute_with_args(insertQuery, argCount, argTypes,
									  argValues, NULL, false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;
		Datum nodeIdDatum = 0;

		nodeIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc,
									1,
									&isNull);

		nodeId = DatumGetInt32(nodeIdDatum);
	}
	else
	{
		elog(ERROR, "could not insert into " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();

	return nodeId;
}


/*
 * SetNodeGoalState updates only the goal state of a node.
 */
void
SetNodeGoalState(char *nodeName, int nodePort, ReplicationState goalState)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* goalstate */
		TEXTOID, /* nodename */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(goalStateOid),       /* goalstate */
		CStringGetTextDatum(nodeName),        /* nodename */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET goalstate = $1, statechangetime = now() "
		"WHERE nodename = $2 AND nodeport = $3";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);
	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}


/*
 * ReportAutoFailoverNodeState persists the reported state and nodes version of
 * a node.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
ReportAutoFailoverNodeState(char *nodeName, int nodePort,
							ReplicationState reportedState,
							bool pgIsRunning, SyncState pgSyncState,
							XLogRecPtr reportedLSN)
{
	Oid reportedStateOid = ReplicationStateGetEnum(reportedState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* reportedstate */
		BOOLOID,				 /* pg_ctl status: is running */
		TEXTOID,				 /* pg_stat_replication.sync_state */
		LSNOID,				 	 /* reportedlsn */
		TEXTOID,				 /* nodename */
		INT4OID					 /* nodeport */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(reportedStateOid),   /* reportedstate */
		BoolGetDatum(pgIsRunning),			  /* pg_ctl status: is running */
		CStringGetTextDatum(SyncStateToString(pgSyncState)), /* sync_state */
		LSNGetDatum(reportedLSN),			  /* reportedlsn */
		CStringGetTextDatum(nodeName),        /* nodename */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET reportedstate = $1, reporttime = now(), "
		"reportedpgisrunning = $2, reportedrepstate = $3, "
		"reportedlsn = CASE $4 WHEN '0/0'::pg_lsn THEN reportedlsn ELSE $4 END, "
		"walreporttime = CASE $4 WHEN '0/0'::pg_lsn THEN walreporttime ELSE now() END, "
		"statechangetime = now() WHERE nodename = $5 AND nodeport = $6";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);

	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}


/*
 * ReportAutoFailoverNodeHealth persists the current health of a node.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
ReportAutoFailoverNodeHealth(char *nodeName, int nodePort,
							 ReplicationState goalState,
							 NodeHealthState health)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* goalstate */
		INT4OID, /* health */
		TEXTOID, /* nodename */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(goalStateOid), /* goalstate */
		Int32GetDatum(health),          /* reportedversion */
		CStringGetTextDatum(nodeName),  /* nodename */
		Int32GetDatum(nodePort)         /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET goalstate = $1, health = $2, "
		"healthchecktime = now(), statechangetime = now() "
		"WHERE nodename = $3 AND nodeport = $4";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);

	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}

/*
 * ReportAutoFailoverNodeReplicationSetting persists the replication properties of
 * a node.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
ReportAutoFailoverNodeReplicationSetting(int nodeid, char *nodeName, int nodePort,
									 	 int candidatePriority,
										 bool replicationQuorum)
{
	Oid argTypes[] = {
		INT4OID,				 /* candidate_priority */
		BOOLOID,				 /* repliation_quorum */
		INT4OID,				 /* nodeid */
		TEXTOID,				 /* nodename */
		INT4OID					 /* nodeport */
	};

	Datum argValues[] = {
		Int32GetDatum(candidatePriority),	  /* candidate_priority */
		BoolGetDatum(replicationQuorum),	  /* replication_quorum */
		Int32GetDatum(nodeid),				  /* nodeid */
		CStringGetTextDatum(nodeName),        /* nodename */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE " SET "
		"candidatepriority = $1, replicationquorum = $2 "
		"WHERE nodeid = $3 and nodename = $4 AND nodeport = $5";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);

	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}

/*
 * RemoveAutoFailoverNode removes a node from a AutoFailover formation.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
RemoveAutoFailoverNode(char *nodeName, int nodePort)
{
	Oid argTypes[] = {
		TEXTOID, /* nodename */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		CStringGetTextDatum(nodeName), /* nodename */
		Int32GetDatum(nodePort)        /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *deleteQuery =
		"DELETE FROM " AUTO_FAILOVER_NODE_TABLE
		" WHERE nodename = $1 AND nodeport = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(deleteQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);

	if (spiStatus != SPI_OK_DELETE)
	{
		elog(ERROR, "could not delete from " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}


/*
 * SynStateFromString returns the enum value represented by given string.
 */
SyncState
SyncStateFromString(const char *pgsrSyncState)
{
	SyncState syncStateArray[] = { SYNC_STATE_UNKNOWN,
								   SYNC_STATE_UNKNOWN,
								   SYNC_STATE_SYNC,
								   SYNC_STATE_ASYNC,
								   SYNC_STATE_QUORUM,
								   SYNC_STATE_POTENTIAL };
	char *syncStateList[] = {"", "unknown",
							 "sync", "async", "quorum", "potential",
							 NULL};

	for(int listIndex = 0; syncStateList[listIndex] != NULL; listIndex++)
	{
		char *candidate = syncStateList[listIndex];

		if (strcmp(pgsrSyncState, candidate) == 0)
		{
			return syncStateArray[listIndex];
		}
	}

	ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
					errmsg("unknown pg_stat_replication.sync_state \"%s\"",
						   pgsrSyncState)));

	/* never happens, make compiler happy */
	return SYNC_STATE_UNKNOWN;
}


/*
 * SyncStateToString returns the string representation of a SyncState
 */
char *
SyncStateToString(SyncState pgsrSyncState)
{
	switch (pgsrSyncState)
	{
		case SYNC_STATE_UNKNOWN:
			return "unknown";

		case SYNC_STATE_ASYNC:
			return "async";

		case SYNC_STATE_SYNC:
			return "sync";

		case SYNC_STATE_QUORUM:
			return "quorum";

		case SYNC_STATE_POTENTIAL:
			return "potential";

		default:
			ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
							errmsg("unknown SyncState enum value %d",
								   pgsrSyncState)));
	}

	/* keep compiler happy */
	return "";
}


/*
 * IsCurrentState returns true if the given node is known to have converged to
 * the given state and false otherwise.
 */
bool
IsCurrentState(AutoFailoverNode *pgAutoFailoverNode, ReplicationState state)
{
	return pgAutoFailoverNode != NULL
		&& pgAutoFailoverNode->goalState == pgAutoFailoverNode->reportedState
		&& pgAutoFailoverNode->goalState == state;
}


/*
 * CanTakeWritesInState returns whether a node can take writes when in
 * the given state.
 */
bool
CanTakeWritesInState(ReplicationState state)
{
	return state == REPLICATION_STATE_SINGLE
		|| state == REPLICATION_STATE_PRIMARY
		|| state == REPLICATION_STATE_WAIT_PRIMARY
		|| state == REPLICATION_STATE_JOIN_PRIMARY
		|| state == REPLICATION_STATE_APPLY_SETTINGS;
}


/*
 * StateBelongsToPrimary returns true when given state belongs to a primary
 * node, either in a healthy state or even when in the middle of being demoted.
 */
bool
StateBelongsToPrimary(ReplicationState state)
{
	return CanTakeWritesInState(state)
		|| state == REPLICATION_STATE_DRAINING
		|| state == REPLICATION_STATE_DEMOTED
		|| state == REPLICATION_STATE_DEMOTE_TIMEOUT;
}


/*
 * IsBeingPromoted returns whether a standby node is going through the process
 * of a promotion.
 */
bool
IsBeingPromoted(AutoFailoverNode *node)
{
	return node != NULL
		&& (node->reportedState == REPLICATION_STATE_WAIT_FORWARD
			|| node->goalState == REPLICATION_STATE_WAIT_FORWARD

			|| node->reportedState == REPLICATION_STATE_FAST_FORWARD
			|| node->goalState == REPLICATION_STATE_FAST_FORWARD

			|| node->reportedState == REPLICATION_STATE_PREPARE_PROMOTION
			|| node->goalState == REPLICATION_STATE_PREPARE_PROMOTION

			|| node->reportedState == REPLICATION_STATE_STOP_REPLICATION
			|| node->goalState == REPLICATION_STATE_STOP_REPLICATION

			|| node->reportedState == REPLICATION_STATE_WAIT_PRIMARY
			|| node->goalState == REPLICATION_STATE_WAIT_PRIMARY);
}


/*
 * IsInWaitOrJoinState returns true when the given node is a primary node that
 * is currently busy with registering a standby: it's then been assigned either
 * WAIT_STANDBY or JOIN_STANDBY replication state.
 */
bool
IsInWaitOrJoinState(AutoFailoverNode *node)
{
	return node != NULL
		&& (node->reportedState == REPLICATION_STATE_WAIT_PRIMARY
			|| node->goalState == REPLICATION_STATE_WAIT_PRIMARY
			|| node->reportedState == REPLICATION_STATE_JOIN_PRIMARY
			|| node->goalState == REPLICATION_STATE_JOIN_PRIMARY);
}


/*
 * IsInPrimaryState returns true if the given node is known to have converged
 * to a state that makes it the primary node in its group.
 */
bool
IsInPrimaryState(AutoFailoverNode *pgAutoFailoverNode)
{
	return pgAutoFailoverNode != NULL
		&& pgAutoFailoverNode->goalState == pgAutoFailoverNode->reportedState
		&& CanTakeWritesInState(pgAutoFailoverNode->goalState);
}
