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


/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_wal);

Datum
register_wal(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	int32 groupId = PG_GETARG_INT32(1);
	int64 nodeId = PG_GETARG_INT64(2);

	text *fileNameText = PG_GETARG_TEXT_P(3);
	char *fileName = text_to_cstring(fileNameText);

	int64 fileSize = PG_GETARG_INT64(4);

	text *md5Text = PG_GETARG_TEXT_P(5);
	char *md5 = text_to_cstring(md5Text);

	AutoFailoverPGWal *pgAutoFailoverPGWal =
		AddAutoFailoverPGWal(
			formationId,
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
			GetAutoFailoverPGWal(formationId, groupId, fileName);

		if (pgAutoFailoverPGWal == NULL)
		{
			ereport(ERROR, (errcode(ERRCODE_UNDEFINED_OBJECT),
							errmsg("couldn't register a pg_wal entry for "
								   "WAL %s in formation %s and group %d",
								   fileName,
								   formationId,
								   groupId)));
		}

		PopActiveSnapshot();
	}

	TupleDesc resultDescriptor = NULL;
	Datum values[8];
	bool isNulls[8];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(pgAutoFailoverPGWal->formationId);
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
