/*-------------------------------------------------------------------------
 *
 * src/monitor/replication_state.h
 *
 * Declarations for public functions and types related to (de)serialising
 * replication states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once


/*
 * ReplicationState represents the current role of a node in a group.
 */
typedef enum ReplicationState
{
	REPLICATION_STATE_INITIAL = 0,
	REPLICATION_STATE_SINGLE = 1,
	REPLICATION_STATE_WAIT_PRIMARY = 2,
	REPLICATION_STATE_PRIMARY = 3,
	REPLICATION_STATE_DRAINING = 4,
	REPLICATION_STATE_DEMOTE_TIMEOUT = 5,
	REPLICATION_STATE_DEMOTED = 6,
	REPLICATION_STATE_CATCHINGUP = 7,
	REPLICATION_STATE_SECONDARY = 8,
	REPLICATION_STATE_PREPARE_PROMOTION = 9,
	REPLICATION_STATE_STOP_REPLICATION = 10,
	REPLICATION_STATE_WAIT_STANDBY = 11,
	REPLICATION_STATE_MAINTENANCE = 12,
	REPLICATION_STATE_JOIN_PRIMARY = 13,
	REPLICATION_STATE_APPLY_SETTINGS = 14,
	REPLICATION_STATE_PREPARE_MAINTENANCE = 15,
	REPLICATION_STATE_WAIT_MAINTENANCE = 16,
	REPLICATION_STATE_REPORT_LSN = 17,
	REPLICATION_STATE_FAST_FORWARD = 18,
	REPLICATION_STATE_JOIN_SECONDARY = 19,
	REPLICATION_STATE_DROPPED = 20,
	REPLICATION_STATE_UNKNOWN = 21
} ReplicationState;


/* declarations of public functions */
extern Oid ReplicationStateTypeOid(void);
extern ReplicationState EnumGetReplicationState(Oid replicationStateOid);
extern Oid ReplicationStateGetEnum(ReplicationState replicationState);
extern ReplicationState NameGetReplicationState(char *replicationStateName);
extern const char * ReplicationStateGetName(ReplicationState replicationState);
