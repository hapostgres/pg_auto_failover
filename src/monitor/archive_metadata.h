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

/* column indexes for pgautofailover.node
 * indices must match with the columns given
 * in the following definition.
 */
#define Natts_pgautofailover_pg_wal 8
#define Anum_pgautofailover_pg_wal_formationid 1
#define Anum_pgautofailover_pg_wal_groupid 2
#define Anum_pgautofailover_pg_wal_nodeid 3
#define Anum_pgautofailover_pg_wal_filename 4
#define Anum_pgautofailover_pg_wal_filesize 5
#define Anum_pgautofailover_pg_wal_md5 6
#define Anum_pgautofailover_pg_wal_start_time 7
#define Anum_pgautofailover_pg_wal_finish_time 8

#define AUTO_FAILOVER_PG_WAL_TABLE_ALL_COLUMNS \
	"formationid, " \
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
	char *formationId;
	int groupId;
	int64 nodeId;
	char *fileName;
	int64 fileSize;
	char *md5;
	TimestampTz startTime;
	TimestampTz finishTime;
} AutoFailoverPGWal;

extern AutoFailoverPGWal * GetAutoFailoverPGWal(char *formationId,
												int groupId,
												char *fileName);

extern AutoFailoverPGWal * AddAutoFailoverPGWal(char *formationId,
												int groupId,
												int64 nodeId,
												char *fileName,
												int64 fileSize,
												char *md5);

extern AutoFailoverPGWal * TupleToAutoFailoverPGWal(TupleDesc tupleDescriptor,
													HeapTuple heapTuple);
