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
#include "notifications.h"

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
#define TLIST_NUM_NODE_ID 1
#define TLIST_NUM_NODE_NAME 2
#define TLIST_NUM_NODE_HOST 3
#define TLIST_NUM_NODE_PORT 4
#define TLIST_NUM_HEALTH_STATUS 5


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
						 "SELECT nodeid, nodename, nodehost, nodeport, health "
						 "FROM " AUTO_FAILOVER_NODE_TABLE);

		pgstat_report_activity(STATE_RUNNING, query.data);

		spiStatus = SPI_execute(query.data, false, 0);

		/*
		 * When we start the monitor during an upgrade (from 1.3 to 1.4), the
		 * background worker might be reading the 1.3 pgautofailover catalogs
		 * still, where the "nodehost" column does not exist.
		 */
		if (spiStatus != SPI_OK_SELECT)
		{
			EndSPITransaction();
			return NIL;
		}

		oldContext = MemoryContextSwitchTo(upperContext);

		for (uint64 rowNumber = 0; rowNumber < SPI_processed; rowNumber++)
		{
			HeapTuple heapTuple = SPI_tuptable->vals[rowNumber];
			NodeHealth *nodeHealth =
				TupleToNodeHealth(heapTuple, SPI_tuptable->tupdesc);
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

	bool extensionLoaded = extensionPresent && extensionScriptExecuted;

	return extensionLoaded;
}


/*
 * TupleToNodeHealth constructs a node health description from a heap tuple obtained
 * via SPI.
 */
NodeHealth *
TupleToNodeHealth(HeapTuple heapTuple, TupleDesc tupleDescriptor)
{
	bool isNull = false;

	Datum nodeIdDatum = SPI_getbinval(heapTuple, tupleDescriptor,
									  TLIST_NUM_NODE_ID, &isNull);
	Datum nodeNameDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										TLIST_NUM_NODE_NAME, &isNull);
	Datum nodeHostDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										TLIST_NUM_NODE_HOST, &isNull);
	Datum nodePortDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										TLIST_NUM_NODE_PORT, &isNull);
	Datum healthStateDatum = SPI_getbinval(heapTuple, tupleDescriptor,
										   TLIST_NUM_HEALTH_STATUS, &isNull);

	NodeHealth *nodeHealth = palloc0(sizeof(NodeHealth));
	nodeHealth->nodeId = DatumGetInt64(nodeIdDatum);
	nodeHealth->nodeName = TextDatumGetCString(nodeNameDatum);
	nodeHealth->nodeHost = TextDatumGetCString(nodeHostDatum);
	nodeHealth->nodePort = DatumGetInt32(nodePortDatum);
	nodeHealth->healthState = DatumGetInt32(healthStateDatum);

	return nodeHealth;
}


/*
 * SetNodeHealthState updates the health state of a node in the metadata.
 */
void
SetNodeHealthState(int64 nodeId,
				   char *nodeName,
				   char *nodeHost,
				   uint16 nodePort,
				   int previousHealthState,
				   int healthState)
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
						 " WHERE nodeid = %lld "
						 "   AND nodehost = %s AND nodeport = %d "
						 " RETURNING node.*",
						 healthState,
						 (long long) nodeId,
						 quote_literal_cstr(nodeHost),
						 nodePort);

		pgstat_report_activity(STATE_RUNNING, query.data);

		spiStatus = SPI_execute(query.data, false, 0);
		Assert(spiStatus == SPI_OK_UPDATE_RETURNING);

		/*
		 * We should have 0 or 1 row impacted, because of pkey on nodeid. We
		 * might have updated zero rows when a node is concurrently being
		 * DELETEd, because of the default REPETEABLE READ isolation level.
		 */
		if (SPI_processed == 1)
		{
			if (healthState != previousHealthState)
			{
				HeapTuple heapTuple = SPI_tuptable->vals[0];
				AutoFailoverNode *pgAutoFailoverNode =
					TupleToAutoFailoverNode(SPI_tuptable->tupdesc, heapTuple);

				char message[BUFSIZE] = { 0 };

				LogAndNotifyMessage(message, sizeof(message),
									"Node " NODE_FORMAT
									" is marked as %s by the monitor",
									NODE_FORMAT_ARGS(pgAutoFailoverNode),
									healthState == 0 ? "unhealthy" : "healthy");

				NotifyStateChange(pgAutoFailoverNode, message);
			}
		}
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


/*
 * NodeHealthToString returns a string representation of the given node health
 * enum value.
 */
char *
NodeHealthToString(NodeHealthState health)
{
	switch (health)
	{
		case NODE_HEALTH_UNKNOWN:
		{
			return "unknown";
		}

		case NODE_HEALTH_BAD:
		{
			return "bad";
		}

		case NODE_HEALTH_GOOD:
		{
			return "good";
		}

		default:
		{
			/* shouldn't happen */
			ereport(ERROR, (errmsg("BUG: health is %d", health)));
			return "unknown";
		}
	}
}
