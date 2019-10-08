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

#include "miscadmin.h"
#include "fmgr.h"

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
		"SELECT * FROM " AUTO_FAILOVER_NODE_TABLE " WHERE formationid = $1";

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
	Datum reportedLSN = heap_getattr(heapTuple, Anum_pgautofailover_node_reportedLSN,
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
	pgAutoFailoverNode->reportedLSN = DatumGetLSN(reportedLSN);
	pgAutoFailoverNode->walReportTime = DatumGetTimestampTz(walReportTime);
	pgAutoFailoverNode->health = DatumGetInt32(health);
	pgAutoFailoverNode->healthCheckTime = DatumGetTimestampTz(healthCheckTime);
	pgAutoFailoverNode->stateChangeTime = DatumGetTimestampTz(stateChangeTime);

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
		"SELECT * FROM " AUTO_FAILOVER_NODE_TABLE
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
		"SELECT * FROM " AUTO_FAILOVER_NODE_TABLE
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
 * AddAutoFailoverNode adds a new AutoFailoverNode to pgautofailover.node with
 * the given properties.
 *
 * We use simple_heap_update instead of SPI to avoid recursing into triggers.
 */
int
AddAutoFailoverNode(char *formationId, int groupId, char *nodeName, int nodePort,
					ReplicationState goalState,
					ReplicationState reportedState)
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
		replicationStateTypeOid	 /* reportedstate */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),  /* formationid */
		Int32GetDatum(groupId),			   /* groupid */
		CStringGetTextDatum(nodeName),     /* nodename */
		Int32GetDatum(nodePort),		   /* nodeport */
		ObjectIdGetDatum(goalStateOid),	   /* goalstate */
		ObjectIdGetDatum(reportedStateOid) /* reportedstate */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;
	int nodeId = 0;

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_NODE_TABLE
		" (formationid, groupid, nodename, nodeport, goalstate, reportedstate)"
		" VALUES ($1, $2, $3, $4, $5, $6) RETURNING nodeid";

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
