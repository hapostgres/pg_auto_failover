/*-------------------------------------------------------------------------
 *
 * src/monitor/archiver_protocol.c
 *
 * Implementation of the functions used to communicate with PostgreSQL
 * nodes.
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

#include "archiver_metadata.h"
#include "metadata.h"
#include "notifications.h"

#include "access/htup_details.h"
#include "access/xlogdefs.h"
#include "utils/builtins.h"

/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_archiver);

/*
 * register_node adds a node to a given formation
 *
 * At register time the monitor connects to the node to check that nodehost and
 * nodeport are valid, and it does a SELECT pg_is_in_recovery() to help decide
 * what initial role to attribute the entering node.
 */
Datum
register_archiver(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);

	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);

	/*
	 * We want to benefit from the STRICT aspect of the function declaration at
	 * the SQL level to bypass checking all the arguments one after the other,
	 * but at the same time we don't want to force a name to be given, we can
	 * assign a default name to an archiver ("archiver_%d").
	 *
	 * Omitting the nodename argument is the same as passing in an explicit
	 * empty string (''), and results in the code here assigning a default
	 * archiver name.
	 */
	int archiverId =
		AddArchiver(
			strcmp(nodeName, "") == 0 ? NULL : nodeName,
			nodeHost);

	AutoFailoverArchiver *archiver = GetArchiver(archiverId);

	if (archiver == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("archiver host \"%s\" with name \"%s\" could "
						"not be registered: "
						"could not get information for node that was inserted",
						nodeHost, nodeName)));
	}
	else
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Registering archiver %d \"%s\" (\"%s\")",
			archiver->archiverId, archiver->nodeName, archiver->nodeHost);
	}

	TupleDesc resultDescriptor = NULL;
	Datum values[3];
	bool isNulls[3];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(archiver->archiverId);
	values[1] = CStringGetTextDatum(archiver->nodeName);
	values[2] = CStringGetTextDatum(archiver->nodeHost);

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
