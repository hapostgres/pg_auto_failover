/*-------------------------------------------------------------------------
 *
 * src/monitor/archive_metadata.h
 *
 * Declarations for public functions and types related to archive metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include <inttypes.h>

#include "access/htup.h"
#include "access/tupdesc.h"
#include "access/xlogdefs.h"
#include "datatype/timestamp.h"

#define AUTO_FAILOVER_PG_WAL_TABLE_NAME "pg_wal"

/* column indexes for pgautofailover.pg_wal
 * indices must match with the columns given
 * in the following definition.
 */
#define Natts_pgautofailover_pg_wal 8
#define Anum_pgautofailover_pg_wal_archiver_policy_id 1
#define Anum_pgautofailover_pg_wal_groupid 2
#define Anum_pgautofailover_pg_wal_nodeid 3
#define Anum_pgautofailover_pg_wal_filename 4
#define Anum_pgautofailover_pg_wal_filesize 5
#define Anum_pgautofailover_pg_wal_md5 6
#define Anum_pgautofailover_pg_wal_start_time 7
#define Anum_pgautofailover_pg_wal_finish_time 8

#define AUTO_FAILOVER_PG_WAL_TABLE_ALL_COLUMNS \
	"archiver_policy_id, " \
	"groupid, " \
	"nodeid, " \
	"filename, " \
	"filesize, " \
	"md5::text, " \
	"start_time, " \
	"finish_time"

#define SELECT_ALL_FROM_AUTO_FAILOVER_PG_WAL_TABLE \
	"SELECT " AUTO_FAILOVER_PG_WAL_TABLE_ALL_COLUMNS \
	" FROM pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME

/*
 * AutoFailoverPGWal represents a pg_wal entry that is being tracked by the
 * pg_auto_failover monitor.
 */
typedef struct AutoFailoverPGWal
{
	int64 policyId;
	int groupId;
	int64 nodeId;
	char *fileName;
	int64 fileSize;
	char *md5;
	TimestampTz startTime;
	TimestampTz finishTime;
} AutoFailoverPGWal;


#define AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_NAME "archiver_policy"

/* column indexes for pgautofailover.archiver_policy
 * indices must match with the columns given
 * in the following definition.
 */
#define Natts_pgautofailover_archiver_policy 8
#define Anum_pgautofailover_archiver_policy_archiver_policy_id 1
#define Anum_pgautofailover_archiver_policy_formationid 2
#define Anum_pgautofailover_archiver_policy_target 3
#define Anum_pgautofailover_archiver_policy_method 4
#define Anum_pgautofailover_archiver_policy_config 5
#define Anum_pgautofailover_archiver_policy_backup_interval 6
#define Anum_pgautofailover_archiver_policy_backup_max_count 7
#define Anum_pgautofailover_archiver_policy_backup_max_age 8

#define AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_ALL_COLUMNS \
	"archiver_policy_id, " \
	"formationid, " \
	"target, " \
	"method, " \
	"config, " \
	"backup_interval, " \
	"backup_max_count, " \
	"backup_max_age, "

#define SELECT_ALL_FROM_AUTO_FAILOVER_ARCHIVER_POLICY_TABLE \
	"SELECT " AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_ALL_COLUMNS \
	" FROM pgautofailover." AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_NAME


/*
 * AutoFailoverArchiverPolicy represents an archiver_policy entry that is being
 * tracked by the pg_auto_failover monitor.
 */
typedef struct AutoFailoverArchiverPolicy
{
	int64 policyId;
	char *formationId;
	char *target;
	char *method;
	char *config;
	Interval *backupInterval;
	int backupMaxCount;
	Interval *backupMaxAge;
} AutoFailoverArchiverPolicy;


extern AutoFailoverArchiverPolicy * AddAutoFailoverArchiverPolicy(char *formationId,
																  char *target,
																  char *method,
																  char *config,
																  Interval *backupInterval,
																  int backupMaxCount,
																  Interval *backdupMaxAge);


extern AutoFailoverPGWal * GetAutoFailoverPGWal(int64 policyId,
												int groupId,
												char *fileName);

extern AutoFailoverPGWal * AddAutoFailoverPGWal(int64 policyId,
												int groupId,
												int64 nodeId,
												char *fileName,
												int64 fileSize,
												char *md5);

extern bool FinishAutoFailoverPGWal(AutoFailoverPGWal *pgAutoFailoverPGWal);

extern bool UpdateAutoFailoverPGWalNode(AutoFailoverPGWal *pgAutoFailoverPGWal,
										int64 nodeId);

extern AutoFailoverPGWal * TupleToAutoFailoverPGWal(TupleDesc tupleDescriptor,
													HeapTuple heapTuple);
