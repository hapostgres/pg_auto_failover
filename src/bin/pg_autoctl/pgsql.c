/*
 * src/bin/pg_autoctl/pgsql.c
 *	 API for sending SQL commands to a PostgreSQL server
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"
#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "defaults.h"
#include "log.h"
#include "parsing.h"
#include "pgsql.h"
#include "signals.h"
#include "string_utils.h"


#define ERRCODE_DUPLICATE_OBJECT "42710"
#define ERRCODE_DUPLICATE_DATABASE "42P04"


static void pgAutoCtlDefaultNoticeProcessor(void *arg, const char *message);
static void pgAutoCtlDebugNoticeProcessor(void *arg, const char *message);
static PGconn * pgsql_open_connection(PGSQL *pgsql);
static PGconn * pgsql_retry_open_connection(PGSQL *pgsql);
static bool is_response_ok(PGresult *result);
static bool clear_results(PGconn *connection);
static bool pgsql_alter_system_set(PGSQL *pgsql, GUC setting);
static bool pgsql_get_current_setting(PGSQL *pgsql, char *settingName,
									  char **currentValue);
static void parsePgMetadata(void *ctx, PGresult *result);
static void parseReplicationSlotMaintain(void *ctx, PGresult *result);


/*
 * parseSingleValueResult is a ParsePostgresResultCB callback that reads the
 * first column of the first row of the resultset only, and parses the answer
 * into the expected C value, one of type QueryResultType.
 */
void
parseSingleValueResult(void *ctx, PGresult *result)
{
	SingleValueResultContext *context = (SingleValueResultContext *) ctx;

	if (PQntuples(result) == 1)
	{
		char *value = PQgetvalue(result, 0, 0);

		switch (context->resultType)
		{
			case PGSQL_RESULT_BOOL:
			{
				context->boolVal = strcmp(value, "t") == 0;
				context->parsedOk = true;
				break;
			}

			case PGSQL_RESULT_INT:
			{
				if (!stringToInt(value, &context->intVal))
				{
					context->parsedOk = false;
					log_error("Failed to parse int result \"%s\"", value);
				}
				context->parsedOk = true;
				break;
			}

			case PGSQL_RESULT_BIGINT:
			{
				if (!stringToUInt64(value, &context->bigint))
				{
					context->parsedOk = false;
					log_error("Failed to parse uint64_t result \"%s\"", value);
				}
				context->parsedOk = true;
				break;
			}

			case PGSQL_RESULT_STRING:
			{
				context->strVal = strdup(value);
				context->parsedOk = true;
				break;
			}
		}
	}
}


/*
 * pgsql_init initialises a PGSQL struct to connect to the given database
 * URL or connection string.
 */
bool
pgsql_init(PGSQL *pgsql, char *url, ConnectionType connectionType)
{
	pgsql->connectionType = connectionType;
	pgsql->connection = NULL;

	if (validate_connection_string(url))
	{
		/* size of url has already been validated. */
		strlcpy(pgsql->connectionString, url, MAXCONNINFO);
	}
	else
	{
		return false;
	}
	return true;
}


/*
 * Finish a PGSQL client connection.
 */
void
pgsql_finish(PGSQL *pgsql)
{
	if (pgsql->connection != NULL)
	{
		log_debug("Disconnecting from \"%s\"", pgsql->connectionString);
		PQfinish(pgsql->connection);
		pgsql->connection = NULL;
	}
}


/*
 * pgsql_open_connection opens a PostgreSQL connection, given a PGSQL client
 * instance. If a connection is already open in the client (it's not NULL),
 * then pgsql_open_connection reuses it and returns it immediately.
 */
static PGconn *
pgsql_open_connection(PGSQL *pgsql)
{
	PGconn *connection = NULL;

	/* we might be connected already */
	if (pgsql->connection != NULL)
	{
		return pgsql->connection;
	}

	log_debug("Connecting to \"%s\"", pgsql->connectionString);

	/* Make a connection to the database */
	connection = PQconnectdb(pgsql->connectionString);

	/* Check to see that the backend connection was successfully made */
	if (PQstatus(connection) != CONNECTION_OK)
	{
		/* We failed to connect to Postgres. When connecting to a local node,
		 * we are going to handle the situation by probing the postmasted.pid
		 * file. When connecting to a remove node (monitor or coordinator), we
		 * want to implement a retry loop: it might be a transient failure,
		 * such as when the remote node is not ready yet.
		 */
		if (pgsql->connectFailFast)
		{
			log_error("Connection to database failed: %s",
					  PQerrorMessage(connection));
			pgsql_finish(pgsql);
			return NULL;
		}
		switch (pgsql->connectionType)
		{
			case PGSQL_CONN_LOCAL:
			{
				char *message = PQerrorMessage(connection);
				char *errorLines[BUFSIZE];
				int lineCount = splitLines(message, errorLines, BUFSIZE);
				int lineNumber = 0;

				for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
				{
					log_error("Connection to database failed: %s",
							  errorLines[lineNumber]);
				}

				pgsql_finish(pgsql);
				return NULL;
			}

			case PGSQL_CONN_MONITOR:
			case PGSQL_CONN_COORDINATOR:
			{
				/* call into the retry loop logic */
				connection = pgsql_retry_open_connection(pgsql);

				if (connection == NULL)
				{
					/* errors have already been logged */
					return NULL;
				}
				break;
			}

			default:
			{
				/* should never happen */
				log_error("BUG: unknown connection type %d",
						  pgsql->connectionType);
				return NULL;
			}
		}
	}

	pgsql->connection = connection;

	/* set the libpq notice receiver to integrate notifications as warnings. */
	PQsetNoticeProcessor(connection, &pgAutoCtlDefaultNoticeProcessor, NULL);

	return connection;
}


/*
 * pgsql_retry_open_connection loops over a PQping call until the remote server
 * is ready to accept connections, and then connects to it and returns true
 * when it could connect, false otherwise.
 */
static PGconn *
pgsql_retry_open_connection(PGSQL *pgsql)
{
	PGconn *connection = NULL;
	int attempts = 0;
	bool retry = true;
	bool connectionOk = false;
	uint64_t startTime = time(NULL);

	log_warn("Failed to connect to \"%s\", retrying until "
			 "the server is ready", pgsql->connectionString);

	while (retry)
	{
		uint64_t now = time(NULL);

		if ((now - startTime) >= POSTGRES_PING_RETRY_TIMEOUT)
		{
			log_warn("Failed to connect to \"%s\" after %d attempts, "
					 "stopping now", pgsql->connectionString, attempts);
			break;
		}

		++attempts;
		switch (PQping(pgsql->connectionString))
		{
			case PQPING_OK:
			{
				log_debug("PQping OK after %d attempts", attempts);
				retry = false;

				/*
				 * Ping is now ok, and connection is still NULL because the
				 * first attempt to connect failed. Now is a good time to
				 * establish the connection.
				 *
				 * PQping does not check authentication, so we might still fail
				 * to connect to the server.
				 */
				connection = PQconnectdb(pgsql->connectionString);

				if (PQstatus(connection) == CONNECTION_OK)
				{
					connectionOk = true;
					log_info("Successfully connected to \"%s\"",
							 pgsql->connectionString);
				}
				else
				{
					log_error("Failed to connect after successful "
							  "ping, please verify authentication "
							  "and logs on the server at \"%s\"",
							  pgsql->connectionString);
				}
				break;
			}

			case PQPING_REJECT:
			{
				log_error("Connection rejected: \"%s\"",
						  pgsql->connectionString);
				retry = false;
				break;
			}

			case PQPING_NO_RESPONSE:
			{
				log_debug("PQping: no response after %d attempts",
						  attempts);
				retry = true;
				break;
			}

			case PQPING_NO_ATTEMPT:
			{
				log_error("Failed to ping server \"%s\" because of "
						  "client-side problems (no attempt were made)",
						  pgsql->connectionString);
				retry = false;
				break;
			}
		}

		if (asked_to_stop || asked_to_stop_fast || asked_to_reload)
		{
			retry = false;
		}

		if (retry)
		{
			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
		}
	}

	if (!connectionOk && connection != NULL)
	{
		log_error("Connection to database failed: %s",
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return NULL;
	}

	return connection;
}


/*
 * pgAutoCtlDefaultNoticeProcessor is our default PostgreSQL libpq Notice
 * Processing: NOTICE, WARNING, HINT etc are processed as log_warn messages by
 * default.
 */
static void
pgAutoCtlDefaultNoticeProcessor(void *arg, const char *message)
{
	char *m = strdup(message);
	char *lines[BUFSIZE];
	int lineCount = splitLines(m, lines, BUFSIZE);
	int lineNumber = 0;

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		log_warn("%s", lines[lineNumber]);
	}

	free(m);
}


/*
 * pgAutoCtlDebugNoticeProcessor is our PostgreSQL libpq Notice Processing to
 * use when wanting to send NOTICE, WARNING, HINT as log_debug messages.
 */
static void
pgAutoCtlDebugNoticeProcessor(void *arg, const char *message)
{
	char *m = strdup(message);
	char *lines[BUFSIZE];
	int lineCount = splitLines(m, lines, BUFSIZE);
	int lineNumber = 0;

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		log_debug("%s", lines[lineNumber]);
	}

	free(m);
}


/*
 * pgsql_execute opens a connection, runs a given SQL command, and closes
 * the connection again.
 *
 * We avoid persisting connection across multiple commands to simplify error
 * handling.
 */
static bool
pgsql_execute(PGSQL *pgsql, const char *sql)
{
	return pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL, NULL, NULL);
}


/*
 * pgsql_execute_with_params opens a connection, runs a given SQL command,
 * and closes the connection again.
 *
 * We avoid persisting connection across multiple commands to simplify error
 * handling.
 */
bool
pgsql_execute_with_params(PGSQL *pgsql, const char *sql, int paramCount,
						  const Oid *paramTypes, const char **paramValues,
						  void *context, ParsePostgresResultCB *parseFun)
{
	PGconn *connection = NULL;
	PGresult *result = NULL;
	char debugParameters[BUFSIZE] = { 0 };

	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		return false;
	}

	log_debug("%s;", sql);

	if (paramCount > 0)
	{
		int paramIndex = 0;
		int remainingBytes = BUFSIZE;
		char *writePointer = (char *) debugParameters;

		for (paramIndex = 0; paramIndex < paramCount; paramIndex++)
		{
			int bytesWritten = 0;
			const char *value = paramValues[paramIndex];

			if (paramIndex > 0)
			{
				bytesWritten = sformat(writePointer, remainingBytes, ", ");
				remainingBytes -= bytesWritten;
				writePointer += bytesWritten;
			}

			if (value == NULL)
			{
				bytesWritten = sformat(writePointer, remainingBytes, "NULL");
			}
			else
			{
				bytesWritten =
					sformat(writePointer, remainingBytes, "'%s'", value);
			}
			remainingBytes -= bytesWritten;
			writePointer += bytesWritten;
		}
		log_debug("%s", debugParameters);
	}

	result = PQexecParams(connection, sql,
						  paramCount, paramTypes, paramValues, NULL, NULL, 0);
	if (!is_response_ok(result))
	{
		char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
		char *message = PQerrorMessage(connection);
		char *errorLines[BUFSIZE];
		int lineCount = splitLines(message, errorLines, BUFSIZE);
		int lineNumber = 0;

		char *prefix =
			pgsql->connectionType == PGSQL_CONN_MONITOR ? "Monitor" : "Postgres";

		/*
		 * PostgreSQL Error message might contain several lines. Log each of
		 * them as a separate ERROR line here.
		 */
		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_error("%s %s", prefix, errorLines[lineNumber]);
		}

		log_error("SQL query: %s", sql);
		log_error("SQL params: %s", debugParameters);

		/* now stash away the SQL STATE if any */
		if (context && sqlstate)
		{
			AbstractResultContext *ctx = (AbstractResultContext *) context;

			strlcpy(ctx->sqlstate, sqlstate, SQLSTATE_LENGTH);
		}

		PQclear(result);
		clear_results(connection);
		pgsql_finish(pgsql);

		return false;
	}

	if (parseFun != NULL)
	{
		(*parseFun)(context, result);
	}

	PQclear(result);
	clear_results(connection);

	return true;
}


/*
 * is_response_ok returns whether the query result is a correct response
 * (not an error or failure).
 */
static bool
is_response_ok(PGresult *result)
{
	ExecStatusType resultStatus = PQresultStatus(result);

	return resultStatus == PGRES_SINGLE_TUPLE || resultStatus == PGRES_TUPLES_OK ||
		   resultStatus == PGRES_COMMAND_OK;
}


/*
 * clear_results consumes results on a connection until NULL is returned.
 * If an error is returned it returns false.
 */
static bool
clear_results(PGconn *connection)
{
	bool success = true;

	while (true)
	{
		PGresult *result = PQgetResult(connection);
		if (result == NULL)
		{
			break;
		}

		if (!is_response_ok(result))
		{
			log_error("Failure from Postgres: %s", PQerrorMessage(connection));
			success = false;
		}

		PQclear(result);
	}

	return success;
}


/*
 * pgsql_is_in_recovery connects to PostgreSQL and sets the is_in_recovery
 * boolean to the result of the SELECT pg_is_in_recovery() query. It returns
 * false when something went wrong doing that.
 */
bool
pgsql_is_in_recovery(PGSQL *pgsql, bool *is_in_recovery)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	char *sql = "SELECT pg_is_in_recovery()";

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL,
								   &context, &parseSingleValueResult))
	{
		/* errors have been logged already */
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to get result from pg_is_in_recovery()");
		return false;
	}

	*is_in_recovery = context.boolVal;

	return true;
}


/*
 * check_postgresql_settings connects to our local PostgreSQL instance and
 * verifies that our minimal viable configuration is in place by running a SQL
 * query that looks at the current settings.
 */
bool
pgsql_check_postgresql_settings(PGSQL *pgsql, bool isCitusInstanceKind,
								bool *settings_are_ok)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	const char *sql =
		isCitusInstanceKind ?
		CHECK_CITUS_NODE_SETTINGS_SQL : CHECK_POSTGRESQL_NODE_SETTINGS_SQL;

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL,
								   &context, &parseSingleValueResult))
	{
		/* errors have been logged already */
		return false;
	}

	if (!context.parsedOk)
	{
		/* errors have already been logged */
		return false;
	}

	*settings_are_ok = context.boolVal;

	return true;
}


/*
 * pgsql_check_monitor_settings connects to the given pgsql instance to check
 * that pgautofailover is part of shared_preload_libraries.
 */
bool
pgsql_check_monitor_settings(PGSQL *pgsql, bool *settings_are_ok)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	const char *sql =
		"select exists(select 1 from "
		"unnest("
		"string_to_array(current_setting('shared_preload_libraries'), ','))"
		" as t(name) "
		"where trim(name) = 'pgautofailover');";

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL,
								   &context, &parseSingleValueResult))
	{
		/* errors have been logged already */
		return false;
	}

	if (!context.parsedOk)
	{
		/* errors have already been logged */
		return false;
	}

	*settings_are_ok = context.boolVal;

	return true;
}


/*
 * postgres_sprintf_replicationSlotName prints the replication Slot Name to use
 * for given nodeId in the given slotName buffer of given size.
 */
bool
postgres_sprintf_replicationSlotName(int nodeId, char *slotName, int size)
{
	int bytesWritten =
		sformat(slotName, size, "%s_%d", REPLICATION_SLOT_NAME_DEFAULT, nodeId);

	return bytesWritten <= size;
}


/*
 * pgsql_set_synchronous_standby_names set synchronous_standby_names on the
 * local Postgres to the value computed on the pg_auto_failover monitor.
 */
bool
pgsql_set_synchronous_standby_names(PGSQL *pgsql,
									char *synchronous_standby_names)
{
	char quoted[BUFSIZE] = { 0 };
	GUC setting = { "synchronous_standby_names", quoted };

	log_info("Enabling synchronous replication");

	if (sformat(quoted, BUFSIZE, "'%s'", synchronous_standby_names) >= BUFSIZE)
	{
		log_error("Failed to apply the synchronous_standby_names value \"%s\": "
				  "pg_autoctl supports values up to %d bytes and this one "
				  "requires %lu bytes",
				  synchronous_standby_names,
				  BUFSIZE,
				  strlen(synchronous_standby_names));
	}

	return pgsql_alter_system_set(pgsql, setting);
}


/*
 * pgsql_replication_slot_maintain advances the current confirmed position of
 * the given replication slot up to the given LSN position, create the
 * replication slot if it does not exists yet, and remove the slots that exist
 * in Postgres but are ommited in the given array of slots.
 */
typedef struct ReplicationSlotMaintainContext
{
	char sqlstate[SQLSTATE_LENGTH];
	char operation[NAMEDATALEN];
	char slotName[BUFSIZE];
	char lsn[PG_LSN_MAXLENGTH];
	bool parsedOK;
} ReplicationSlotMaintainContext;


/*
 * pgsql_create_replication_slot tries to create a replication slot on the
 * database identified by a connection string. It's implemented as CREATE IF
 * NOT EXISTS so that it's idempotent and can be retried easily.
 */
bool
pgsql_create_replication_slot(PGSQL *pgsql, const char *slotName)
{
	ReplicationSlotMaintainContext context = { 0 };
	char *sql =
		"SELECT 'create', slot_name, lsn "
		"  FROM pg_create_physical_replication_slot($1) "
		" WHERE NOT EXISTS "
		" (SELECT 1 FROM pg_replication_slots WHERE slot_name = $1)";
	const Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { slotName };

	log_trace("pgsql_create_replication_slot");

	/*
	 * parseReplicationSlotMaintain will log_info() the replication slot
	 * creation if it happens. When the slot already exists we return 0 row and
	 * remain silent about it.
	 */
	return pgsql_execute_with_params(pgsql, sql, 1, paramTypes, paramValues,
									 &context, parseReplicationSlotMaintain);
}


/*
 * pgsql_drop_replication_slot drops a given replication slot.
 */
bool
pgsql_drop_replication_slot(PGSQL *pgsql, const char *slotName)
{
	char *sql =
		"SELECT pg_drop_replication_slot(slot_name) "
		"  FROM pg_replication_slots "
		" WHERE slot_name = $1";
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { slotName };

	log_info("Drop replication slot \"%s\"", slotName);

	return pgsql_execute_with_params(pgsql, sql,
									 1, paramTypes, paramValues, NULL, NULL);
}


/*
 * BuildNodesArrayValues build the SQL expression to use in a FROM clause to
 * represent the list of other standby nodes from the given nodeArray.
 *
 * Such a list looks either like:
 *
 *   VALUES($1, $2::pg_lsn), ($3, $4)
 *
 * or for an empty set (e.g. when we're the only standby):
 *
 *   SELECT id, lsn
 *     FROM (values (null::int, null::pg_lsn)) as t(id, lsn)
 *    WHERE false
 *
 * We actually need to provide an empty set (0 rows) with columns of the
 * expected data types so that we can join against the existing replication
 * slots and drop them. If the set is empty, we drop all the slots.
 *
 * We return how many parameters we filled in paramTypes and paramValues from
 * the nodeArray.
 */
static int
BuildNodesArrayValues(NodeAddressArray *nodeArray,
					  Oid *paramTypes, char **paramValues,
					  char *values, int size)
{
	char buffer[BUFSIZE];
	int nodeIndex = 0;
	int paramIndex = 0;
	int valuesIndex = 0;

	/*
	 * Build a SQL VALUES statement for every other node registered in the
	 * system, so that we can maintain their LSN position locally on a standby
	 * server.
	 */
	for (nodeIndex = 0; nodeIndex < nodeArray->count; nodeIndex++)
	{
		NodeAddress *node = &(nodeArray->nodes[nodeIndex]);
		IntString nodeIdString = intToString(node->nodeId);

		int idParamIndex = paramIndex;
		int lsnParamIndex = paramIndex + 1;

		paramTypes[idParamIndex] = INT4OID;
		paramValues[idParamIndex] = strdup(nodeIdString.strValue);

		paramTypes[lsnParamIndex] = LSNOID;
		paramValues[lsnParamIndex] = node->lsn;

		valuesIndex += sformat(buffer + valuesIndex, BUFSIZE - valuesIndex,
							   "%s($%d, $%d%s)",
							   valuesIndex == 0 ? "" : ",",

		                       /* we begin at $1 here: intentional off-by-one */
							   idParamIndex + 1, lsnParamIndex + 1,

		                       /* cast only the first row */
							   valuesIndex == 0 ? "::pg_lsn" : "");

		if (valuesIndex > BUFSIZE)
		{
			/* shouldn't happen because we only support up to 12 nodes */
			log_error("Failed to prepare the SQL query for "
					  "pgsql_replication_slot_maintain");
			return false;
		}

		/* prepare next round */
		paramIndex += 2;
	}

	/* when we didn't find any node to process, return our empty set */
	if (paramIndex == 0)
	{
		/* we know it fits, size is BUFSIZE or more */
		sformat(values, size,
				"SELECT id, lsn "
				"FROM (values (null::int, null::pg_lsn)) as t(id, lsn) "
				"where false");
	}
	else
	{
		int bytes = 0;

		bytes = sformat(values, size, "values %s", buffer);

		if (bytes > size)
		{
			/* shouldn't happen because we only support up to 12 nodes */
			log_error("Failed to prepare the SQL query for "
					  "pgsql_replication_slot_maintain");
			return false;
		}
	}
	return paramIndex;
}


/*
 * pgsql_replication_slot_drop_removed drop replication slots that belong to
 * nodes that have been removed. We call that function on the primary, where
 * the slots are maintained by the replication protocol.
 *
 * On the standby nodes, we advance the slots ourselves and use the other
 * function pgsql_replication_slot_maintain which is complete (create, drop,
 * advance).
 */
bool
pgsql_replication_slot_drop_removed(PGSQL *pgsql, NodeAddressArray *nodeArray)
{
	int bytes;
	char sql[2 * BUFSIZE] = { 0 };
	char values[BUFSIZE] = { 0 };

	/* *INDENT-OFF* */
	char *sqlTemplate =
		/*
		 * We could simplify the writing of this query, but we prefer that it
		 * looks as much as possible like the query used in
		 * pgsql_replication_slot_maintain() so that we can maintain both
		 * easily.
		 */
		"WITH nodes(slot_name, lsn) as ("
		" SELECT '" REPLICATION_SLOT_NAME_DEFAULT "_' || id, lsn"
		"   FROM (%s) as sb(id, lsn) "
		"), \n"
		"dropped as ("
		" SELECT slot_name, pg_drop_replication_slot(slot_name) "
		"   FROM pg_replication_slots pgrs LEFT JOIN nodes USING(slot_name) "
		"  WHERE nodes.slot_name IS NULL "
		"    AND (   slot_name ~ '" REPLICATION_SLOT_NAME_PATTERN "' "
		"         OR slot_name ~ '" REPLICATION_SLOT_NAME_DEFAULT "' )"
		"    AND slot_type = 'physical'"
		"), \n"
		"created as ("
		"SELECT c.slot_name, c.lsn "
		"  FROM nodes LEFT JOIN pg_replication_slots pgrs USING(slot_name), "
		"       LATERAL pg_create_physical_replication_slot(slot_name, true) c"
		" WHERE pgrs.slot_name IS NULL "
		") \n"
		"SELECT 'create', slot_name, lsn FROM created "
		" union all "
		"SELECT 'drop', slot_name, NULL::pg_lsn FROM dropped";
	/* *INDENT-ON* */

	Oid paramTypes[NODE_ARRAY_MAX_COUNT * 2] = { 0 };
	const char *paramValues[NODE_ARRAY_MAX_COUNT * 2] = { 0 };
	ReplicationSlotMaintainContext context = { 0 };

	int paramCount = BuildNodesArrayValues(nodeArray,
										   paramTypes, (char **) paramValues,
										   values, BUFSIZE);

	/* add the computed ($1,$2), ... string to the query "template" */
	bytes = sformat(sql, 2 * BUFSIZE, sqlTemplate, values);

	if (bytes > 2 * BUFSIZE)
	{
		/* shouldn't happen because we only support up to 12 nodes */
		log_error("Failed to prepare the SQL query for "
				  "pgsql_replication_slot_maintain");
		return false;
	}

	return pgsql_execute_with_params(pgsql, sql,
									 paramCount, paramTypes, paramValues,
									 &context,
									 parseReplicationSlotMaintain);
}


/*
 * pgsql_replication_slot_maintain creates, dropts, and advance replication
 * slots that belong to other standby nodes. We call that function on the
 * standby nodes, where the slots are maintained manually just in case we need
 * them at failover.
 */
bool
pgsql_replication_slot_maintain(PGSQL *pgsql, NodeAddressArray *nodeArray)
{
	int bytes;
	char sql[2 * BUFSIZE] = { 0 };
	char values[BUFSIZE] = { 0 };

	/* *INDENT-OFF* */
	char *sqlTemplate =
		"WITH nodes(slot_name, lsn) as ("
		" SELECT '" REPLICATION_SLOT_NAME_DEFAULT "_' || id, lsn"
		"   FROM (%s) as sb(id, lsn) "
		"), \n"
		"dropped as ("
		" SELECT slot_name, pg_drop_replication_slot(slot_name) "
		"   FROM pg_replication_slots pgrs LEFT JOIN nodes USING(slot_name) "
		"  WHERE nodes.slot_name IS NULL "
		"    AND slot_name ~ '" REPLICATION_SLOT_NAME_PATTERN "' "
		"    AND slot_type = 'physical'"
		"), \n"
		"advanced as ("
		"SELECT a.slot_name, a.end_lsn"
		"  FROM pg_replication_slots JOIN nodes USING(slot_name), "
		"       LATERAL pg_replication_slot_advance(slot_name, lsn) a"
		" WHERE nodes.lsn <> '0/0'"
		"), \n"
		"created as ("
		"SELECT c.slot_name, c.lsn "
		"  FROM nodes LEFT JOIN pg_replication_slots pgrs USING(slot_name), "
		"       LATERAL pg_create_physical_replication_slot(slot_name, true) c"
		" WHERE pgrs.slot_name IS NULL "
		") \n"
		"SELECT 'create', slot_name, lsn FROM created "
		" union all "
		"SELECT 'drop', slot_name, NULL::pg_lsn FROM dropped "
		" union all "
		"SELECT 'advance', slot_name, end_lsn FROM advanced ";
	/* *INDENT-ON* */

	Oid paramTypes[NODE_ARRAY_MAX_COUNT * 2] = { 0 };
	const char *paramValues[NODE_ARRAY_MAX_COUNT * 2] = { 0 };
	ReplicationSlotMaintainContext context = { 0 };

	int paramCount = BuildNodesArrayValues(nodeArray,
										   paramTypes, (char **) paramValues,
										   values, BUFSIZE);

	/* add the computed ($1,$2), ... string to the query "template" */
	bytes = sformat(sql, 2 * BUFSIZE, sqlTemplate, values);

	if (bytes > 2 * BUFSIZE)
	{
		/* shouldn't happen because we only support up to 12 nodes */
		log_error("Failed to prepare the SQL query for "
				  "pgsql_replication_slot_maintain");
		return false;
	}

	return pgsql_execute_with_params(pgsql, sql,
									 paramCount, paramTypes, paramValues,
									 &context,
									 parseReplicationSlotMaintain);
}


/*
 * parseReplicationSlotMaintain parses the result from a PostgreSQL query
 * fetching two columns from pg_stat_replication: sync_state and currentLSN.
 */
static void
parseReplicationSlotMaintain(void *ctx, PGresult *result)
{
	int rowNumber = 0;
	ReplicationSlotMaintainContext *context =
		(ReplicationSlotMaintainContext *) ctx;

	if (PQnfields(result) != 3)
	{
		log_error("Query returned %d columns, expected 3", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	for (rowNumber = 0; rowNumber < PQntuples(result); rowNumber++)
	{
		/* operation and slotName can't be NULL given how the SQL is built */
		char *operation = PQgetvalue(result, rowNumber, 0);
		char *slotName = PQgetvalue(result, rowNumber, 1);
		char *lsn = PQgetisnull(result, rowNumber, 2) ? ""
					: PQgetvalue(result, rowNumber, 2);

		/* adding or removing another standby node is worthy of a log line */
		if (strcmp(operation, "create") == 0)
		{
			log_info("Creating replication slot \"%s\"", slotName);
		}
		else if (strcmp(operation, "drop") == 0)
		{
			log_info("Dropping replication slot \"%s\"", slotName);
		}
		else
		{
			log_debug("parseReplicationSlotMaintain: %s %s %s",
					  operation, slotName, lsn);
		}
	}

	context->parsedOK = true;
}


/*
 * pgsql_enable_synchronous_replication enables synchronous replication
 * in Postgres such that all writes block post-commit until they are
 * replicated.
 */
bool
pgsql_enable_synchronous_replication(PGSQL *pgsql)
{
	GUC setting = { "synchronous_standby_names", "'*'" };

	log_info("Enabling synchronous replication");

	return pgsql_alter_system_set(pgsql, setting);
}


/*
 * pgsql_disable_synchronous_replication disables synchronous replication
 * in Postgres such that writes do not block if there is no replica.
 */
bool
pgsql_disable_synchronous_replication(PGSQL *pgsql)
{
	GUC setting = { "synchronous_standby_names", "''" };
	char *cancelBlockedStatementsCommand =
		"SELECT pg_cancel_backend(pid) "
		"  FROM pg_stat_activity "
		" WHERE wait_event = 'SyncRep'";

	log_info("Disabling synchronous replication");

	if (!pgsql_alter_system_set(pgsql, setting))
	{
		return false;
	}

	log_debug("Unblocking commands waiting for synchronous replication");

	if (!pgsql_execute(pgsql, cancelBlockedStatementsCommand))
	{
		return false;
	}

	return true;
}


/*
 * pgsql_set_default_transaction_mode_read_only makes it so that the server
 * won't be a target of a connection string requiring target_session_attrs
 * read-write by issuing ALTER SYSTEM SET transaction_mode_read_only TO on;
 *
 */
bool
pgsql_set_default_transaction_mode_read_only(PGSQL *pgsql)
{
	GUC setting = { "default_transaction_read_only", "'on'" };

	log_info("Setting default_transaction_read_only to on");

	return pgsql_alter_system_set(pgsql, setting);
}


/*
 * pgsql_set_default_transaction_mode_read_write makes it so that the server
 * can be a target of a connection string requiring target_session_attrs
 * read-write by issuing ALTER SYSTEM SET transaction_mode_read_only TO off;
 *
 */
bool
pgsql_set_default_transaction_mode_read_write(PGSQL *pgsql)
{
	GUC setting = { "default_transaction_read_only", "'off'" };

	log_info("Setting default_transaction_read_only to off");

	return pgsql_alter_system_set(pgsql, setting);
}


/*
 * pgsql_checkpoint runs a CHECKPOINT command on postgres to trigger a checkpoint.
 */
bool
pgsql_checkpoint(PGSQL *pgsql)
{
	return pgsql_execute(pgsql, "CHECKPOINT");
}


/*
 * pgsql_alter_system_set runs an ALTER SYSTEM SET ... command on Postgres
 * to globally set a GUC and then runs pg_reload_conf() to make existing
 * sessions reload it.
 */
static bool
pgsql_alter_system_set(PGSQL *pgsql, GUC setting)
{
	char command[1024];

	sformat(command, 1024,
			"ALTER SYSTEM SET %s TO %s", setting.name, setting.value);

	if (!pgsql_execute(pgsql, command))
	{
		return false;
	}

	if (!pgsql_reload_conf(pgsql))
	{
		return false;
	}

	return true;
}


/*
 * pgsql_reset_primary_conninfo issues the following SQL commands:
 *
 *   ALTER SYSTEM RESET primary_conninfo;
 *   ALTER SYSTEM RESET primary_slot_name;
 *
 * That's necessary to clean-up the replication settings that pg_basebackup
 * puts in place in postgresql.auto.conf in Postgres 12. We don't reload the
 * configuration after the RESET in that case, because Postgres 12 requires a
 * restart to apply the new setting value anyway.
 */
bool
pgsql_reset_primary_conninfo(PGSQL *pgsql)
{
	char *reset_primary_conninfo = "ALTER SYSTEM RESET primary_conninfo";
	char *reset_primary_slot_name = "ALTER SYSTEM RESET primary_slot_name";

	/* ALTER SYSTEM cannot run inside a transaction block */
	if (!pgsql_execute(pgsql, reset_primary_conninfo))
	{
		return false;
	}

	if (!pgsql_execute(pgsql, reset_primary_slot_name))
	{
		return false;
	}

	return true;
}


/*
 * pgsql_reload_conf causes open sessions to reload the PostgresSQL configuration
 * files.
 */
bool
pgsql_reload_conf(PGSQL *pgsql)
{
	char *sql = "SELECT pg_reload_conf()";

	return pgsql_execute(pgsql, sql);
}


/*
 * pgsql_get_hba_file_path gets the value of the hba_file setting in
 * Postgres or returns false if a failure occurred. The value is copied to
 * the hbaFilePath pointer.
 */
bool
pgsql_get_hba_file_path(PGSQL *pgsql, char *hbaFilePath, int maxPathLength)
{
	char *configValue = NULL;
	int hbaFilePathLength = 0;

	if (!pgsql_get_current_setting(pgsql, "hba_file", &configValue))
	{
		/* pgsql_get_current_setting logs a relevant error */
		return false;
	}

	hbaFilePathLength = strlcpy(hbaFilePath, configValue, maxPathLength);

	if (hbaFilePathLength >= maxPathLength)
	{
		log_error("The hba_file \"%s\" returned by postgres is %d characters, "
				  "the maximum supported by pg_autoctl is %d characters",
				  configValue, hbaFilePathLength, maxPathLength);
		free(configValue);
		return false;
	}

	free(configValue);

	return true;
}


/*
 * pgsql_get_current_setting gets the value of a GUC in Postgres by running
 * SELECT current_setting($settingName), or returns false if a failure occurred.
 *
 * If getting the value was successful, currentValue will point to a copy of the
 * value which should be freed by the caller.
 */
static bool
pgsql_get_current_setting(PGSQL *pgsql, char *settingName, char **currentValue)
{
	SingleValueResultContext context = { 0 };
	char *sql = "SELECT current_setting($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { settingName };

	context.resultType = PGSQL_RESULT_STRING;

	if (!pgsql_execute_with_params(pgsql, sql, paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		/* errors have already been logged */
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to get result from current_setting('%s')", settingName);
		return false;
	}

	*currentValue = context.strVal;

	return true;
}


/*
 * pgsql_create_database issues a CREATE DATABASE statement.
 */
bool
pgsql_create_database(PGSQL *pgsql, const char *dbname, const char *owner)
{
	char command[BUFSIZE];
	char *escapedDBName, *escapedOwner;
	PGconn *connection = NULL;
	PGresult *result = NULL;

	/* open a connection upfront since it is needed by PQescape functions */
	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		/* error message was logged in pgsql_open_connection */
		return false;
	}

	/* escape the dbname */
	escapedDBName = PQescapeIdentifier(connection, dbname, strlen(dbname));
	if (escapedDBName == NULL)
	{
		log_error("Failed to create database \"%s\": %s", dbname,
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return false;
	}

	/* escape the username */
	escapedOwner = PQescapeIdentifier(connection, owner, strlen(owner));
	if (escapedOwner == NULL)
	{
		log_error("Failed to create database \"%s\": %s", dbname,
				  PQerrorMessage(connection));
		PQfreemem(escapedDBName);
		pgsql_finish(pgsql);
		return false;
	}

	/* now build the SQL command */
	sformat(command, BUFSIZE,
			"CREATE DATABASE %s WITH OWNER %s",
			escapedDBName,
			escapedOwner);

	log_debug("Running command on Postgres: %s;", command);

	PQfreemem(escapedDBName);
	PQfreemem(escapedOwner);

	result = PQexec(connection, command);

	if (!is_response_ok(result))
	{
		/*
		 * Check if we have a duplicate_database (42P04) error, in which case
		 * it means the user has already been created, accept that as a
		 * non-error, only inform about the situation.
		 */
		char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);

		if (strcmp(sqlstate, ERRCODE_DUPLICATE_DATABASE) == 0)
		{
			log_info("The database \"%s\" already exists, skipping.", dbname);
		}
		else
		{
			log_error("Failed to create database \"%s\"[%s]: %s",
					  dbname, sqlstate, PQerrorMessage(connection));
			PQclear(result);
			clear_results(connection);
			pgsql_finish(pgsql);
			return false;
		}
	}

	PQclear(result);
	clear_results(connection);

	return true;
}


/*
 * pgsql_create_extension issues a CREATE EXTENSION statement.
 */
bool
pgsql_create_extension(PGSQL *pgsql, const char *name)
{
	char command[BUFSIZE];
	char *escapedIdentifier;
	PGconn *connection = NULL;
	PGresult *result = NULL;

	/* open a connection upfront since it is needed by PQescape functions */
	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		/* error message was logged in pgsql_open_connection */
		return false;
	}

	/* escape the dbname */
	escapedIdentifier = PQescapeIdentifier(connection, name, strlen(name));
	if (escapedIdentifier == NULL)
	{
		log_error("Failed to create extension \"%s\": %s", name,
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return false;
	}

	/* now build the SQL command */
	sformat(command, BUFSIZE, "CREATE EXTENSION %s CASCADE", escapedIdentifier);
	PQfreemem(escapedIdentifier);
	log_debug("Running command on Postgres: %s;", command);

	result = PQexec(connection, command);

	if (!is_response_ok(result))
	{
		/*
		 * Check if we have a duplicate_object (42710) error, in which case
		 * it means the user has already been created, accept that as a
		 * non-error, only inform about the situation.
		 */
		char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);

		if (strcmp(sqlstate, ERRCODE_DUPLICATE_OBJECT) == 0)
		{
			log_info("The extension \"%s\" already exists, skipping.", name);
		}
		else
		{
			log_error("Failed to create extension \"%s\"[%s]: %s",
					  name, sqlstate, PQerrorMessage(connection));
			PQclear(result);
			clear_results(connection);
			pgsql_finish(pgsql);
			return false;
		}
	}

	PQclear(result);
	clear_results(connection);

	return true;
}


/*
 * pgsql_create_user creates a user with the given settings.
 *
 * Unlike most functions this function does opens a connection itself
 * because it has some specific requirements around logging, error handling
 * and escaping.
 */
bool
pgsql_create_user(PGSQL *pgsql, const char *userName, const char *password,
				  bool login, bool superuser, bool replication)
{
	PGconn *connection = NULL;
	PGresult *result = NULL;
	PQExpBuffer query = NULL;
	char *escapedIdentifier = NULL;
	PQnoticeProcessor previousNoticeProcessor = NULL;

	/* open a connection upfront since it is needed by PQescape functions */
	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		/* error message was logged in pgsql_open_connection */
		return false;
	}

	/* escape the username */
	query = createPQExpBuffer();
	escapedIdentifier = PQescapeIdentifier(connection, userName, strlen(userName));
	if (escapedIdentifier == NULL)
	{
		log_error("Failed to create user \"%s\": %s", userName,
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return false;
	}

	appendPQExpBuffer(query, "CREATE USER %s", escapedIdentifier);
	PQfreemem(escapedIdentifier);

	if (login || superuser || replication || password)
	{
		appendPQExpBufferStr(query, " WITH");
	}
	if (login)
	{
		appendPQExpBufferStr(query, " LOGIN");
	}
	if (superuser)
	{
		appendPQExpBufferStr(query, " SUPERUSER");
	}
	if (replication)
	{
		appendPQExpBufferStr(query, " REPLICATION");
	}
	if (password)
	{
		/* show the statement before we append the password */
		log_debug("Running command on Postgres: %s PASSWORD '*****';", query->data);

		escapedIdentifier = PQescapeLiteral(connection, password, strlen(password));
		if (escapedIdentifier == NULL)
		{
			log_error("Failed to create user \"%s\": %s", userName,
					  PQerrorMessage(connection));
			PQfreemem(escapedIdentifier);
			pgsql_finish(pgsql);
			destroyPQExpBuffer(query);
			return false;
		}

		appendPQExpBuffer(query, " PASSWORD %s", escapedIdentifier);
		PQfreemem(escapedIdentifier);
	}
	else
	{
		log_debug("Running command on Postgres: %s;", query->data);
	}

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(query))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(query);
		pgsql_finish(pgsql);
		return false;
	}

	/*
	 * Set the libpq notice receiver to integrate notifications as debug
	 * message, because when dealing with the citus extension those messages
	 * are not that interesting to our pg_autoctl users frankly:
	 *
	 * NOTICE:  not propagating CREATE ROLE/USER commands to worker nodes
	 * HINT:  Connect to worker nodes directly...
	 */
	previousNoticeProcessor =
		PQsetNoticeProcessor(connection, &pgAutoCtlDebugNoticeProcessor, NULL);

	result = PQexec(connection, query->data);
	destroyPQExpBuffer(query);

	if (!is_response_ok(result))
	{
		/*
		 * Check if we have a duplicate_object (42710) error, in which case
		 * it means the user has already been created, accept that as a
		 * non-error, only inform about the situation.
		 */
		char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);

		if (strcmp(sqlstate, ERRCODE_DUPLICATE_OBJECT) == 0)
		{
			log_info("The user \"%s\" already exists, skipping.", userName);
		}
		else
		{
			log_error("Failed to create user \"%s\"[%s]: %s",
					  userName, sqlstate, PQerrorMessage(connection));
			PQclear(result);
			clear_results(connection);
			pgsql_finish(pgsql);
			return false;
		}
	}

	PQclear(result);
	clear_results(connection);

	/* restore the normal notice message processing, if needed. */
	PQsetNoticeProcessor(connection, previousNoticeProcessor, NULL);

	return true;
}


/*
 * pgsql_has_replica returns whether a replica with the given username is active.
 */
bool
pgsql_has_replica(PGSQL *pgsql, char *userName, bool *hasReplica)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };

	/*
	 * Check whether there is an entry in pg_stat_replication, which means
	 * there is either a pg_basebackup or streaming replica active. In either
	 * case, it means there is a replica that recently communicated with the
	 * postgres server, which is all we care about for the purpose of this
	 * function.
	 */
	char *sql =
		"SELECT EXISTS (SELECT 1 FROM pg_stat_replication WHERE usename = $1)";

	const Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { userName };
	int paramCount = 1;

	if (!pgsql_execute_with_params(pgsql, sql, paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		/* errors have already been logged */
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to find pg_stat_replication");
		return false;
	}

	*hasReplica = context.boolVal;

	return true;
}


/*
 * hostname_from_uri parses a PostgreSQL connection string URI and returns
 * whether the URL was successfully parsed.
 */
bool
hostname_from_uri(const char *pguri,
				  char *hostname, int maxHostLength, int *port)
{
	int found = 0;
	char *errmsg;
	PQconninfoOption *conninfo, *option;

	conninfo = PQconninfoParse(pguri, &errmsg);
	if (conninfo == NULL)
	{
		log_error("Failed to parse pguri \"%s\": %s", pguri, errmsg);
		PQfreemem(errmsg);
		return false;
	}

	for (option = conninfo; option->keyword != NULL; option++)
	{
		if (strcmp(option->keyword, "host") == 0 ||
			strcmp(option->keyword, "hostaddr") == 0)
		{
			if (option->val)
			{
				int hostNameLength = strlcpy(hostname, option->val, maxHostLength);

				if (hostNameLength >= maxHostLength)
				{
					log_error(
						"The URL \"%s\" contains a hostname of %d characters, "
						"the maximum supported by pg_autoctl is %d characters",
						option->val, hostNameLength, maxHostLength);
					PQconninfoFree(conninfo);
					return false;
				}

				++found;
			}
		}

		if (strcmp(option->keyword, "port") == 0)
		{
			if (option->val)
			{
				/* we expect a single port number in a monitor's URI */
				if (!stringToInt(option->val, port))
				{
					log_error("Failed to parse port number : %s", option->val);

					PQconninfoFree(conninfo);
					return false;
				}
				++found;
			}
			else
			{
				*port = POSTGRES_PORT;
			}
		}

		if (found == 2)
		{
			break;
		}
	}
	PQconninfoFree(conninfo);

	return true;
}


/*
 * validate_connection_string takes a connection string and parses it with
 * libpq, varifying that it's well formed and usable.
 */
bool
validate_connection_string(const char *connectionString)
{
	PQconninfoOption *connInfo = NULL;
	char *errorMessage = NULL;

	int length = strlen(connectionString);
	if (length >= MAXCONNINFO)
	{
		log_error("Connection string \"%s\" is %d "
				  "characters, the maximum supported by pg_autoctl is %d",
				  connectionString, length, MAXCONNINFO);
		return false;
	}

	connInfo = PQconninfoParse(connectionString, &errorMessage);
	if (connInfo == NULL)
	{
		log_error("Failed to parse connection string \"%s\": %s ",
				  connectionString, errorMessage);
		PQfreemem(errorMessage);
		return false;
	}

	PQconninfoFree(connInfo);

	return true;
}


/*
 * pgsql_get_postgres_metadata returns several bits of information that we need
 * to take decisions in the rest of the code:
 *
 *  - config_file path (cache invalidation in case it changed)
 *  - hba_file path (cache invalidation in case it changed)
 *  - pg_is_in_recovery (primary or standby, as expected?)
 *  - sync_state from pg_stat_replication when a primary
 *  - current_lsn from the server
 *
 * With those metadata we can then check our expectations and take decisions in
 * some cases. We can obtain all the metadata that we need easily enough in a
 * single SQL query, so that's what we do.
 */
typedef struct PgMetadata
{
	char sqlstate[6];
	bool parsedOk;
	bool pg_is_in_recovery;
	char syncState[PGSR_SYNC_STATE_MAXLENGTH];
	char currentLSN[PG_LSN_MAXLENGTH];
} PgMetadata;


bool
pgsql_get_postgres_metadata(PGSQL *pgsql, const char *slotName,
							bool *pg_is_in_recovery,
							char *pgsrSyncState, char *currentLSN)
{
	PgMetadata context = { 0 };

	/* *INDENT-OFF* */
	char *sql =
		/*
		 * Make it so that we still have the current WAL LSN even in the case
		 * where there's no replication slot in use by any standby.
		 *
		 * When on the primary, we might have multiple standby nodes connected.
		 * We're good when at least one of them is either 'sync' or 'quorum'.
		 * We don't check individual replication slots, we take the "best" one
		 * and report that.
		 */
		"select pg_is_in_recovery(),"
		" coalesce(rep.sync_state, '') as sync_state,"
		" case when pg_is_in_recovery()"
		" then pg_last_wal_receive_lsn()"
		" else pg_current_wal_lsn()"
		" end as current_lsn"
		" from (values(1)) as dummy"
		" full outer join"
		" ("
		"   select sync_state"
		"     from pg_replication_slots slot"
		"     join pg_stat_replication rep"
		"       on rep.pid = slot.active_pid"
		"   where slot_name ~ '" REPLICATION_SLOT_NAME_PATTERN "' "
		"      or slot_name = '" REPLICATION_SLOT_NAME_DEFAULT "' "
		"order by case sync_state "
		"         when 'quorum' then 4 "
		"         when 'sync' then 3 "
		"         when 'potential' then 2 "
		"         when 'async' then 1 "
		"         else 0 end "
		"    desc limit 1"
		" ) "
		"as rep on true";
	/* *INDENT-ON* */

	const Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1] = { slotName };
	int paramCount = 1;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parsePgMetadata))
	{
		/* errors have already been logged */
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to parse the Postgres metadata");
		return false;
	}

	*pg_is_in_recovery = context.pg_is_in_recovery;

	/* the last two metadata items are opt-in */
	if (pgsrSyncState != NULL)
	{
		strlcpy(pgsrSyncState, context.syncState, PGSR_SYNC_STATE_MAXLENGTH);
	}

	if (currentLSN != NULL)
	{
		strlcpy(currentLSN, context.currentLSN, PG_LSN_MAXLENGTH);
	}

	pgsql_finish(pgsql);

	return true;
}


/*
 * parsePgMetadata parses the result from a PostgreSQL query fetching
 * two columns from pg_stat_replication: sync_state and currentLSN.
 */
static void
parsePgMetadata(void *ctx, PGresult *result)
{
	PgMetadata *context = (PgMetadata *) ctx;

	if (PQnfields(result) != 3)
	{
		log_error("Query returned %d columns, expected 3", PQnfields(result));
		context->parsedOk = false;
		return;
	}

	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOk = false;
		return;
	}

	context->pg_is_in_recovery = strcmp(PQgetvalue(result, 0, 0), "t") == 0;

	if (!PQgetisnull(result, 0, 1))
	{
		char *value = PQgetvalue(result, 0, 1);

		strlcpy(context->syncState, value, PGSR_SYNC_STATE_MAXLENGTH);
	}
	else
	{
		context->syncState[0] = '\0';
	}

	if (!PQgetisnull(result, 0, 2))
	{
		char *value = PQgetvalue(result, 0, 2);

		strlcpy(context->currentLSN, value, PG_LSN_MAXLENGTH);
	}
	else
	{
		context->currentLSN[0] = '\0';
	}

	context->parsedOk = true;
}


/*
 * LISTEN/NOTIFY support.
 *
 * First, send a LISTEN command.
 */
bool
pgsql_listen(PGSQL *pgsql, char *channels[])
{
	PGconn *connection = NULL;
	PGresult *result = NULL;
	char sql[BUFSIZE];

	/* open a connection upfront since it is needed by PQescape functions */
	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		/* error message was logged in pgsql_open_connection */
		return false;
	}

	for (int i = 0; channels[i]; i++)
	{
		char *channel =
			PQescapeIdentifier(connection, channels[i], strlen(channels[i]));

		if (channel == NULL)
		{
			log_error("Failed to LISTEN \"%s\": %s",
					  channels[i], PQerrorMessage(connection));
			pgsql_finish(pgsql);
			return false;
		}

		sformat(sql, BUFSIZE, "LISTEN %s", channel);

		PQfreemem(channel);

		result = PQexec(connection, sql);

		if (!is_response_ok(result))
		{
			log_error("Failed to LISTEN \"%s\": %s",
					  channels[i], PQerrorMessage(connection));
			PQclear(result);
			clear_results(connection);

			return false;
		}

		PQclear(result);
		clear_results(connection);
	}

	return true;
}


/*
 * pgsql_alter_extension_update_to executes ALTER EXTENSION ... UPDATE TO ...
 */
bool
pgsql_alter_extension_update_to(PGSQL *pgsql,
								const char *extname, const char *version)
{
	int n = 0;
	char command[BUFSIZE];
	char *escapedIdentifier, *escapedVersion;
	PGconn *connection = NULL;
	PGresult *result = NULL;

	/* open a connection upfront since it is needed by PQescape functions */
	connection = pgsql_open_connection(pgsql);
	if (connection == NULL)
	{
		/* error message was logged in pgsql_open_connection */
		return false;
	}

	/* escape the extname */
	escapedIdentifier = PQescapeIdentifier(connection, extname, strlen(extname));
	if (escapedIdentifier == NULL)
	{
		log_error("Failed to update extension \"%s\": %s", extname,
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return false;
	}

	/* escape the version */
	escapedVersion = PQescapeIdentifier(connection, version, strlen(version));
	if (escapedIdentifier == NULL)
	{
		log_error("Failed to update extension \"%s\" to version \"%s\": %s",
				  extname, version,
				  PQerrorMessage(connection));
		pgsql_finish(pgsql);
		return false;
	}

	/* now build the SQL command */
	n = sformat(command, BUFSIZE, "ALTER EXTENSION %s UPDATE TO %s",
				escapedIdentifier, escapedVersion);

	if (n >= BUFSIZE)
	{
		log_error("BUG: pg_autoctl only supports SQL string up to %d bytes, "
				  "a SQL string of %d bytes is needed to "
				  "update the \"%s\" extension.",
				  BUFSIZE, n, extname);
	}

	PQfreemem(escapedIdentifier);
	PQfreemem(escapedVersion);

	log_debug("Running command on Postgres: %s;", command);

	result = PQexec(connection, command);

	if (!is_response_ok(result))
	{
		char *sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);

		log_error("Error %s while running Postgres query: %s: %s",
				  sqlstate, command, PQerrorMessage(connection));
		PQclear(result);
		clear_results(connection);
		pgsql_finish(pgsql);
		return false;
	}

	PQclear(result);
	clear_results(connection);

	return true;
}
