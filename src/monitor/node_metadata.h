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

#include <inttypes.h>

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"

#include "health_check.h"
#include "replication_state.h"

#define AUTO_FAILOVER_NODE_TABLE_NAME "node"

/* column indexes for pgautofailover.node
 * indices must match with the columns given
 * in the following definition.
 */
#define Natts_pgautofailover_node 19
#define Anum_pgautofailover_node_formationid 1
#define Anum_pgautofailover_node_nodeid 2
#define Anum_pgautofailover_node_groupid 3
#define Anum_pgautofailover_node_nodename 4
#define Anum_pgautofailover_node_nodehost 5
#define Anum_pgautofailover_node_nodeport 6
#define Anum_pgautofailover_node_sysidentifier 7
#define Anum_pgautofailover_node_goalstate 8
#define Anum_pgautofailover_node_reportedstate 9
#define Anum_pgautofailover_node_reportedpgisrunning 10
#define Anum_pgautofailover_node_reportedrepstate 11
#define Anum_pgautofailover_node_reporttime 12
#define Anum_pgautofailover_node_reportedTLI 13
#define Anum_pgautofailover_node_reportedLSN 14
#define Anum_pgautofailover_node_walreporttime 15
#define Anum_pgautofailover_node_health 16
#define Anum_pgautofailover_node_healthchecktime 17
#define Anum_pgautofailover_node_statechangetime 18
#define Anum_pgautofailover_node_candidate_priority 19
#define Anum_pgautofailover_node_replication_quorum 20
#define Anum_pgautofailover_node_nodecluster 21

#define AUTO_FAILOVER_NODE_TABLE_ALL_COLUMNS \
	"formationid, " \
	"nodeid, " \
	"groupid, " \
	"nodename, " \
	"nodehost, " \
	"nodeport, " \
	"sysidentifier, " \
	"goalstate, " \
	"reportedstate, " \
	"reportedpgisrunning, " \
	"reportedrepstate, " \
	"reporttime, " \
	"reportedtli, " \
	"reportedlsn, " \
	"walreporttime, " \
	"health, " \
	"healthchecktime, " \
	"statechangetime, " \
	"candidatepriority, " \
	"replicationquorum, " \
	"nodecluster"


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
 * We restrict candidatePriority values in the range 0..100 to the users.
 * Internally, we increment the candidatePriority (+= 100) when the
 * perform_promotion API is used, in order to tweak the selection of the
 * candidate.
 */
#define MAX_USER_DEFINED_CANDIDATE_PRIORITY 100
#define CANDIDATE_PRIORITY_INCREMENT (MAX_USER_DEFINED_CANDIDATE_PRIORITY + 1)


/*
 * Use the same output format each time we are notifying and logging about an
 * AutoFailoverNode, for consistency. Well, apart when registering, where we
 * don't have the node id and/or the node name yet.
 */
#define NODE_FORMAT "node %lld \"%s\" (%s:%d)"
#define NODE_FORMAT_ARGS(node) \
	(long long) node->nodeId, node->nodeName, node->nodeHost, node->nodePort

/*
 * AutoFailoverNode represents a Postgres node that is being tracked by the
 * pg_auto_failover monitor.
 */
typedef struct AutoFailoverNode
{
	char *formationId;
	int64 nodeId;
	int groupId;
	char *nodeName;
	char *nodeHost;
	int nodePort;
	uint64 sysIdentifier;
	ReplicationState goalState;
	ReplicationState reportedState;
	TimestampTz reportTime;
	bool pgIsRunning;
	SyncState pgsrSyncState;
	TimestampTz walReportTime;
	NodeHealthState health;
	TimestampTz healthCheckTime;
	TimestampTz stateChangeTime;
	int reportedTLI;
	XLogRecPtr reportedLSN;
	int candidatePriority;
	bool replicationQuorum;
	char *nodeCluster;
} AutoFailoverNode;


/*
 * Formation.kind: "pgsql" or "citus"
 *
 * We define the formation kind here to avoid cyclic dependency between the
 * formation_metadata.h and node_metadata.h headers.
 */
typedef enum FormationKind
{
	FORMATION_KIND_UNKNOWN = 0,
	FORMATION_KIND_PGSQL,
	FORMATION_KIND_CITUS
} FormationKind;


/* public function declarations */
extern List * AllAutoFailoverNodes(char *formationId);
extern List * AutoFailoverNodeGroup(char *formationId, int groupId);
extern List * AutoFailoverAllNodesInGroup(char *formationId, int groupId);
extern List * AutoFailoverOtherNodesList(AutoFailoverNode *pgAutoFailoverNode);
extern List * AutoFailoverOtherNodesListInState(AutoFailoverNode *pgAutoFailoverNode,
												ReplicationState currentState);
extern List * AutoFailoverCandidateNodesListInState(AutoFailoverNode *pgAutoFailoverNode,
													ReplicationState currentState);
extern AutoFailoverNode * GetPrimaryNodeInGroup(char *formationId, int32 groupId);
AutoFailoverNode * GetNodeToFailoverFromInGroup(char *formationId, int32 groupId);
extern AutoFailoverNode * GetPrimaryOrDemotedNodeInGroup(char *formationId,
														 int32 groupId);
extern AutoFailoverNode * FindFailoverNewStandbyNode(List *groupNodeList);
extern List * GroupListCandidates(List *groupNodeList);
extern List * ListMostAdvancedStandbyNodes(List *groupNodeList);
extern List * GroupListSyncStandbys(List *groupNodeList);
extern bool AllNodesHaveSameCandidatePriority(List *groupNodeList);
extern int CountSyncStandbys(List *groupNodeList);
extern bool IsHealthySyncStandby(AutoFailoverNode *node);
extern int CountHealthySyncStandbys(List *groupNodeList);
extern int CountHealthyCandidates(List *groupNodeList);
extern bool IsFailoverInProgress(List *groupNodeList);
extern AutoFailoverNode * FindMostAdvancedStandby(List *groupNodeList);
extern AutoFailoverNode * FindCandidateNodeBeingPromoted(List *groupNodeList);

extern AutoFailoverNode * GetAutoFailoverNode(char *nodeHost, int nodePort);
extern AutoFailoverNode * GetAutoFailoverNodeById(int64 nodeId);
extern AutoFailoverNode * GetAutoFailoverNodeByName(char *formationId,
													char *nodeName);
extern AutoFailoverNode * OtherNodeInGroup(AutoFailoverNode *pgAutoFailoverNode);
extern AutoFailoverNode * GetWritableNodeInGroup(char *formationId, int32 groupId);
extern AutoFailoverNode * TupleToAutoFailoverNode(TupleDesc tupleDescriptor,
												  HeapTuple heapTuple);
extern int AddAutoFailoverNode(char *formationId,
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
							   char *nodeCluster);
extern void SetNodeGoalState(AutoFailoverNode *pgAutoFailoverNode,
							 ReplicationState goalState,
							 const char *message);
extern void ReportAutoFailoverNodeState(char *nodeHost, int nodePort,
										ReplicationState reportedState,
										bool pgIsRunning,
										SyncState pgSyncState,
										int reportedTLI,
										XLogRecPtr reportedLSN);
extern void ReportAutoFailoverNodeHealth(char *nodeHost, int nodePort,
										 ReplicationState goalState,
										 NodeHealthState health);
extern void ReportAutoFailoverNodeReplicationSetting(int64 nodeid,
													 char *nodeHost,
													 int nodePort,
													 int candidatePriority,
													 bool replicationQuorum);
extern void UpdateAutoFailoverNodeMetadata(int64 nodeid,
										   char *nodeName,
										   char *nodeHost,
										   int nodePort);
extern void RemoveAutoFailoverNode(AutoFailoverNode *pgAutoFailoverNode);


extern SyncState SyncStateFromString(const char *pgsrSyncState);
extern char * SyncStateToString(SyncState pgsrSyncState);
extern bool IsCurrentState(AutoFailoverNode *pgAutoFailoverNode,
						   ReplicationState state);
extern bool CanTakeWritesInState(ReplicationState state);
extern bool CanInitiateFailover(ReplicationState state);
extern bool StateBelongsToPrimary(ReplicationState state);
extern bool IsBeingPromoted(AutoFailoverNode *node);
extern bool IsBeingDemotedPrimary(AutoFailoverNode *node);
extern bool IsDemotedPrimary(AutoFailoverNode *node);
extern bool CandidateNodeIsReadyToStreamWAL(AutoFailoverNode *node);
extern bool IsParticipatingInPromotion(AutoFailoverNode *node);
extern bool IsInWaitOrJoinState(AutoFailoverNode *node);
extern bool IsInPrimaryState(AutoFailoverNode *pgAutoFailoverNode);
extern bool IsInMaintenance(AutoFailoverNode *node);
extern bool IsStateIn(ReplicationState state, List *allowedStates);
extern bool IsHealthy(AutoFailoverNode *pgAutoFailoverNode);
extern bool IsUnhealthy(AutoFailoverNode *pgAutoFailoverNode);
extern bool IsDrainTimeExpired(AutoFailoverNode *pgAutoFailoverNode);
extern bool IsReporting(AutoFailoverNode *pgAutoFailoverNode);
