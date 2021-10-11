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
 * GetAutoFailoverPGWal returns a single AutoFailoverPGWal entry identified by
 * a formation, group, and filename.
 */
AutoFailoverPGWal *
GetAutoFailoverPGWal(char *formationId, int groupId, char *fileName)
{
	AutoFailoverPGWal *pgAutoFailoverPGWal = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID,                    /* formationId */
		INT4OID,                    /* groupId */
		TEXTOID                     /* fileName */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationId */
		Int32GetDatum(groupId),           /* groupId */
		CStringGetTextDatum(fileName)     /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		SELECT_ALL_FROM_AUTO_FAILOVER_PG_WAL_TABLE
		" WHERE formationid = $1 and groupid = $2 and filename = $3";

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
 *
 * We use simple_heap_update instead of SPI to avoid recursing into triggers.
 */
AutoFailoverPGWal *
AddAutoFailoverPGWal(char *formationId,
					 int groupId,
					 int64 nodeId,
					 char *fileName,
					 int64 fileSize,
					 char *md5)
{
	AutoFailoverPGWal *pgAutoFailoverPGWal = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT4OID, /* groupid */
		INT8OID, /* nodeid */
		TEXTOID, /* filename */
		INT8OID, /* filesize */
		TEXTOID  /* md5 */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),   /* formationid */
		Int32GetDatum(groupId),             /* groupid */
		Int64GetDatum(nodeId),              /* nodeid */
		CStringGetTextDatum(fileName),      /* filename */
		Int64GetDatum(fileSize),            /* filesize */
		CStringGetTextDatum(md5)            /* md5 */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *insertQuery =
		"INSERT INTO pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		" (formationid, groupid, nodeid, filename, filesize, md5, start_time) "
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
		TEXTOID, /* formationid */
		INT4OID, /* groupid */
		TEXTOID  /* filename */
	};

	Datum argValues[] = {
		CStringGetTextDatum(pgAutoFailoverPGWal->formationId),   /* formationid */
		Int32GetDatum(pgAutoFailoverPGWal->groupId),             /* groupid */
		CStringGetTextDatum(pgAutoFailoverPGWal->fileName)      /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"   UPDATE pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		"      SET finish_time = now() "
		"    WHERE formationid = $1 and groupid = $2 and filename = $3 "
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
		TEXTOID, /* formationid */
		INT4OID, /* groupid */
		INT8OID, /* nodeid */
		TEXTOID  /* filename */
	};

	Datum argValues[] = {
		CStringGetTextDatum(pgAutoFailoverPGWal->formationId),   /* formationid */
		Int32GetDatum(pgAutoFailoverPGWal->groupId),             /* groupid */
		Int64GetDatum(nodeId),                                   /* nodeid */
		CStringGetTextDatum(pgAutoFailoverPGWal->fileName)       /* filename */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"   UPDATE pgautofailover." AUTO_FAILOVER_PG_WAL_TABLE_NAME
		"      SET nodeid = $3, start_time = now() "
		"    WHERE formationid = $1 and groupid = $2 and filename = $4 "
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

	Datum formationId =
		heap_getattr(heapTuple,
					 Anum_pgautofailover_pg_wal_formationid,
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

	pgAutoFailoverPGWal->formationId = TextDatumGetCString(formationId);
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
