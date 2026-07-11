/*-------------------------------------------------------------------------
 *
 * src/monitor/group_state_machine.h
 *
 * Declarations for public functions and types related to a group state
 * machine.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */
#pragma once

#include "postgres.h"

#include "access/xlogdefs.h"
#include "datatype/timestamp.h"
#include "formation_metadata.h"
#include "node_metadata.h"

/*
 * AutoFailoverNodeState describes the current state of a node in a group.
 */
typedef struct AutoFailoverNodeState
{
	int64 nodeId;
	int32 groupId;
	ReplicationState replicationState;
	int32 reportedTLI;
	XLogRecPtr reportedLSN;
	SyncState pgsrSyncState;
	bool pgIsRunning;
	int candidatePriority;
	bool replicationQuorum;
} AutoFailoverNodeState;


/*
 * GroupStateContext bundles every input the monitor node_active protocol needs
 * to make FSM decisions: the node list (loaded once from the DB), the
 * formation, a single timestamp snapshot, and copies of the relevant GUCs.
 *
 * Production code builds this with BuildGroupStateContext(), which fetches
 * everything from the database.  Test code can populate it from fixtures and
 * call ProceedGroupStateFromContext() directly, making the FSM logic
 * exercisable without a live database.
 */
typedef struct GroupStateContext
{
	char *formationId;
	int groupId;
	AutoFailoverNode *activeNode;
	List *groupNodeList;
	int groupNodeCount;
	AutoFailoverFormation *formation;
	TimestampTz now;
	int unhealthyTimeoutMs;
	int drainTimeoutMs;
	int startupGracePeriodMs;
} GroupStateContext;


/* public function declarations */
extern bool BuildGroupStateContext(GroupStateContext *ctx,
								   AutoFailoverNode *activeNode);
extern bool ProceedGroupState(AutoFailoverNode *activeNode);
extern bool ProceedGroupStateFromContext(GroupStateContext *ctx);

/* GUCs */
extern int EnableSyncXlogThreshold;
extern int PromoteXlogThreshold;
extern int DrainTimeoutMs;
extern int UnhealthyTimeoutMs;
extern int StartupGracePeriodMs;
