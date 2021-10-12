/*-------------------------------------------------------------------------
 *
 * src/monitor/archive_metadata.c
 *
 * Implmentation of functions related to archive metadata.
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

#include "metadata.h"
#include "archive_metadata.h"
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


/*
 * AddAutoFailoverArchiverPolicy adds a new AutoFailoverArchiverPolicy entry to
 * the pgautofailover.archiver_policy table.
 */
AutoFailoverArchiverPolicy *
AddAutoFailoverArchiverPolicy(char *formationId,
							  char *target,
							  char *method,
							  char *config,
							  Interval *backupInterval,
							  int backupMaxCount,
							  Interval *backupMaxAge)
{
	Oid argTypes[] = {
		TEXTOID,                /* formationid */
		TEXTOID,                /* target */
		TEXTOID,                /* method */
		TEXTOID,                /* config */
		INTERVALOID,            /* backup_interval */
		INT4OID,                /* backup_max_count */
		INTERVALOID             /* backup_max_age */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),   /* formationid */
		CStringGetTextDatum(target),        /* target */
		CStringGetTextDatum(method),        /* method */
		CStringGetTextDatum(config),        /* config */
		IntervalPGetDatum(backupInterval),  /* backup_interval */
		Int32GetDatum(backupMaxCount),      /* backup_max_count */
		IntervalPGetDatum(backupMaxAge)     /* backup_max_age */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int64 archiverPolicyId = 0;

	const char *insertQuery =
		"INSERT INTO pgautofailover." AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_NAME
		" (formationid, target, method, config, "
		"  backup_interval, backup_max_count, backup_max_age) "
		" VALUES ($1, $2, $3, $4::jsonb, $5, $6, $7) "
		" RETURNING archiver_policy_id";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(insertQuery, argCount,
										  argTypes, argValues, NULL,
										  false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;

		Datum nodeIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc,
										  1,
										  &isNull);

		archiverPolicyId = DatumGetInt64(nodeIdDatum);
	}
	else
	{
		elog(ERROR, "could not insert into "
			 AUTO_FAILOVER_ARCHIVER_POLICY_TABLE_NAME);
	}

	SPI_finish();

	AutoFailoverArchiverPolicy *pgAutoFailoverPolicy =
		(AutoFailoverArchiverPolicy *) palloc0(sizeof(AutoFailoverArchiverPolicy));

	pgAutoFailoverPolicy->policyId = archiverPolicyId;
	pgAutoFailoverPolicy->formationId = formationId;
	pgAutoFailoverPolicy->target = target;
	pgAutoFailoverPolicy->method = method;
	pgAutoFailoverPolicy->config = config;
	pgAutoFailoverPolicy->backupInterval = backupInterval;
	pgAutoFailoverPolicy->backupMaxCount = backupMaxCount;
	pgAutoFailoverPolicy->backupMaxAge = backupMaxAge;

	return pgAutoFailoverPolicy;
}


/*
 * GetAutoFailoverPGWal returns a single AutoFailoverPGWal entry identified by
 * a formation, group, and filename.
 */
AutoFailoverPGWal *
GetAutoFailoverPGWal(int64 policyId, int groupId, char *fileName)
{
	AutoFailoverPGWal *pgAutoFailoverPGWal = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		INT8OID,                    /* policyId */
		INT4OID,                    /* groupId */
		TEXTOID                     /* fileName */
	};

	Datum argValues[] = {
		Int64GetDatum(policyId),      /* policyId */
		Int32GetDatum(groupId),           /* groupId */
		CStringGetTextDatum(fileName)     /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_PG_WAL_TABLE
		" WHERE archiver_policy_id = $1 and groupid = $2 and filename = $3";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 1);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_PG_WAL_TABLE_NAME);
	}

	if (SPI_processed > 0)
	{
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);
		pgAutoFailoverPGWal = TupleToAutoFailoverPGWal(SPI_tuptable->tupdesc,
													   SPI_tuptable->vals[0]);
		MemoryContextSwitchTo(spiContext);
	}
	else
	{
		pgAutoFailoverPGWal = NULL;
	}

	SPI_finish();

	return pgAutoFailoverPGWal;
}


/*
 * AddAutoFailoverPGWal adds a new AutoFailoverPGWal to pgautofailover.pg_wal
 * with the given properties.
 */
AutoFailoverPGWal *
AddAutoFailoverPGWal(int64 policyId,
					 int groupId,
					 int64 nodeId,
					 char *fileName,
					 int64 fileSize,
					 char *md5)
{
	AutoFailoverPGWal *pgAutoFailoverPGWal = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		INT8OID, /* policyId */
		INT4OID, /* groupid */
		INT8OID, /* nodeid */
		TEXTOID, /* filename */
		INT8OID, /* filesize */
		TEXTOID  /* md5 */
	};

	Datum argValues[] = {
		Int64GetDatum(policyId),       /* policyId */
		Int32GetDatum(groupId),             /* groupid */
		Int64GetDatum(nodeId),              /* nodeid */
		CStringGetTextDatum(fileName),      /* filename */
		Int64GetDatum(fileSize),            /* filesize */
		CStringGetTextDatum(md5)            /* md5 */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *insertQuery =
		"INSERT INTO pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		" (archiver_policy_id, groupid, nodeid, filename, "
		"  filesize, md5, start_time) "
		" VALUES ($1, $2, $3, $4, $5, $6::uuid, now()) "
		" ON CONFLICT ON CONSTRAINT pg_wal_pkey DO NOTHING "
		" RETURNING " AUTO_FAILOVER_PG_WAL_TABLE_ALL_COLUMNS;

	SPI_connect();

	int spiStatus = SPI_execute_with_args(insertQuery, argCount,
										  argTypes, argValues, NULL,
										  false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);
		pgAutoFailoverPGWal = TupleToAutoFailoverPGWal(SPI_tuptable->tupdesc,
													   SPI_tuptable->vals[0]);
		MemoryContextSwitchTo(spiContext);
	}
	else
	{
		pgAutoFailoverPGWal = NULL;
	}

	SPI_finish();

	return pgAutoFailoverPGWal;
}


/*
 * FinishAutoFailoverPGWAL updates the pgAutoFailoverPGWal finish_time column
 * to the current time, and updates the structure to reflect that information.
 */
bool
FinishAutoFailoverPGWal(AutoFailoverPGWal *pgAutoFailoverPGWal)
{
	Oid argTypes[] = {
		INT8OID, /* policyId */
		INT4OID, /* groupid */
		TEXTOID  /* filename */
	};

	Datum argValues[] = {
		Int64GetDatum(pgAutoFailoverPGWal->policyId),      /* policyid */
		Int32GetDatum(pgAutoFailoverPGWal->groupId),             /* groupid */
		CStringGetTextDatum(pgAutoFailoverPGWal->fileName)      /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"   UPDATE pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		"      SET finish_time = now() "
		"    WHERE archiver_policy_id = $1 and groupid = $2 and filename = $3 "
		"RETURNING finish_time";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);

	if (spiStatus == SPI_OK_UPDATE_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;

		Datum finishTimeDatum = SPI_getbinval(SPI_tuptable->vals[0],
											  SPI_tuptable->tupdesc,
											  1,
											  &isNull);

		if (isNull)
		{
			elog(ERROR, "could not update " AUTO_FAILOVER_PG_WAL_TABLE_NAME);
		}

		pgAutoFailoverPGWal->finishTime = DatumGetTimestampTz(finishTimeDatum);
	}
	else
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_PG_WAL_TABLE_NAME);
	}

	SPI_finish();

	return true;
}


/*
 * UpdateAutoFailoverPGWalNode updates the nodeid and the startTime of the
 * given pgautofailover.pg_wal record, allowing another node to take over the
 * archiving of a WAL.
 */
bool
UpdateAutoFailoverPGWalNode(AutoFailoverPGWal *pgAutoFailoverPGWal, int64 nodeId)
{
	Oid argTypes[] = {
		INT8OID, /* policyid */
		INT4OID, /* groupid */
		INT8OID, /* nodeid */
		TEXTOID  /* filename */
	};

	Datum argValues[] = {
		Int64GetDatum(pgAutoFailoverPGWal->policyId),      /* policyid */
		Int32GetDatum(pgAutoFailoverPGWal->groupId),             /* groupid */
		Int64GetDatum(nodeId),                                   /* nodeid */
		CStringGetTextDatum(pgAutoFailoverPGWal->fileName)       /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"   UPDATE pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		"      SET nodeid = $3, start_time = now() "
		"    WHERE archiver_policy_id = $1 and groupid = $2 and filename = $4 "
		"RETURNING start_time";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);

	if (spiStatus == SPI_OK_UPDATE_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;

		Datum startTimeDatum = SPI_getbinval(SPI_tuptable->vals[0],
											 SPI_tuptable->tupdesc,
											 1,
											 &isNull);

		if (isNull)
		{
			elog(ERROR, "could not update " AUTO_FAILOVER_PG_WAL_TABLE_NAME);
		}

		pgAutoFailoverPGWal->nodeId = nodeId;
		pgAutoFailoverPGWal->startTime = DatumGetTimestampTz(startTimeDatum);
	}
	else
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_PG_WAL_TABLE_NAME);
	}

	SPI_finish();

	return true;
}


/*
 * TupleToAutoFailoverNode constructs a AutoFailoverNode from a heap tuple.
 */
AutoFailoverPGWal *
TupleToAutoFailoverPGWal(TupleDesc tupleDescriptor, HeapTuple heapTuple)
{
	bool isNull = false;
	bool finishTimeIsNull = false;

	Datum archiver_policy_id =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_archiver_policy_id,
					 tupleDescriptor, &isNull);

	Datum groupId =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_groupid,
					 tupleDescriptor, &isNull);

	Datum nodeId =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_nodeid,
					 tupleDescriptor, &isNull);

	Datum fileName =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_filename,
					 tupleDescriptor, &isNull);

	Datum fileSize =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_filesize,
					 tupleDescriptor, &isNull);

	Datum md5 =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_md5,
					 tupleDescriptor, &isNull);

	Datum startTime =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_start_time,
					 tupleDescriptor, &isNull);

	Datum finishTime =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_finish_time,
					 tupleDescriptor, &finishTimeIsNull);

	AutoFailoverPGWal *pgAutoFailoverPGWal =
		(AutoFailoverPGWal *) palloc0(sizeof(AutoFailoverPGWal));

	pgAutoFailoverPGWal->policyId = DatumGetInt64(archiver_policy_id);
	pgAutoFailoverPGWal->groupId = DatumGetInt32(groupId);
	pgAutoFailoverPGWal->nodeId = DatumGetInt64(nodeId);
	pgAutoFailoverPGWal->fileName = TextDatumGetCString(fileName);
	pgAutoFailoverPGWal->fileSize = DatumGetInt64(fileSize);
	pgAutoFailoverPGWal->md5 = TextDatumGetCString(md5);
	pgAutoFailoverPGWal->startTime = DatumGetTimestampTz(startTime);

	pgAutoFailoverPGWal->finishTime =
		finishTimeIsNull ? 0 : DatumGetTimestampTz(finishTime);

	return pgAutoFailoverPGWal;
}
