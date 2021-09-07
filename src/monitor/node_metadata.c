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
#include "notifications.h"

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


/* GUC variables */
int DrainTimeoutMs = 30 * 1000;
int UnhealthyTimeoutMs = 20 * 1000;
int StartupGracePeriodMs = 10 * 1000;


/*
 * AllAutoFailoverNodes returns all AutoFailover nodes in a formation as a
 * list.
 */
List *
AllAutoFailoverNodes(char *formationId)
{
	List *nodeList = NIL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID /* formationid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId)  /* formationid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	uint64 rowNumber = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE formationid = $1 ";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

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
	bool isNull = false;
	bool sysIdentifierIsNull = false;

	Datum formationId = heap_getattr(heapTuple,
									 Anum_pgautofailover_node_formationid,
									 tupleDescriptor, &isNull);
	Datum nodeId = heap_getattr(heapTuple, Anum_pgautofailover_node_nodeid,
								tupleDescriptor, &isNull);
	Datum groupId = heap_getattr(heapTuple, Anum_pgautofailover_node_groupid,
								 tupleDescriptor, &isNull);
	Datum nodeName = heap_getattr(heapTuple, Anum_pgautofailover_node_nodename,
								  tupleDescriptor, &isNull);
	Datum nodeHost = heap_getattr(heapTuple, Anum_pgautofailover_node_nodehost,
								  tupleDescriptor, &isNull);
	Datum nodePort = heap_getattr(heapTuple, Anum_pgautofailover_node_nodeport,
								  tupleDescriptor, &isNull);
	Datum sysIdentifier = heap_getattr(heapTuple,
									   Anum_pgautofailover_node_sysidentifier,
									   tupleDescriptor, &sysIdentifierIsNull);
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
	Datum reportedTLI = heap_getattr(heapTuple, Anum_pgautofailover_node_reportedTLI,
									 tupleDescriptor, &isNull);
	Datum reportedLSN = heap_getattr(heapTuple, Anum_pgautofailover_node_reportedLSN,
									 tupleDescriptor, &isNull);
	Datum candidatePriority = heap_getattr(heapTuple,
										   Anum_pgautofailover_node_candidate_priority,
										   tupleDescriptor, &isNull);
	Datum replicationQuorum = heap_getattr(heapTuple,
										   Anum_pgautofailover_node_replication_quorum,
										   tupleDescriptor, &isNull);
	Datum nodeCluster = heap_getattr(heapTuple,
									 Anum_pgautofailover_node_nodecluster,
									 tupleDescriptor, &isNull);

	Oid goalStateOid = DatumGetObjectId(goalState);
	Oid reportedStateOid = DatumGetObjectId(reportedState);

	AutoFailoverNode *pgAutoFailoverNode = (AutoFailoverNode *) palloc0(
		sizeof(AutoFailoverNode));
	pgAutoFailoverNode->formationId = TextDatumGetCString(formationId);
	pgAutoFailoverNode->nodeId = DatumGetInt64(nodeId);
	pgAutoFailoverNode->groupId = DatumGetInt32(groupId);
	pgAutoFailoverNode->nodeName = TextDatumGetCString(nodeName);
	pgAutoFailoverNode->nodeHost = TextDatumGetCString(nodeHost);
	pgAutoFailoverNode->nodePort = DatumGetInt32(nodePort);

	pgAutoFailoverNode->sysIdentifier =
		sysIdentifierIsNull ? 0 : DatumGetInt64(sysIdentifier);

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
	pgAutoFailoverNode->reportedTLI = DatumGetInt32(reportedTLI);
	pgAutoFailoverNode->reportedLSN = DatumGetLSN(reportedLSN);
	pgAutoFailoverNode->candidatePriority = DatumGetInt32(candidatePriority);
	pgAutoFailoverNode->replicationQuorum = DatumGetBool(replicationQuorum);
	pgAutoFailoverNode->nodeCluster = TextDatumGetCString(nodeCluster);

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

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT4OID  /* groupid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationid */
		Int32GetDatum(groupId)            /* groupid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	uint64 rowNumber = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		"    WHERE formationid = $1 AND groupid = $2"
		"      AND goalstate <> 'dropped'"
		" ORDER BY nodeid";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

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
 * AutoFailoverAllNodesInGroup returns all nodes in the given formation and
 * group as a list, and includes nodes that are currently being dropped.
 */
List *
AutoFailoverAllNodesInGroup(char *formationId, int groupId)
{
	List *nodeList = NIL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT4OID  /* groupid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationid */
		Int32GetDatum(groupId)            /* groupid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	uint64 rowNumber = 0;

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		"    WHERE formationid = $1 AND groupid = $2"
		" ORDER BY nodeid";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_NODE_TABLE);
	}

	MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

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
	List *otherNodesList = NIL;

	if (pgAutoFailoverNode == NULL)
	{
		return NIL;
	}

	List *groupNodeList = AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
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
	List *otherNodesList = NIL;

	if (pgAutoFailoverNode == NULL)
	{
		return NIL;
	}

	List *groupNodeList = AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
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
 * AutoFailoverCandidateNodesList returns a list of all the other nodes in the
 * same formation and group as the given one, with candidate priority > 0.
 */
List *
AutoFailoverCandidateNodesListInState(AutoFailoverNode *pgAutoFailoverNode,
									  ReplicationState currentState)
{
	ListCell *nodeCell = NULL;
	List *otherNodesList = NIL;

	if (pgAutoFailoverNode == NULL)
	{
		return NIL;
	}

	List *groupNodeList = AutoFailoverNodeGroup(pgAutoFailoverNode->formationId,
												pgAutoFailoverNode->groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *otherNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (otherNode != NULL &&
			otherNode->nodeId != pgAutoFailoverNode->nodeId &&
			otherNode->candidatePriority > 0 &&
			otherNode->goalState == currentState)
		{
			otherNodesList = lappend(otherNodesList, otherNode);
		}
	}

	return otherNodesList;
}


/*
 * GetPrimaryNodeInGroup returns the writable node in the specified group, if
 * any.
 */
AutoFailoverNode *
GetPrimaryNodeInGroup(char *formationId, int32 groupId)
{
	AutoFailoverNode *writableNode = NULL;
	ListCell *nodeCell = NULL;

	List *groupNodeList = AutoFailoverNodeGroup(formationId, groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (CanTakeWritesInState(currentNode->goalState))
		{
			writableNode = currentNode;
			break;
		}
	}

	return writableNode;
}


/*
 * GetPrimaryNodeInGroup returns the writable node in the specified group, if
 * any.
 */
AutoFailoverNode *
GetNodeToFailoverFromInGroup(char *formationId, int32 groupId)
{
	AutoFailoverNode *failoverNode = NULL;
	ListCell *nodeCell = NULL;

	List *groupNodeList = AutoFailoverNodeGroup(formationId, groupId);

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (CanInitiateFailover(currentNode->goalState) &&
			currentNode->reportedState == currentNode->goalState)
		{
			failoverNode = currentNode;
			break;
		}
	}

	return failoverNode;
}


/*
 * GetPrimaryOrDemotedNodeInGroup returns the node in the group with a role
 * that only a primary can have.
 *
 * When handling multiple standbys, it could be that the primary node gets
 * demoted, triggering a failover with the other standby node(s). Then the
 * demoted node connects back to the monitor, and should be processed as a
 * standby that re-joins the group, not as a primary being demoted.
 */
AutoFailoverNode *
GetPrimaryOrDemotedNodeInGroup(char *formationId, int32 groupId)
{
	AutoFailoverNode *primaryNode = NULL;
	ListCell *nodeCell = NULL;

	List *groupNodeList = AutoFailoverNodeGroup(formationId, groupId);

	/* first find a node that is writable */
	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (CanTakeWritesInState(currentNode->goalState))
		{
			primaryNode = currentNode;
			break;
		}
	}

	/* if we found a writable node, we're done */
	if (primaryNode != NULL)
	{
		return primaryNode;
	}

	/*
	 * Maybe we have a primary that is draining or has been demoted?
	 * In case there are more than one of those, choose the one that is
	 * currently being demoted.
	 */
	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *currentNode = (AutoFailoverNode *) lfirst(nodeCell);

		if (StateBelongsToPrimary(currentNode->reportedState) &&
			(!IsBeingDemotedPrimary(primaryNode) ||
			 !IsDemotedPrimary(currentNode)))
		{
			primaryNode = currentNode;
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
 * FindCandidateNodeBeingPromoted scans through the given groupNodeList and
 * returns the first node found that IsBeingPromoted().
 */
bool
IsFailoverInProgress(List *groupNodeList)
{
	ListCell *nodeCell = NULL;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node == NULL)
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: node is NULL")));
		}

		/*
		 * A single node participating in a promotion allows to answer already.
		 */
		if (IsParticipatingInPromotion(node))
		{
			return true;
		}

		/* no conclusions to be drawn from nodes in maintenance */
		if (IsInMaintenance(node))
		{
			continue;
		}
	}

	/*
	 * If no node is participating in a promotion, then no failover is in
	 * progress.
	 */
	return false;
}


/*
 * FindCandidateNodeBeingPromoted scans through the given groupNodeList and
 * returns the first node found that IsBeingPromoted().
 */
AutoFailoverNode *
FindCandidateNodeBeingPromoted(List *groupNodeList)
{
	ListCell *nodeCell = NULL;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node == NULL)
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: node is NULL")));
		}

		/* we might have a failover ongoing already */
		if (IsBeingPromoted(node))
		{
			return node;
		}
	}

	return NULL;
}


/*
 * pgautofailover_node_candidate_priority_compare
 *	  qsort comparator for sorting node lists by candidate priority
 */
#if (PG_VERSION_NUM >= 130000)
static int
pgautofailover_node_candidate_priority_compare(const union ListCell *a,
											   const union ListCell *b)
{
	AutoFailoverNode *node1 = (AutoFailoverNode *) lfirst(a);
	AutoFailoverNode *node2 = (AutoFailoverNode *) lfirst(b);
#else
static int
pgautofailover_node_candidate_priority_compare(const void *a, const void *b)
{
	AutoFailoverNode *node1 = (AutoFailoverNode *) lfirst(*(ListCell **) a);
	AutoFailoverNode *node2 = (AutoFailoverNode *) lfirst(*(ListCell **) b);
#endif

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

	List *sortedNodeList = list_copy(groupNodeList);

	#if (PG_VERSION_NUM >= 130000)
	list_sort(sortedNodeList, pgautofailover_node_candidate_priority_compare);
	#else
	sortedNodeList =
		list_qsort(sortedNodeList,
				   pgautofailover_node_candidate_priority_compare);
	#endif

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
 * pgautofailover_node_reportedlsn_compare
 *	  qsort comparator for sorting node lists by reported lsn, descending
 */
#if (PG_VERSION_NUM >= 130000)
static int
pgautofailover_node_reportedlsn_compare(const union ListCell *a,
										const union ListCell *b)
{
	AutoFailoverNode *node1 = (AutoFailoverNode *) lfirst(a);
	AutoFailoverNode *node2 = (AutoFailoverNode *) lfirst(b);
#else
static int
pgautofailover_node_reportedlsn_compare(const void *a, const void *b)
{
	AutoFailoverNode *node1 = (AutoFailoverNode *) lfirst(*(ListCell **) a);
	AutoFailoverNode *node2 = (AutoFailoverNode *) lfirst(*(ListCell **) b);
#endif

	if (node1->reportedTLI > node2->reportedTLI ||
		(node1->reportedTLI == node2->reportedTLI &&
		 node1->reportedLSN > node2->reportedLSN))
	{
		return -1;
	}

	if (node1->reportedTLI < node2->reportedTLI ||
		(node1->reportedTLI == node2->reportedTLI &&
		 node1->reportedLSN < node2->reportedLSN))
	{
		return 1;
	}

	return 0;
}


/*
 * ListMostAdvancedStandbyNodes returns the nodes in groupNodeList that have
 * the most advanced LSN.
 */
List *
ListMostAdvancedStandbyNodes(List *groupNodeList)
{
	ListCell *nodeCell = NULL;
	List *mostAdvancedNodeList = NIL;
	XLogRecPtr mostAdvancedLSN = 0;

	#if (PG_VERSION_NUM >= 130000)
	List *sortedNodeList = list_copy(groupNodeList);
	list_sort(sortedNodeList, pgautofailover_node_reportedlsn_compare);
	#else
	List *sortedNodeList =
		list_qsort(groupNodeList,
				   pgautofailover_node_reportedlsn_compare);
	#endif

	foreach(nodeCell, sortedNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		/* skip old primary */
		if (StateBelongsToPrimary(node->reportedState))
		{
			continue;
		}

		if (mostAdvancedLSN == 0)
		{
			mostAdvancedLSN = node->reportedLSN;
		}

		if (node->reportedLSN == mostAdvancedLSN)
		{
			mostAdvancedNodeList = lappend(mostAdvancedNodeList, node);
		}
	}

	return mostAdvancedNodeList;
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

	if (groupNodeList == NIL)
	{
		return NIL;
	}

	#if (PG_VERSION_NUM >= 130000)
	List *sortedNodeList = list_copy(groupNodeList);
	list_sort(sortedNodeList, pgautofailover_node_candidate_priority_compare);
	#else
	List *sortedNodeList =
		list_qsort(groupNodeList,
				   pgautofailover_node_candidate_priority_compare);
	#endif

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
 * CountSyncStandbys returns how many standby nodes have their
 * replicationQuorum property set to true in the given groupNodeList.
 */
int
CountSyncStandbys(List *groupNodeList)
{
	int count = 0;
	ListCell *nodeCell = NULL;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->replicationQuorum)
		{
			++count;
		}
	}

	return count;
}


/*
 * IsHealthySyncStandby returns true if the node its replicationQuorum property
 * set to true in the given groupNodeList, but only if only if that node is
 * currently currently in REPLICATION_STATE_SECONDARY and known healthy.
 */
bool
IsHealthySyncStandby(AutoFailoverNode *node)
{
	return node->replicationQuorum &&
		   IsCurrentState(node, REPLICATION_STATE_SECONDARY) &&
		   IsHealthy(node);
}


/*
 * CountHealthySyncStandbys returns how many standby nodes have their
 * replicationQuorum property set to true in the given groupNodeList, counting
 * only nodes that are currently in REPLICATION_STATE_SECONDARY and known
 * healthy.
 */
int
CountHealthySyncStandbys(List *groupNodeList)
{
	int count = 0;
	ListCell *nodeCell = NULL;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (IsHealthySyncStandby(node))
		{
			++count;
		}
	}

	return count;
}


/*
 * CountHealthyCandidates returns how many standby nodes have their
 * candidatePriority > 0 in the given groupNodeList, counting only nodes that
 * are currently in REPLICATION_STATE_SECONDARY and known healthy.
 */
int
CountHealthyCandidates(List *groupNodeList)
{
	int count = 0;
	ListCell *nodeCell = NULL;

	foreach(nodeCell, groupNodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->candidatePriority > 0 &&
			IsCurrentState(node, REPLICATION_STATE_SECONDARY) &&
			IsHealthy(node))
		{
			++count;
		}
	}

	return count;
}


/*
 * GetAutoFailoverNode returns a single AutoFailover node by hostname and port.
 */
AutoFailoverNode *
GetAutoFailoverNode(char *nodeHost, int nodePort)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID, /* nodehost */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		CStringGetTextDatum(nodeHost), /* nodehost */
		Int32GetDatum(nodePort)        /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE nodehost = $1 AND nodeport = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
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
 *
 * This function returns NULL, when the node could not be found.
 */
AutoFailoverNode *
GetAutoFailoverNodeById(int64 nodeId)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		INT8OID  /* nodeId */
	};

	Datum argValues[] = {
		Int64GetDatum(nodeId)           /* nodeId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE nodeid = $1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery,
										  argCount, argTypes, argValues,
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
GetAutoFailoverNodeByName(char *formationId, char *nodeName)
{
	AutoFailoverNode *pgAutoFailoverNode = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID,                    /* formationId */
		TEXTOID                     /* nodename */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationId */
		CStringGetTextDatum(nodeName)     /* nodename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE
		" WHERE formationid = $1 and nodename = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
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
 * AddAutoFailoverNode adds a new AutoFailoverNode to pgautofailover.node with
 * the given properties.
 *
 * We use simple_heap_update instead of SPI to avoid recursing into triggers.
 */
int
AddAutoFailoverNode(char *formationId,
					FormationKind formationKind,
					int64 nodeId,
					int groupId,
					char *nodeName,
					char *nodeHost,
					int nodePort,
					uint64 sysIdentifier,
					ReplicationState goalState,
					ReplicationState reportedState,
					int candidatePriority,
					bool replicationQuorum,
					char *nodeCluster)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid reportedStateOid = ReplicationStateGetEnum(reportedState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	const char *prefix =
		formationKind == FORMATION_KIND_CITUS
		? (groupId == 0 ? "coordinator" : "worker")
		: "node";

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT8OID, /* nodeid */
		INT4OID, /* groupid */
		TEXTOID, /* nodename */
		TEXTOID, /* nodehost */
		INT4OID, /* nodeport */
		INT8OID, /* sysidentifier */
		replicationStateTypeOid, /* goalstate */
		replicationStateTypeOid, /* reportedstate */
		INT4OID, /* candidate_priority */
		BOOLOID,  /* replication_quorum */
		TEXTOID,  /* node name prefix */
		TEXTOID   /* nodecluster */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),   /* formationid */
		Int64GetDatum(nodeId),              /* nodeid */
		Int32GetDatum(groupId),             /* groupid */
		nodeName == NULL ? (Datum) 0 : CStringGetTextDatum(nodeName),   /* nodename */
		CStringGetTextDatum(nodeHost),      /* nodehost */
		Int32GetDatum(nodePort),            /* nodeport */
		Int64GetDatum(sysIdentifier),       /* sysidentifier */
		ObjectIdGetDatum(goalStateOid),     /* goalstate */
		ObjectIdGetDatum(reportedStateOid), /* reportedstate */
		Int32GetDatum(candidatePriority),   /* candidate_priority */
		BoolGetDatum(replicationQuorum),    /* replication_quorum */
		CStringGetTextDatum(prefix),        /* prefix */
		CStringGetTextDatum(nodeCluster)    /* nodecluster */
	};

	/*
	 * Rather than turning the register_node function as non STRICT, we accept
	 * the default system identifier to be zero and then insert NULL here
	 * instead.
	 *
	 * The alternative would imply testing the 10 args of the function against
	 * the possibility of them being NULL. Also, on the client side, when
	 * PGDATA does not exist our pg_control_data.system_identifier internal
	 * structure is intialized with a zero value.
	 */
	const char argNulls[] = {
		' ',                            /* formationid */
		' ',                            /* nodeid */
		' ',                            /* groupid */
		nodeName == NULL ? 'n' : ' ',   /* nodename */
		' ',                            /* nodehost */
		' ',                            /* nodeport */
		sysIdentifier == 0 ? 'n' : ' ', /* sysidentifier */
		' ',                            /* goalstate */
		' ',                            /* reportedstate */
		' ',                            /* candidate_priority */
		' ',                            /* replication_quorum */
		' ',                            /* prefix */
		' '                             /* nodecluster */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int64 insertedNodeId = 0;

	/*
	 * The node name can be specified by the user as the --name argument at
	 * node registration time, in which case that's what we use of course.
	 *
	 * That said, when the user is not using --name, we still want the node
	 * name NOT NULL and default to 'node_%d' using the nodeid. We can't use
	 * another column in a DEFAULT value though, so we implement this default
	 * in a CASE expression in the INSERT query.
	 *
	 * In a citus formation kind, we want to name the node with the convention
	 * 'coordinator_%d' for the coordinator nodes, and 'worker%d' for the
	 * worker nodes.
	 */

	const char *insertQuery =
		"WITH seq(nodeid) AS "
		"(SELECT case when $2 = -1 "
		"  then nextval('pgautofailover.node_nodeid_seq'::regclass) "
		"  else $2 end) "
		"INSERT INTO " AUTO_FAILOVER_NODE_TABLE
		" (formationid, nodeid, groupid, nodename, nodehost, nodeport, "
		" sysidentifier, goalstate, reportedstate, "
		" candidatepriority, replicationquorum, nodecluster)"
		" SELECT $1, seq.nodeid, $3, "
		" case when $4 is null then format('%s_%s', $12, seq.nodeid) else $4 end, "
		" $5, $6, $7, $8, $9, $10, $11, $13 "
		" FROM seq "
		"RETURNING nodeid";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(insertQuery, argCount,
										  argTypes, argValues, argNulls,
										  false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;

		Datum nodeIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc,
										  1,
										  &isNull);

		insertedNodeId = DatumGetInt64(nodeIdDatum);
	}
	else
	{
		elog(ERROR, "could not insert into " AUTO_FAILOVER_NODE_TABLE);
	}

	/* when a desired_node_id has been given, maintain the nodeid sequence */
	if (nodeId != -1)
	{
		const char *setValQuery =
			"SELECT setval('pgautofailover.node_nodeid_seq'::regclass, "
			" max(nodeid)+1) "
			" FROM " AUTO_FAILOVER_NODE_TABLE;

		int spiStatus = SPI_execute_with_args(setValQuery,
											  0, NULL, NULL, NULL,
											  false, 0);

		if (spiStatus != SPI_OK_SELECT)
		{
			elog(ERROR,
				 "could not setval('pgautofailover.node_nodeid_seq'::regclass)");
		}
	}

	SPI_finish();

	return insertedNodeId;
}


/*
 * SetNodeGoalState updates the goal state of a node both on-disk and
 * in-memory, and notifies the state change.
 */
void
SetNodeGoalState(AutoFailoverNode *pgAutoFailoverNode,
				 ReplicationState goalState,
				 const char *message)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* goalstate */
		INT8OID  /* nodeid */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(goalStateOid),           /* goalstate */
		Int64GetDatum(pgAutoFailoverNode->nodeId) /* nodeid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET goalstate = $1, statechangetime = now() "
		"WHERE nodeid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();

	/*
	 * Now that the UPDATE went through, update the pgAutoFailoverNode struct
	 * with the new goal State and notify the state change.
	 */
	pgAutoFailoverNode->goalState = goalState;

	if (message != NULL)
	{
		NotifyStateChange(pgAutoFailoverNode, (char *) message);
	}
}


/*
 * ReportAutoFailoverNodeState persists the reported state and nodes version of
 * a node.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
ReportAutoFailoverNodeState(char *nodeHost, int nodePort,
							ReplicationState reportedState,
							bool pgIsRunning, SyncState pgSyncState,
							int reportedTLI,
							XLogRecPtr reportedLSN)
{
	Oid reportedStateOid = ReplicationStateGetEnum(reportedState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* reportedstate */
		BOOLOID,                 /* pg_ctl status: is running */
		TEXTOID,                 /* pg_stat_replication.sync_state */
		INT4OID,                 /* reportedtli */
		LSNOID,                  /* reportedlsn */
		TEXTOID,                 /* nodehost */
		INT4OID                  /* nodeport */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(reportedStateOid),   /* reportedstate */
		BoolGetDatum(pgIsRunning),            /* pg_ctl status: is running */
		CStringGetTextDatum(SyncStateToString(pgSyncState)), /* sync_state */
		Int32GetDatum(reportedTLI),                          /* reportedtli */
		LSNGetDatum(reportedLSN),             /* reportedlsn */
		CStringGetTextDatum(nodeHost),        /* nodehost */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET reportedstate = $1, reporttime = now(), "
		"reportedpgisrunning = $2, reportedrepstate = $3, "
		"reportedtli = CASE $4 WHEN 0 THEN reportedtli ELSE $4 END, "
		"reportedlsn = CASE $5 WHEN '0/0'::pg_lsn THEN reportedlsn ELSE $5 END, "
		"walreporttime = CASE $5 WHEN '0/0'::pg_lsn THEN walreporttime ELSE now() END, "
		"statechangetime = CASE WHEN reportedstate <> $1 THEN now() ELSE statechangetime END "
		"WHERE nodehost = $6 AND nodeport = $7";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
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
ReportAutoFailoverNodeHealth(char *nodeHost, int nodePort,
							 ReplicationState goalState,
							 NodeHealthState health)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		replicationStateTypeOid, /* goalstate */
		INT4OID, /* health */
		TEXTOID, /* nodehost */
		INT4OID  /* nodeport */
	};

	Datum argValues[] = {
		ObjectIdGetDatum(goalStateOid), /* goalstate */
		Int32GetDatum(health),          /* reportedversion */
		CStringGetTextDatum(nodeHost),  /* nodehost */
		Int32GetDatum(nodePort)         /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET goalstate = $1, health = $2, "
		"healthchecktime = now(), statechangetime = now() "
		"WHERE nodehost = $3 AND nodeport = $4";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
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
ReportAutoFailoverNodeReplicationSetting(int64 nodeid,
										 char *nodeHost, int nodePort,
										 int candidatePriority,
										 bool replicationQuorum)
{
	Oid argTypes[] = {
		INT4OID,                 /* candidate_priority */
		BOOLOID,                 /* repliation_quorum */
		INT8OID,                 /* nodeid */
		TEXTOID,                 /* nodehost */
		INT4OID                  /* nodeport */
	};

	Datum argValues[] = {
		Int32GetDatum(candidatePriority),     /* candidate_priority */
		BoolGetDatum(replicationQuorum),      /* replication_quorum */
		Int64GetDatum(nodeid),                /* nodeid */
		CStringGetTextDatum(nodeHost),        /* nodehost */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		"   SET candidatepriority = $1, replicationquorum = $2 "
		" WHERE nodeid = $3 and nodehost = $4 AND nodeport = $5";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);

	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_NODE_TABLE);
	}

	SPI_finish();
}


/*
 * UpdateAutoFailoverNodeMetadata updates a node registration to a possibly new
 * nodeName, nodeHost, and nodePort. Those are NULL (or zero) when not changed.
 *
 * We use SPI to automatically handle triggers, function calls, etc.
 */
void
UpdateAutoFailoverNodeMetadata(int64 nodeid,
							   char *nodeName,
							   char *nodeHost,
							   int nodePort)
{
	Oid argTypes[] = {
		INT8OID,                 /* nodeid */
		TEXTOID,                 /* nodename */
		TEXTOID,                 /* nodehost */
		INT4OID                  /* nodeport */
	};

	Datum argValues[] = {
		Int64GetDatum(nodeid),                /* nodeid */
		CStringGetTextDatum(nodeName),        /* nodename */
		CStringGetTextDatum(nodeHost),        /* nodehost */
		Int32GetDatum(nodePort)               /* nodeport */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_NODE_TABLE
		" SET nodename = $2, nodehost = $3, nodeport = $4 "
		"WHERE nodeid = $1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
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
RemoveAutoFailoverNode(AutoFailoverNode *pgAutoFailoverNode)
{
	Oid argTypes[] = {
		INT8OID  /* nodeId */
	};

	Datum argValues[] = {
		Int64GetDatum(pgAutoFailoverNode->nodeId)        /* nodeId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *deleteQuery =
		"DELETE FROM " AUTO_FAILOVER_NODE_TABLE
		" WHERE nodeid = $1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(deleteQuery,
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
	SyncState syncStateArray[] = {
		SYNC_STATE_UNKNOWN,
		SYNC_STATE_UNKNOWN,
		SYNC_STATE_SYNC,
		SYNC_STATE_ASYNC,
		SYNC_STATE_QUORUM,
		SYNC_STATE_POTENTIAL
	};
	char *syncStateList[] = {
		"", "unknown",
		"sync", "async", "quorum", "potential",
		NULL
	};

	for (int listIndex = 0; syncStateList[listIndex] != NULL; listIndex++)
	{
		char *candidate = syncStateList[listIndex];

		if (strcmp(pgsrSyncState, candidate) == 0)
		{
			return syncStateArray[listIndex];
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
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
		{
			return "unknown";
		}

		case SYNC_STATE_ASYNC:
		{
			return "async";
		}

		case SYNC_STATE_SYNC:
		{
			return "sync";
		}

		case SYNC_STATE_QUORUM:
		{
			return "quorum";
		}

		case SYNC_STATE_POTENTIAL:
		{
			return "potential";
		}

		default:
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
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
	return pgAutoFailoverNode != NULL &&
		   pgAutoFailoverNode->goalState == pgAutoFailoverNode->reportedState &&
		   pgAutoFailoverNode->goalState == state;
}


/*
 * CanTakeWritesInState returns whether a node can take writes when in
 * the given state.
 */
bool
CanTakeWritesInState(ReplicationState state)
{
	return state == REPLICATION_STATE_SINGLE ||
		   state == REPLICATION_STATE_PRIMARY ||
		   state == REPLICATION_STATE_WAIT_PRIMARY ||
		   state == REPLICATION_STATE_JOIN_PRIMARY ||
		   state == REPLICATION_STATE_APPLY_SETTINGS;
}


/*
 * CanInitiateFailover returns whether a node is a primary that we can initiate
 * a (manual) failover from. We refuse to failover from a WAIT_PRIMARY node
 * because we're not sure if the secondary has done catching-up yet.
 */
bool
CanInitiateFailover(ReplicationState state)
{
	return state == REPLICATION_STATE_SINGLE ||
		   state == REPLICATION_STATE_PRIMARY ||
		   state == REPLICATION_STATE_JOIN_PRIMARY;
}


/*
 * StateBelongsToPrimary returns true when given state belongs to a primary
 * node, either in a healthy state or even when in the middle of being demoted.
 */
bool
StateBelongsToPrimary(ReplicationState state)
{
	return CanTakeWritesInState(state) ||
		   state == REPLICATION_STATE_DRAINING ||
		   state == REPLICATION_STATE_DEMOTE_TIMEOUT ||
		   state == REPLICATION_STATE_PREPARE_MAINTENANCE;
}


/*
 * IsBeingDemotedPrimary returns true when a given node is currently going
 * through a demotion.
 */
bool
IsBeingDemotedPrimary(AutoFailoverNode *node)
{
	return node != NULL &&
		   (StateBelongsToPrimary(node->reportedState) &&
			(node->goalState == REPLICATION_STATE_DRAINING ||
			 node->goalState == REPLICATION_STATE_DEMOTE_TIMEOUT ||
			 node->goalState == REPLICATION_STATE_PREPARE_MAINTENANCE));
}


/*
 * IsDemotedPrimary returns true when a node has completed a process of
 * demotion.
 */
bool
IsDemotedPrimary(AutoFailoverNode *node)
{
	return node != NULL &&
		   (node->goalState == REPLICATION_STATE_DEMOTED &&
			(StateBelongsToPrimary(node->reportedState) ||
			 node->reportedState == REPLICATION_STATE_DEMOTED));
}


/*
 * IsBeingPromoted returns whether a standby node is going through the process
 * of a promotion.
 *
 * We need to recognize a node going though the FSM even before it has reached
 * a stable state (where reportedState and goalState are the same).
 */
bool
IsBeingPromoted(AutoFailoverNode *node)
{
	return node != NULL &&
		   ((node->reportedState == REPLICATION_STATE_REPORT_LSN &&
			 (node->goalState == REPLICATION_STATE_FAST_FORWARD ||
			  node->goalState == REPLICATION_STATE_PREPARE_PROMOTION)) ||

			(node->reportedState == REPLICATION_STATE_FAST_FORWARD &&
			 (node->goalState == REPLICATION_STATE_FAST_FORWARD ||
			  node->goalState == REPLICATION_STATE_PREPARE_PROMOTION)) ||

			(node->reportedState == REPLICATION_STATE_PREPARE_PROMOTION &&
			 (node->goalState == REPLICATION_STATE_PREPARE_PROMOTION ||
			  node->goalState == REPLICATION_STATE_STOP_REPLICATION ||
			  node->goalState == REPLICATION_STATE_WAIT_PRIMARY)) ||

			(node->reportedState == REPLICATION_STATE_STOP_REPLICATION &&
			 (node->goalState == REPLICATION_STATE_STOP_REPLICATION ||
			  node->goalState == REPLICATION_STATE_WAIT_PRIMARY)));
}


/*
 * CandidateNodeIsReadyToStreamWAL returns whether a newly selected candidate
 * node, possibly still being promoted, is ready for the other standby nodes is
 * REPORT_LSN to already use the new primary as an upstream node.
 *
 * We're okay with making progress when the selected candidate is on the
 * expected path of FAST_FORWARD to PREPARE_PROMOTION to STOP_REPLICATION to
 * WAIT_PRIMARY to PRIMARY. We want to allow matching intermediate states (when
 * reportedState and goalState are not the same), and we also want to prevent
 * matching other FSM paths.
 *
 * Finally, FAST_FORWARD is a little too soon, so we skip that.
 */
bool
CandidateNodeIsReadyToStreamWAL(AutoFailoverNode *node)
{
	return node != NULL &&
		   ((node->reportedState == REPLICATION_STATE_PREPARE_PROMOTION &&
			 (node->goalState == REPLICATION_STATE_STOP_REPLICATION ||
			  node->goalState == REPLICATION_STATE_WAIT_PRIMARY)) ||

			(node->reportedState == REPLICATION_STATE_STOP_REPLICATION &&
			 (node->goalState == REPLICATION_STATE_STOP_REPLICATION ||
			  node->goalState == REPLICATION_STATE_WAIT_PRIMARY)) ||

			(node->reportedState == REPLICATION_STATE_WAIT_PRIMARY &&
			 (node->goalState == REPLICATION_STATE_WAIT_PRIMARY ||
			  node->goalState == REPLICATION_STATE_PRIMARY)) ||

			(node->reportedState == REPLICATION_STATE_PRIMARY &&
			 node->goalState == REPLICATION_STATE_PRIMARY));
}


/*
 * IsParticipatingInPromotion returns whether a node is currently participating
 * in a promotion, either as a candidate that IsBeingPromoted, or as a
 * "support" node that is reporting its LSN or re-joining as a secondary.
 */
bool
IsParticipatingInPromotion(AutoFailoverNode *node)
{
	return IsBeingPromoted(node) ||

		   node->reportedState == REPLICATION_STATE_REPORT_LSN ||
		   node->goalState == REPLICATION_STATE_REPORT_LSN ||

		   node->reportedState == REPLICATION_STATE_JOIN_SECONDARY ||
		   node->goalState == REPLICATION_STATE_JOIN_SECONDARY;
}


/*
 * IsInWaitOrJoinState returns true when the given node is a primary node that
 * is currently busy with registering a standby: it's then been assigned either
 * WAIT_STANDBY or JOIN_STANDBY replication state.
 */
bool
IsInWaitOrJoinState(AutoFailoverNode *node)
{
	return node != NULL &&
		   (node->reportedState == REPLICATION_STATE_WAIT_PRIMARY ||
			node->goalState == REPLICATION_STATE_WAIT_PRIMARY ||
			node->reportedState == REPLICATION_STATE_JOIN_PRIMARY ||
			node->goalState == REPLICATION_STATE_JOIN_PRIMARY);
}


/*
 * IsInPrimaryState returns true if the given node is known to have converged
 * to a state that makes it the primary node in its group.
 */
bool
IsInPrimaryState(AutoFailoverNode *pgAutoFailoverNode)
{
	return pgAutoFailoverNode != NULL &&

		   ((pgAutoFailoverNode->goalState == pgAutoFailoverNode->reportedState &&
			 CanTakeWritesInState(pgAutoFailoverNode->goalState)) ||

	        /*
	         * We accept both apply_settings -> primary and primary ->
	         * apply_settings as primary states.
	         */
			((pgAutoFailoverNode->goalState == REPLICATION_STATE_APPLY_SETTINGS ||
			  pgAutoFailoverNode->goalState == REPLICATION_STATE_PRIMARY) &&
			 (pgAutoFailoverNode->reportedState == REPLICATION_STATE_PRIMARY ||
			  pgAutoFailoverNode->reportedState == REPLICATION_STATE_APPLY_SETTINGS)));
}


/*
 * IsInMaintenance returns true if the given node has been assigned a
 * maintenance state, whether it reached it yet or not.
 */
bool
IsInMaintenance(AutoFailoverNode *node)
{
	return node != NULL &&
		   (node->goalState == REPLICATION_STATE_PREPARE_MAINTENANCE ||
			node->goalState == REPLICATION_STATE_WAIT_MAINTENANCE ||
			node->goalState == REPLICATION_STATE_MAINTENANCE);
}


/*
 * IsStateIn returns true if state is equal to any of allowedStates
 */
bool
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
 * IsHealthy returns whether the given node is heathly, meaning it succeeds the
 * last health check and its PostgreSQL instance is reported as running by the
 * keeper.
 */
bool
IsHealthy(AutoFailoverNode *pgAutoFailoverNode)
{
	TimestampTz now = GetCurrentTimestamp();
	int nodeActiveCallsFrequencyMs = 1 * 1000; /* keeper sleep time */

	if (pgAutoFailoverNode == NULL)
	{
		return false;
	}

	/*
	 * If the keeper has been reporting that Postgres is running after our last
	 * background check run, and within the node-active protocol client-time
	 * sleep time (1 second), then trust pg_autoctl node reporting: we might be
	 * out of a network split or node-local failure mode, and our background
	 * checks might not have run yet to clarify that "back to good" situation.
	 *
	 * In any case, the pg_autoctl node-active process could connect to the
	 * monitor, so there is no network split at this time.
	 */
	if (pgAutoFailoverNode->health == NODE_HEALTH_BAD &&
		TimestampDifferenceExceeds(pgAutoFailoverNode->healthCheckTime,
								   pgAutoFailoverNode->reportTime,
								   0) &&
		TimestampDifferenceExceeds(pgAutoFailoverNode->reportTime,
								   now,
								   nodeActiveCallsFrequencyMs))
	{
		return pgAutoFailoverNode->pgIsRunning;
	}

	/* nominal case: trust background checks + reported Postgres state */
	return pgAutoFailoverNode->health == NODE_HEALTH_GOOD &&
		   pgAutoFailoverNode->pgIsRunning == true;
}


/*
 * IsUnhealthy returns whether the given node is unhealthy, meaning it failed
 * its last health check and has not reported for more than UnhealthyTimeoutMs,
 * and it's PostgreSQL instance has been reporting as running by the keeper.
 */
bool
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
 * IsReporting returns whether the given node has reported recently, within the
 * UnhealthyTimeoutMs interval.
 */
bool
IsReporting(AutoFailoverNode *pgAutoFailoverNode)
{
	TimestampTz now = GetCurrentTimestamp();

	if (pgAutoFailoverNode == NULL)
	{
		return false;
	}

	if (TimestampDifferenceExceeds(pgAutoFailoverNode->reportTime,
								   now,
								   UnhealthyTimeoutMs))
	{
		return false;
	}

	return true;
}


/*
 * IsDrainTimeExpired returns whether the node should be done according
 * to the drain time-outs.
 */
bool
IsDrainTimeExpired(AutoFailoverNode *pgAutoFailoverNode)
{
	bool drainTimeExpired = false;

	if (pgAutoFailoverNode == NULL ||
		pgAutoFailoverNode->goalState != REPLICATION_STATE_DEMOTE_TIMEOUT)
	{
		return false;
	}

	TimestampTz now = GetCurrentTimestamp();
	if (TimestampDifferenceExceeds(pgAutoFailoverNode->stateChangeTime,
								   now,
								   DrainTimeoutMs))
	{
		drainTimeExpired = true;
	}

	return drainTimeExpired;
}
