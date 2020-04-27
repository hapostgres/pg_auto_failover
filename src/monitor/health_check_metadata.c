/*-------------------------------------------------------------------------
 *
 * src/monitor/health_check_metadata.c
 *
 * Implementation of functions related to health check metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"

#include "health_check.h"
#include "metadata.h"

#include "access/htup.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "commands/extension.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "pgstat.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/snapmgr.h"


/* human-readable names for addressing columns of health check queries */
#define TLIST_NUM_NODE_NAME 1
#define TLIST_NUM_NODE_PORT 2
#define TLIST_NUM_HEALTH_STATUS 3


/* GUCs */
bool HealthChecksEnabled = true;


static bool HaMonitorHasBeenLoaded(void);
static void StartSPITransaction(void);
static void EndSPITransaction(void);


/*
 * LoadNodeHealthList loads a list of nodes of which to check the health.
 */
List *
LoadNodeHealthList(void)
{
	List *nodeHealthList = NIL;
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;
	StringInfoData query;

	MemoryContext upperContext = CurrentMemoryContext, oldContext = NULL;

	if (!HealthChecksEnabled)
	{
		return NIL;
	}

	StartSPITransaction();

	if (HaMonitorHasBeenLoaded())
	{
		initStringInfo(&query);
		appendStringInfo(&query,
						 "SELECT nodehost, nodeport, health "
						 "FROM " AUTO_FAILOVER_NODE_TABLE);

		pgstat_report_activity(STATE_RUNNING, query.data);

		spiStatus = SPI_execute(query.data, false, 0);
		Assert(spiStatus == SPI_OK_SELECT);

		oldContext = MemoryContextSwitchTo(upperContext);

		for (uint64 rowNumber = 0; rowNumber < SPI_processed; rowNumber++)
		{
			HeapTuple heapTuple = SPI_tuptable->vals[rowNumber];
			NodeHealth *nodeHealth = TupleToNodeHealth(heapTuple,
													   SPI_tuptable->tupdesc);
			nodeHealthList = lappend(nodeHealthList, nodeHealth);
		}

		MemoryContextSwitchTo(oldContext);
	}

	EndSPITransaction();

	MemoryContextSwitchTo(upperContext);

	return nodeHealthList;
}


/*
 * HaMonitorHasBeenLoaded returns true if the extension has been created
 * in the current database and the extension script has been executed. Otherwise,
 * it returns false. The result is cached as this is called very frequently.
 */
static bool
HaMonitorHasBeenLoaded(void)
{
	bool extensionLoaded = false;
	bool extensionPresent = false;
	bool extensionScriptExecuted = true;

	Oid extensionOid = get_extension_oid(AUTO_FAILOVER_EXTENSION_NAME, true);
	if (extensionOid != InvalidOid)
	{
		extensionPresent = true;
	}

	if (extensionPresent)
	{
		/* check if pg_cron extension objects are still being created */
		if (creating_extension && CurrentExtensionObject == extensionOid)
		{
			extensionScriptExecuted = false;
		}
		else if (IsBinaryUpgrade)
		{
			extensionScriptExecuted = false;
		}
	}

	extensionLoaded = extensionPresent && extensionScriptExecuted;

	return extensionLoaded;
}


/*
 * TupleToNodeHealth constructs a node health description from a heap tuple obtained
 * via SPI.
 */
NodeHealth *
TupleToNodeHealth(HeapTuple heapTuple, TupleDesc tupleDescriptor)
{
	NodeHealth *nodeHealth = NULL;
	bool isNull = false;

	Datum nodeHostDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										TLIST_NUM_NODE_NAME, &isNull);
	Datum nodePortDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										TLIST_NUM_NODE_PORT, &isNull);
	Datum healthStateDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										   TLIST_NUM_HEALTH_STATUS, &isNull);

	nodeHealth = palloc0(sizeof(NodeHealth));
	nodeHealth->nodeHost = TextDatumGetCString(nodeHostDatum);
	nodeHealth->nodePort = DatumGetInt32(nodePortDatum);
	nodeHealth->healthState = DatumGetInt32(healthStateDatum);

	return nodeHealth;
}


/*
 * SetNodeHealthState updates the health state of a node in the metadata.
 */
void
SetNodeHealthState(char *nodeHost, uint16 nodePort, int healthState)
{
	StringInfoData query;
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;
	MemoryContext upperContext = CurrentMemoryContext;

	StartSPITransaction();

	if (HaMonitorHasBeenLoaded())
	{
		initStringInfo(&query);
		appendStringInfo(&query,
						 "UPDATE " AUTO_FAILOVER_NODE_TABLE
						 "   SET health = %d, healthchecktime = now() "
						 " WHERE nodehost = %s AND nodeport = %d",
						 healthState,
						 quote_literal_cstr(nodeHost),
						 nodePort);

		pgstat_report_activity(STATE_RUNNING, query.data);

		spiStatus = SPI_execute(query.data, false, 0);
		Assert(spiStatus == SPI_OK_UPDATE);
	}
	else
	{
		/* extension has been dropped, just skip the update */
	}

	EndSPITransaction();

	MemoryContextSwitchTo(upperContext);
}


/*
 * StartSPITransaction starts a transaction using SPI.
 */
static void
StartSPITransaction(void)
{
	SetCurrentStatementStartTimestamp();
	StartTransactionCommand();
	SPI_connect();
	PushActiveSnapshot(GetTransactionSnapshot());
}


/*
 * EndSPITransaction finishes a transaction that was started using SPI.
 */
static void
EndSPITransaction(void)
{
	pgstat_report_activity(STATE_IDLE, NULL);
	SPI_finish();
	PopActiveSnapshot();
	CommitTransactionCommand();
}
