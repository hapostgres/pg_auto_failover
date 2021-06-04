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
#include "node_metadata.h"

/*
 * AutoFailoverNodeState describes the current state of a node in a group.
 */
typedef struct AutoFailoverNodeState
{
	int32 nodeId;
	int32 groupId;
	ReplicationState replicationState;
	XLogRecPtr reportedLSN;
	SyncState pgsrSyncState;
	bool pgIsRunning;
	int candidatePriority;
	bool replicationQuorum;
} AutoFailoverNodeState;


typedef struct AutoFailoverNodeRegistration
{
	char *formationId;
	AutoFailoverNodeState *currentNodeState;
	char *nodeName;
	char *nodeHost;
	int32 nodePort;
	char *expectedDBName;
	uint64 sysIdentifier;
	char *nodeKind;
	char *nodeCluster;
	AutoFailoverNode *pgAutoFailoverNode;
} AutoFailoverNodeRegistration;


/* public function declarations */
extern bool ProceedGroupState(AutoFailoverNode *activeNode);

/* node_active_protocol.c */
extern AutoFailoverNodeState * RegisterNode(
	AutoFailoverNodeRegistration *nodeRegistration);

/* GUCs */
extern int EnableSyncXlogThreshold;
extern int PromoteXlogThreshold;
extern int DrainTimeoutMs;
extern int UnhealthyTimeoutMs;
extern int StartupGracePeriodMs;
