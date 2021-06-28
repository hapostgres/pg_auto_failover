/*-------------------------------------------------------------------------
 *
 * src/monitor/health_check_metadata.h
 *
 * Declarations for public functions and types related to health check
 * metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/htup.h"
#include "access/tupdesc.h"
#include "nodes/pg_list.h"


/*
 * NodeHealthState represents the last-known health state of a node after
 * the last round of health checks.
 */
typedef enum
{
	NODE_HEALTH_UNKNOWN = -1,
	NODE_HEALTH_BAD = 0,
	NODE_HEALTH_GOOD = 1
} NodeHealthState;

/*
 * NodeHealth represents a node that is to be health-checked and its last-known
 * health state.
 */
typedef struct NodeHealth
{
	int64 nodeId;
	char *nodeName;
	char *nodeHost;
	int nodePort;
	NodeHealthState healthState;
} NodeHealth;


/* GUCs to configure health checks */
extern bool HealthChecksEnabled;
extern int HealthCheckPeriod;
extern int HealthCheckTimeout;
extern int HealthCheckMaxRetries;
extern int HealthCheckRetryDelay;


extern void InitializeHealthCheckWorker(void);
extern void HealthCheckWorkerMain(Datum arg);
extern void HealthCheckWorkerLauncherMain(Datum arg);
extern List * LoadNodeHealthList(void);
extern NodeHealth * TupleToNodeHealth(HeapTuple heapTuple,
									  TupleDesc tupleDescriptor);
extern void SetNodeHealthState(int64 nodeId,
							   char *nodeName,
							   char *nodeHost,
							   uint16 nodePort,
							   int previousHealthState,
							   int healthState);
extern void StopHealthCheckWorker(Oid databaseId);
extern char * NodeHealthToString(NodeHealthState health);
