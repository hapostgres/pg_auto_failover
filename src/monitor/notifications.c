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

#include "postgres.h"

#include "metadata.h"
#include "notifications.h"
#include "replication_state.h"

#include "catalog/pg_type.h"
#include "commands/async.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/pg_lsn.h"


/*
 * LogAndNotifyMessage emits the given message both as a log entry and also as
 * a notification on the CHANNEL_LOG channel.
 */
void
LogAndNotifyMessage(char *message, size_t size, const char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);

	/*
	 * Explanation of IGNORE-BANNED
	 * Arguments are always non-null and we
	 * do not write before the allocated buffer.
	 *
	 */
	n = vsnprintf(message, size - 2, fmt, args); /* IGNORE-BANNED */
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
NotifyStateChange(ReplicationState reportedState,
				  ReplicationState goalState,
				  const char *formationId,
				  int groupId,
				  int64 nodeId,
				  const char *nodeName,
				  const char *nodeHost,
				  int nodePort,
				  SyncState pgsrSyncState,
				  XLogRecPtr reportedLSN,
				  int candidatePriority,
				  bool replicationQuorum,
				  char *description)
{
	int64 eventid;
	StringInfo payload = makeStringInfo();

	/*
	 * Insert the event in our events table.
	 */
	eventid = InsertEvent(formationId, groupId, nodeId,
						  nodeName, nodeHost, nodePort,
						  reportedState, goalState, pgsrSyncState, reportedLSN,
						  candidatePriority, replicationQuorum, description);

	/*
	 * Rather than try and escape dots and colon characters from the user
	 * provided strings formationId and nodeName, we include the length of the
	 * string in the message. Parsing is then easier on the receiving side too.
	 */
	appendStringInfo(payload,
					 "S:%s:%s:%lu.%s:%d:%ld:%lu.%s:%lu.%s:%d",
					 ReplicationStateGetName(reportedState),
					 ReplicationStateGetName(goalState),
					 strlen(formationId),
					 formationId,
					 groupId,
					 nodeId,
					 strlen(nodeName),
					 nodeName,
					 strlen(nodeHost),
					 nodeHost,
					 nodePort);

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
InsertEvent(const char *formationId, int groupId, int64 nodeId,
			const char *nodeName, const char *nodeHost, int nodePort,
			ReplicationState reportedState,
			ReplicationState goalState,
			SyncState pgsrSyncState,
			XLogRecPtr reportedLSN,
			int candidatePriority,
			bool replicationQuorum,
			char *description)
{
	Oid goalStateOid = ReplicationStateGetEnum(goalState);
	Oid reportedStateOid = ReplicationStateGetEnum(reportedState);
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
		LSNOID,  /* reportedLSN */
		INT4OID, /* candidate_priority */
		BOOLOID, /* replication_quorum */
		TEXTOID  /* description */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),   /* formationid */
		Int64GetDatum(nodeId),              /* nodeid */
		Int32GetDatum(groupId),             /* groupid */
		CStringGetTextDatum(nodeName),      /* nodename */
		CStringGetTextDatum(nodeHost),      /* nodehost */
		Int32GetDatum(nodePort),            /* nodeport */
		ObjectIdGetDatum(reportedStateOid), /* reportedstate */
		ObjectIdGetDatum(goalStateOid),     /* goalstate */
		CStringGetTextDatum(SyncStateToString(pgsrSyncState)), /* sync_state */
		LSNGetDatum(reportedLSN),           /* reportedLSN */
		Int32GetDatum(candidatePriority),   /* candidate_priority */
		BoolGetDatum(replicationQuorum),    /* replication_quorum */
		CStringGetTextDatum(description)    /* description */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;
	int64 eventId = 0;

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_EVENT_TABLE
		"(formationid, nodeid, groupid, nodename, nodehost, nodeport,"
		" reportedstate, goalstate, reportedrepstate, reportedlsn, "
		" candidatepriority, replicationquorum, description) "
		"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13) "
		"RETURNING eventid";

	SPI_connect();

	spiStatus = SPI_execute_with_args(insertQuery, argCount, argTypes,
									  argValues, NULL, false, 0);

	if (spiStatus == SPI_OK_INSERT_RETURNING && SPI_processed > 0)
	{
		bool isNull = false;
		Datum eventIdDatum = 0;

		eventIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
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
