/*-------------------------------------------------------------------------
 *
 * src/monitor/archive_protocol.c
 *
 * Implementation of the functions used to communicate with PostgreSQL
 * nodes that are archiving WAL and PGDATA (base backups).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "access/xact.h"

#include "archive_metadata.h"
#include "metadata.h"
#include "notifications.h"

#include "access/htup_details.h"
#include "access/xlogdefs.h"
#include "catalog/pg_enum.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "parser/parse_type.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/pg_lsn.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/uuid.h"


/* GUC variables */
int ArchiveUpdateNodeTimeoutMs = 60 * 1000;


/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_archiver_policy);
PG_FUNCTION_INFO_V1(register_wal);
PG_FUNCTION_INFO_V1(finish_wal);


/*
 * register_archiver_policy registers an archiver policy for a given formation.
 */
Datum
register_archiver_policy(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	text *targetText = PG_GETARG_TEXT_P(1);
	char *target = text_to_cstring(targetText);

	text *methodText = PG_GETARG_TEXT_P(2);
	char *method = text_to_cstring(methodText);

	text *configText = PG_GETARG_TEXT_P(3);
	char *config = text_to_cstring(configText);

	Interval *backupInterval = PG_GETARG_INTERVAL_P(4);
	int backupMaxCount = PG_GETARG_INT32(5);
	Interval *backupMaxAge = PG_GETARG_INTERVAL_P(6);

	AutoFailoverArchiverPolicy *pgAutoFailoverPolicy =
		AddAutoFailoverArchiverPolicy(
			formationId,
			target,
			method,
			config,
			backupInterval,
			backupMaxCount,
			backupMaxAge);

	if (pgAutoFailoverPolicy == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("couldn't register an archiver policy for "
							   "formation %s and target %s",
							   formationId, target)));
	}

	PG_RETURN_INT64(pgAutoFailoverPolicy->policyId);
}


/*
 * register_wal registers a WAL filename into pgautofailover.pg_wal.
 *
 * Several Postgres nodes might be calling this command concurrently in the
 * context of the archive_command (pg_autoctl archive wal). In this
 * implementation we mke sure that only one of the callers gets to archive the
 * WAL, thanks to using the ON CONFLICT ON CONSTRAINT pkey DO NOTHING.
 *
 * When such a conflict happens, we take another transaction snapshot and
 * SELECT from the pgautofailover.pg_wal table to get the information about the
 * concurrent registration for the WAL; it will have a different node name to
 * it.
 *
 * Finally, when a node as registered itself to archive a particular WAL
 * segment but then couldn't finish the archiving within
 * ArchiveUpdateNodeTimeoutMs (defaults to 1 min), we re-assign the WAL to any
 * new node that calls the pgautofailover.register_wal() function.
 */
Datum
register_wal(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int64 policyId = PG_GETARG_INT64(0);
	int32 groupId = PG_GETARG_INT32(1);
	int64 nodeId = PG_GETARG_INT64(2);

	text *fileNameText = PG_GETARG_TEXT_P(3);
	char *fileName = text_to_cstring(fileNameText);

	int64 fileSize = PG_GETARG_INT64(4);

	text *md5Text = PG_GETARG_TEXT_P(5);
	char *md5 = text_to_cstring(md5Text);

	AutoFailoverPGWal *pgAutoFailoverPGWal =
		AddAutoFailoverPGWal(
			policyId,
			groupId,
			nodeId,
			fileName,
			fileSize,
			md5);

	if (pgAutoFailoverPGWal == NULL)
	{
		/*
		 * AddAutoFailoverPGWal uses an ON UPDATE DO NOTHING clause, so when we
		 * get a NULL from the function, we know we can SELECT the row for the
		 * WAL. Because the conflict might be with another INSERT that is still
		 * in-flight though, we want to grab a new snapthot.
		 */
		CommandCounterIncrement();

		PushActiveSnapshot(GetLatestSnapshot());

		pgAutoFailoverPGWal =
			GetAutoFailoverPGWal(policyId, groupId, fileName);

		if (pgAutoFailoverPGWal == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
							errmsg("couldn't register a pg_wal entry for "
								   "WAL %s for archiver_policy_id %lld "
								   "and group %d",
								   fileName,
								   (long long) policyId,
								   groupId)));
		}

		PopActiveSnapshot();
	}

	/*
	 * If we found a previous entry for another node, with a NULL finishTime
	 * and a startTime that's older than ArchiveUpdateNodeTimeoutMs, then we
	 * allow the current node calling register_wal to take over and proceed
	 * with the archiving.
	 */
	if (pgAutoFailoverPGWal->nodeId != nodeId &&
		pgAutoFailoverPGWal->finishTime == 0)
	{
		TimestampTz now = GetCurrentTimestamp();

		if (TimestampDifferenceExceeds(pgAutoFailoverPGWal->startTime,
									   now,
									   ArchiveUpdateNodeTimeoutMs))
		{
			UpdateAutoFailoverPGWalNode(pgAutoFailoverPGWal, nodeId);
		}
	}

	TupleDesc resultDescriptor = NULL;
	Datum values[8];
	bool isNulls[8];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int64GetDatum(pgAutoFailoverPGWal->policyId);
	values[1] = Int32GetDatum(pgAutoFailoverPGWal->groupId);
	values[2] = Int64GetDatum(pgAutoFailoverPGWal->nodeId);
	values[3] = CStringGetTextDatum(pgAutoFailoverPGWal->fileName);
	values[4] = Int64GetDatum(pgAutoFailoverPGWal->fileSize);
	values[5] = CStringGetTextDatum(pgAutoFailoverPGWal->md5);
	values[6] = TimestampTzGetDatum(pgAutoFailoverPGWal->startTime);

	if (pgAutoFailoverPGWal->finishTime == 0)
	{
		isNulls[7] = true;
	}
	else
	{
		values[7] = TimestampTzGetDatum(pgAutoFailoverPGWal->finishTime);
	}

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * finish_wal updates the pgautofailover.pg_wal.finish_time, marking the WAL as
 * successfully archived now.
 */
Datum
finish_wal(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int64 policyId = PG_GETARG_INT64(0);
	int32 groupId = PG_GETARG_INT32(1);

	text *fileNameText = PG_GETARG_TEXT_P(2);
	char *fileName = text_to_cstring(fileNameText);

	AutoFailoverPGWal *pgAutoFailoverPGWal =
		GetAutoFailoverPGWal(policyId, groupId, fileName);

	if (pgAutoFailoverPGWal == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
						errmsg("couldn't register a pg_wal entry for "
							   "WAL %s in archiver_policy_id %lld and group %d",
							   fileName,
							   (long long) policyId,
							   groupId)));
	}

	FinishAutoFailoverPGWal(pgAutoFailoverPGWal);

	TupleDesc resultDescriptor = NULL;
	Datum values[8];
	bool isNulls[8];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int64GetDatum(pgAutoFailoverPGWal->policyId);
	values[1] = Int32GetDatum(pgAutoFailoverPGWal->groupId);
	values[2] = Int64GetDatum(pgAutoFailoverPGWal->nodeId);
	values[3] = CStringGetTextDatum(pgAutoFailoverPGWal->fileName);
	values[4] = Int64GetDatum(pgAutoFailoverPGWal->fileSize);
	values[5] = CStringGetTextDatum(pgAutoFailoverPGWal->md5);
	values[6] = TimestampTzGetDatum(pgAutoFailoverPGWal->startTime);
	values[7] = TimestampTzGetDatum(pgAutoFailoverPGWal->finishTime);

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}
