/*-------------------------------------------------------------------------
 *
 * src/monitor/node_metadata.h
 *
 * Declarations for public functions and types related to node metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"

#include "health_check.h"
#include "replication_state.h"

#define AUTO_FAILOVER_NODE_TABLE_NAME "node"

/* column indexes for pgautofailover.node
 * indices must match with the columns given
 * in the following definition.
 */
#define Natts_pgautofailover_node 15
#define Anum_pgautofailover_node_formationid 1
#define Anum_pgautofailover_node_nodeid 2
#define Anum_pgautofailover_node_groupid 3
#define Anum_pgautofailover_node_nodename 4
#define Anum_pgautofailover_node_nodeport 5
#define Anum_pgautofailover_node_goalstate 6
#define Anum_pgautofailover_node_reportedstate 7
#define Anum_pgautofailover_node_reportedpgisrunning 8
#define Anum_pgautofailover_node_reportedrepstate 9
#define Anum_pgautofailover_node_reporttime 10
#define Anum_pgautofailover_node_walreporttime 11
#define Anum_pgautofailover_node_health 12
#define Anum_pgautofailover_node_healthchecktime 13
#define Anum_pgautofailover_node_statechangetime 14
#define Anum_pgautofailover_node_reportedLSN 15
#define Anum_pgautofailover_node_candidate_priority 16
#define Anum_pgautofailover_node_replication_quorum 17

#define AUTO_FAILOVER_NODE_TABLE_ALL_COLUMNS \
    "formationid, "			\
	"nodeid, "				\
	"groupid, "				\
	"nodename, "			\
	"nodeport, "			\
	"goalstate, "			\
	"reportedstate, "		\
	"reportedpgisrunning, "	\
	"reportedrepstate, "	\
	"reporttime, "			\
	"walreporttime, "		\
	"health, "				\
	"healthchecktime, "		\
	"statechangetime, "		\
	"reportedlsn, "			\
	"candidatepriority, "	\
    "replicationquorum"


#define SELECT_ALL_FROM_AUTO_FAILOVER_NODE_TABLE \
	"SELECT " AUTO_FAILOVER_NODE_TABLE_ALL_COLUMNS " FROM " AUTO_FAILOVER_NODE_TABLE

/* pg_stat_replication.sync_state: "sync", "async", "quorum", "potential" */
typedef enum SyncState
{
	SYNC_STATE_UNKNOWN = 0,
	SYNC_STATE_SYNC,
	SYNC_STATE_ASYNC,
	SYNC_STATE_QUORUM,
	SYNC_STATE_POTENTIAL
} SyncState;


/*
 * AutoFailoverNode represents a Postgres node that is being tracked by the
 * pg_auto_failover monitor.
 */
typedef struct AutoFailoverNode
{
	char            *formationId;
	int              nodeId;
	int              groupId;
	char            *nodeName;
	int              nodePort;
	ReplicationState goalState;
	ReplicationState reportedState;
	TimestampTz      reportTime;
	bool             pgIsRunning;
	SyncState        pgsrSyncState;
	TimestampTz      walReportTime;
	NodeHealthState  health;
	TimestampTz      healthCheckTime;
	TimestampTz      stateChangeTime;
	XLogRecPtr       reportedLSN;
	int              candidatePriority;
	bool             replicationQuorum;
} AutoFailoverNode;


/* public function declarations */
extern List * AllAutoFailoverNodes(char *formationId);
extern List * AutoFailoverNodeGroup(char *formationId, int groupId);
extern List * AutoFailoverOtherNodesList(AutoFailoverNode *pgAutoFailoverNode);
extern List * AutoFailoverOtherNodesListInState(
	AutoFailoverNode *pgAutoFailoverNode, ReplicationState currentState);
extern AutoFailoverNode * GetPrimaryNodeInGroup(char *formationId, int32 groupId);
extern AutoFailoverNode * FindFailoverNewStandbyNode(List *groupNodeList);
extern List *GroupListCandidates(List *groupNodeList);
extern List *GroupListSyncStandbys(List *groupNodeList);
extern bool AllNodesHaveSameCandidatePriority(List *groupNodeList);
extern int CountStandbyCandidates(AutoFailoverNode *primaryNode,
								  List *stateList);
extern AutoFailoverNode * FindMostAdvancedStandby(List *groupNodeList);
extern AutoFailoverNode * GetAutoFailoverNode(char *nodeName, int nodePort);
extern AutoFailoverNode * GetAutoFailoverNodeWithId(int nodeid,
													char *nodeName, int nodePort);
extern AutoFailoverNode * GetWritableNodeInGroup(char *formationId, int32 groupId);
extern AutoFailoverNode * GetPrimaryNodeInGroup(char *formationId, int32 groupId);
extern AutoFailoverNode * TupleToAutoFailoverNode(TupleDesc tupleDescriptor,
												  HeapTuple heapTuple);
extern int AddAutoFailoverNode(char *formationId, int groupId,
							   char *nodeName, int nodePort,
							   ReplicationState goalState,
							   ReplicationState reportedState,
							   int candidatePriority,
							   bool replicationQuorum);
extern void SetNodeGoalState(char *nodeName, int nodePort,
							 ReplicationState goalState);
extern void ReportAutoFailoverNodeState(char *nodeName, int nodePort,
										ReplicationState reportedState,
										bool pgIsRunning,
										SyncState pgSyncState,
										XLogRecPtr reportedLSN);
extern void ReportAutoFailoverNodeHealth(char *nodeName, int nodePort,
										 ReplicationState goalState,
										 NodeHealthState health);
extern void ReportAutoFailoverNodeReplicationSetting(int nodeid,
													 char *nodeName,
													 int nodePort,
													 int candidatePriority,
													 bool replicationQuorum);
extern void RemoveAutoFailoverNode(char *nodeName, int nodePort);

extern SyncState SyncStateFromString(const char *pgsrSyncState);
extern char *SyncStateToString(SyncState pgsrSyncState);
extern bool IsCurrentState(AutoFailoverNode *pgAutoFailoverNode,
						   ReplicationState state);
extern bool CanTakeWritesInState(ReplicationState state);
extern bool StateBelongsToPrimary(ReplicationState state);
extern bool IsBeingPromoted(AutoFailoverNode *node);
extern bool IsInWaitOrJoinState(AutoFailoverNode *node);
extern bool IsInPrimaryState(AutoFailoverNode *pgAutoFailoverNode);
extern bool IsStateIn(ReplicationState state, List *allowedStates);
