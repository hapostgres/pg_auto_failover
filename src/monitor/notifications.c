/*-------------------------------------------------------------------------
 *
 * src/monitor/notifications.c
 *
 * Implementation of the functions used to send messages to the
 * pg_auto_failover monitor clients.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include <inttypes.h>

#include "postgres.h"

#include "metadata.h"
#include "node_metadata.h"
#include "notifications.h"
#include "replication_state.h"

#include "catalog/pg_type.h"
#include "commands/async.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/json.h"
#include "utils/pg_lsn.h"


/*
 * LogAndNotifyMessage emits the given message both as a log entry and also as
 * a notification on the CHANNEL_LOG channel.
 */
void
LogAndNotifyMessage(char *message, size_t size, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	/*
	 * Explanation of IGNORE-BANNED
	 * Arguments are always non-null and we
	 * do not write before the allocated buffer.
	 *
	 */
	int n = vsnprintf(message, size - 2, fmt, args); /* IGNORE-BANNED */
	va_end(args);

	if (n < 0)
	{
		ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
						errmsg("out of memory")));
	}

	ereport(LOG, (errmsg("%s", message)));
	Async_Notify(CHANNEL_LOG, message);
}


/*
 * NotifyStateChange emits a notification message on the CHANNEL_STATE channel
 * about a state change decided by the monitor. This state change is encoded so
 * as to be easy to parse by a machine.
 */
int64
NotifyStateChange(AutoFailoverNode *node, char *description)
{
	StringInfo payload = makeStringInfo();

	/*
	 * Insert the event in our events table.
	 */
	int64 eventid = InsertEvent(node, description);

	/* build a json object from the notification pieces */
	appendStringInfoChar(payload, '{');

	appendStringInfo(payload, "\"type\": \"state\"");

	appendStringInfo(payload, ", \"formation\": ");
	escape_json(payload, node->formationId);

	appendStringInfo(payload, ", \"groupId\": %d", node->groupId);
	appendStringInfo(payload, ", \"nodeId\": %lld", (long long) node->nodeId);

	appendStringInfo(payload, ", \"name\": ");
	escape_json(payload, node->nodeName);

	appendStringInfo(payload, ", \"host\": ");
	escape_json(payload, node->nodeHost);

	appendStringInfo(payload, ", \"port\": %d", node->nodePort);

	appendStringInfo(payload, ", \"reportedState\": ");
	escape_json(payload, ReplicationStateGetName(node->reportedState));

	appendStringInfo(payload, ", \"goalState\": ");
	escape_json(payload, ReplicationStateGetName(node->goalState));

	appendStringInfo(payload, ", \"health\":");
	escape_json(payload, NodeHealthToString(node->health));

	appendStringInfoChar(payload, '}');

	Async_Notify(CHANNEL_STATE, payload->data);

	pfree(payload->data);
	pfree(payload);
	return eventid;
}


/*
 * InsertEvent populates the monitor's pgautofailover.event table with a new
 * entry, and returns the id of the new event.
 */
int64
InsertEvent(AutoFailoverNode *node, char *description)
{
	Oid goalStateOid = ReplicationStateGetEnum(node->goalState);
	Oid reportedStateOid = ReplicationStateGetEnum(node->reportedState);
	Oid replicationStateTypeOid = ReplicationStateTypeOid();

	Oid argTypes[] = {
		TEXTOID, /* formationid */
		INT8OID, /* nodeid */
		INT4OID, /* groupid */
		TEXTOID, /* nodename */
		TEXTOID, /* nodehost */
		INT4OID, /* nodeport */
		replicationStateTypeOid, /* reportedstate */
		replicationStateTypeOid, /* goalstate */
		TEXTOID, /* pg_stat_replication.sync_state */
		INT4OID, /* timeline_id */
		LSNOID,  /* reportedLSN */
		INT4OID, /* candidate_priority */
		BOOLOID, /* replication_quorum */
		TEXTOID  /* description */
	};

	Datum argValues[] = {
		CStringGetTextDatum(node->formationId),   /* formationid */
		Int64GetDatum(node->nodeId),              /* nodeid */
		Int32GetDatum(node->groupId),             /* groupid */
		CStringGetTextDatum(node->nodeName),      /* nodename */
		CStringGetTextDatum(node->nodeHost),      /* nodehost */
		Int32GetDatum(node->nodePort),            /* nodeport */
		ObjectIdGetDatum(reportedStateOid), /* reportedstate */
		ObjectIdGetDatum(goalStateOid),     /* goalstate */
		CStringGetTextDatum(SyncStateToString(node->pgsrSyncState)), /* sync_state */
		Int32GetDatum(node->reportedTLI),         /* reportedTLI */
		LSNGetDatum(node->reportedLSN),           /* reportedLSN */
		Int32GetDatum(node->candidatePriority),   /* candidate_priority */
		BoolGetDatum(node->replicationQuorum),    /* replication_quorum */
		CStringGetTextDatum(description)          /* description */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int64 eventId = 0;

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_EVENT_TABLE
		"(formationid, nodeid, groupid, nodename, nodehost, nodeport,"
		" reportedstate, goalstate, reportedrepstate, reportedtli, reportedlsn,"
		" candidatepriority, replicationquorum, description) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14) "
		"RETURNING eventid";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(insertQuery, argCount, argTypes,
										  argValues, NULL, false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;

		Datum eventIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
										   SPI_tuptable->tupdesc,
										   1,
										   &isNull);

		eventId = DatumGetInt64(eventIdDatum);
	}
	else
	{
		elog(ERROR, "could not insert into " AUTO_FAILOVER_EVENT_TABLE);
	}

	SPI_finish();

	return eventId;
}
