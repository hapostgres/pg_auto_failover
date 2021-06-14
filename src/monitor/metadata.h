/*-------------------------------------------------------------------------
 *
 * src/monitor/metadata.h
 *
 * Declarations for public functions and types related to pg_auto_failover
 * metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "storage/lockdefs.h"

#define AUTO_FAILOVER_EXTENSION_VERSION "1.6"
#define AUTO_FAILOVER_EXTENSION_NAME "pgautofailover"
#define AUTO_FAILOVER_SCHEMA_NAME "pgautofailover"
#define AUTO_FAILOVER_FORMATION_TABLE "pgautofailover.formation"
#define AUTO_FAILOVER_NODE_TABLE "pgautofailover.node"
#define AUTO_FAILOVER_EVENT_TABLE "pgautofailover.event"
#define REPLICATION_STATE_TYPE_NAME "replication_state"


/*
 * Postgres' advisory locks use 'field4' to discern between different kind of
 * advisory locks. It only uses values 1 and 2, whereas Citus uses values 4, 5
 * 6. We start counting at 10 to avoid conflict.
 */
typedef enum AutoFailoverHALocktagClass
{
	ADV_LOCKTAG_CLASS_AUTO_FAILOVER_FORMATION = 10,
	ADV_LOCKTAG_CLASS_AUTO_FAILOVER_NODE_GROUP = 11
} AutoFailoverHALocktagClass;

/* GUC variable for version checks, true by default */
extern bool EnableVersionChecks;

/* public function declarations */
extern Oid pgAutoFailoverRelationId(const char *relname);
extern Oid pgAutoFailoverSchemaId(void);
extern Oid pgAutoFailoverExtensionOwner(void);
extern void LockFormation(char *formationId, LOCKMODE lockMode);
extern void LockNodeGroup(char *formationId, int groupId, LOCKMODE lockMode);
extern bool checkPgAutoFailoverVersion(void);
