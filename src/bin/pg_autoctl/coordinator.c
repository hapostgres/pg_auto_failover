/*
 * src/bin/pg_autoctl/coordinator.c
 *	 API for interacting with the coordinator
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <limits.h>
#include <strings.h>

#include "coordinator.h"
#include "postgres_fe.h"
#include "libpq-fe.h"
#include "log.h"
#include "parsing.h"

typedef struct CoordinatorNodeParseContext
{
	CoordinatorNode *node;
	bool parsedOK;
} CoordinatorNodeParseContext;


static void parseCoordinatorNode(void *ctx, PGresult *result);


/*
 * coordinator_init initialises a Coordinator struct to connect to the given
 * database URL.
 */
bool
coordinator_init(Coordinator *coordinator, NodeAddress *node, Keeper *keeper)
{
	char connInfo[MAXCONNINFO];

	/* copy our NodeAddress into the Coordinator struct for later reference */
	strlcpy(coordinator->node.host, node->host, _POSIX_HOST_NAME_MAX);
	coordinator->node.port = node->port;

	/*
	 * Prepare a connection string to connect to the coordinator node. We
	 * consider that the Citus coordinator and the workers are setup with the
	 * same dbname and username.
	 */
	sprintf(connInfo, "host=%s port=%d dbname=%s user=%s",
			node->host, node->port,
			keeper->config.pgSetup.dbname,
			pg_setup_get_username(&(keeper->config.pgSetup)));

	if (!pgsql_init(&coordinator->pgsql, connInfo))
	{
		/* URL must be invalid, pgsql_init logged an error */
		return false;
	}

	return true;
}


/*
 * coordinator_init_from_monitor connects to the monitor to fetch the nodename
 * and port of the coordinator, then initializes the Coordinator data structure
 * and PostgreSQL client connection details.
 */
bool
coordinator_init_from_monitor(Coordinator *coordinator, Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	NodeAddress coordinatorNodeAddress = { 0 };

	if (!monitor_get_coordinator(monitor,
								 config->formation,
								 &coordinatorNodeAddress))
	{
		log_error("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		return false;
	}

	if (!coordinator_init(coordinator, &coordinatorNodeAddress, keeper))
	{
		/* that would be very surprising at this point */
		log_error("Failed to contact the coordinator because its URL is "
				  "invalid, see above for details");
		return false;
	}

	return true;
}



/*
 * coordinator_add_inactive_node calls master_add_inactive_node() on the
 * coordinator node to add the current PostgreSQL instance as a worker.
 */
bool
coordinator_add_inactive_node(Coordinator *coordinator, Keeper *keeper,
							  CoordinatorNode *node)
{
	PGSQL *pgsql = &coordinator->pgsql;
	const char *sql =
		"SELECT * FROM master_add_inactive_node($1, $2, groupid => $3)";
	int paramCount = 3;
	Oid paramTypes[3] = { TEXTOID, INT4OID, INT4OID };
	const char *paramValues[3];
	CoordinatorNodeParseContext parseContext = { node, false };

	paramValues[0] = keeper->config.nodename;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;
	paramValues[2] = intToString(keeper->config.groupId).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseCoordinatorNode))
	{
		log_error("Failed to add inactive node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.nodename, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to add inactive node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\" "
				  "because the coordinator returned an unexpected result, "
				  "see previous lines for details",
				  keeper->config.nodename, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	return true;
}


/*
 * coordinator_add_inactive_node calls master_activate_node() on the
 * coordinator node to activate the current PostgreSQL instance as a worker.
 */
bool
coordinator_activate_node(Coordinator *coordinator, Keeper *keeper,
						  CoordinatorNode *node)
{
	PGSQL *pgsql = &coordinator->pgsql;
	const char *sql =
		"SELECT * FROM master_activate_node($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];
	CoordinatorNodeParseContext parseContext = { node, false };

	paramValues[0] = keeper->config.nodename;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseCoordinatorNode))
	{
		log_warn("Failed to activate node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.nodename, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	if (!parseContext.parsedOK)
	{
		log_warn("Failed to activate node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\" "
				  "because the coordinator returned an unexpected result, "
				  "see previous lines for details",
				  keeper->config.nodename, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	return true;
}


/*
 * coordinator_remove_node calls master_remove_node() on the coordinator node
 * with the current keeper's node as an argument.
 */
bool
coordinator_remove_node(Coordinator *coordinator, Keeper *keeper)
{
	PGSQL *pgsql = &coordinator->pgsql;
	const char *sql =
		"SELECT * FROM master_remove_node($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = keeper->config.nodename;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to remove node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.nodename, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator->pgsql);

	return true;
}


/*
 * parseCoordinatorNode parses the result of the coordinator
 * master_add_inactive_node and master_activate_node from the libpq result and
 * writes it to the CoordinatorNodeParseContext pointed to by ctx.
 */
static void
parseCoordinatorNode(void *ctx, PGresult *result)
{
	CoordinatorNodeParseContext *context = (CoordinatorNodeParseContext *) ctx;
	char *value = NULL;
	int valueLength = 0;
	int errors = 0;

	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 9)
	{
		log_error("Query returned %d columns, expected 9", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 0);
	context->node->nodeid = strtol(value, NULL, 0);
	if (context->node->nodeid == 0)
	{
		log_error("Invalid nodeid \"%s\" returned by coordinator", value);
		errors++;
	}

	value = PQgetvalue(result, 0, 1);
	context->node->groupid = strtol(value, NULL, 0);
	if (context->node->groupid == 0)
	{
		log_error("Invalid groupid \"%s\" returned by coordinator", value);
		errors++;
	}

	value = PQgetvalue(result, 0, 2);
	valueLength = strlcpy(context->node->nodename, value, _POSIX_HOST_NAME_MAX);

	if (valueLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("nodename \"%s\" returned by coordinator is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, valueLength, _POSIX_HOST_NAME_MAX - 1);
		errors++;
	}

	value = PQgetvalue(result, 0, 3);
	context->node->nodeport = strtol(value, NULL, 0);
	if (context->node->nodeport == 0)
	{
		log_error("Invalid nodeport \"%s\" returned by coordinator", value);
		errors++;
	}

	value = PQgetvalue(result, 0, 4);
	valueLength = strlcpy(context->node->noderack, value, NAMEDATALEN);
	if (valueLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("noderack \"%s\" returned by coordinator is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, valueLength, NAMEDATALEN - 1);
		errors++;
	}

	value = PQgetvalue(result, 0, 5);
	context->node->hasmetadata = strcmp(value, "t") == 0;

	value = PQgetvalue(result, 0, 6);
	context->node->isactive = strcmp(value, "t") == 0;

	value = PQgetvalue(result, 0, 7);
	valueLength = strlcpy(context->node->state, value, NAMEDATALEN);
	if (valueLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("state \"%s\" returned by coordinator is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, valueLength, NAMEDATALEN - 1);
		errors++;
	}

	value = PQgetvalue(result, 0, 4);
	valueLength = strlcpy(context->node->nodecluster, value, _POSIX_HOST_NAME_MAX);
	if (valueLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("nodecluster \"%s\" returned by coordinator is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, valueLength, _POSIX_HOST_NAME_MAX - 1);
		errors++;
	}

	if (errors > 0)
	{
		context->parsedOK = false;
	}
	else
	{
		context->parsedOK = true;
	}

	return;
}


/*
 * coordinator_update_node_prepare call master_update_node() on the formation's
 * coordinator node, in a prepared transaction named "master_update_node
 * nodename".
 */
bool
coordinator_update_node_prepare(Coordinator *coordinator, Keeper *keeper,
								NodeAddress *primary)
{
	char sql[BUFSIZE];
	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	PGSQL *pgsql = &coordinator->pgsql;
	int paramCount = 5;
	Oid paramTypes[5] = { INT4OID, TEXTOID, INT4OID, TEXTOID, INT4OID };
	const char *paramValues[5];
	int groupId = keeper->state.current_group;

	/*
	 * Protect against calls with non-initialized primary.
	 *
	 * Note that when the primary is forcibly removed (e.g. by a user doing
	 * `pg_autoctl drop node`), we can't get the primary host/port from the
	 * monitor anymore.
	 */
	if (primary != NULL
		&& (IS_EMPTY_STRING_BUFFER(primary->host) || primary->port == 0))
	{
		/* that's a developer error, message targets devs */
		log_error("BUG: coordinator_update_node_prepare: uninitialized primary!");
		return false;
	}

	GetPreparedTransactionName(groupId, transactionName);

	sprintf(sql, "BEGIN;");

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL, NULL, NULL))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	/*
	 * Now call master_update_node() on the nodeid from our group. Also make
	 * sure that the metadata are in sync in between the monitor, the keeper,
	 * and the coordinator node by adding all we know in the WHERE clause of
	 * the query.
	 *
	 * That way, if there's some mismatch, we get 0 rows returned and fail
	 * visibly rather than succeed to update a random node on the coordinator.
	 *
	 * Finally, we have the groupid from the monitor, but we don't have the
	 * nodeid from the coordinator on the keeper: we don't need it, that's
	 * private data handled by the coordinator, and the coordinator is going to
	 * provide for that information itself with the following SQL query.
	 */
	if (primary)
	{
		paramCount = 5;

		sprintf(sql,
				"SELECT master_update_node(nodeid, $2, $3)"
				"  FROM pg_dist_node "
				" WHERE groupid = $1 and nodename = $4 and nodeport = $5");
	}
	else
	{
		paramCount = 3;

		sprintf(sql,
				"SELECT master_update_node(nodeid, $2, $3)"
				"  FROM pg_dist_node WHERE groupid = $1");
	}

	paramValues[0] = intToString(groupId).strValue;
	paramValues[1] = keeper->config.nodename;
	paramValues[2] = intToString(keeper->config.pgSetup.pgport).strValue;
	paramValues[3] = primary ? primary->host : NULL;
	paramValues[4] = primary ? intToString(primary->port).strValue : 0;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	if (keeper->config.pgSetup.proxyport > 0)
	{
		if (!coordinator_upsert_poolinfo_port(coordinator, keeper)) {
			log_error("Failed to add proxyport to pg_dist_poolinfo, "
					  "see above for details");
			return false;
		}
	}

	sprintf(sql, "PREPARE TRANSACTION '%s';", transactionName);

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL, NULL, NULL))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator->pgsql);

	/* set the transaction name in the state, we might need it */
	strlcpy(keeper->state.preparedTransactionName,
			transactionName, PREPARED_TRANSACTION_NAMELEN);

	return true;
}


/*
 * coordinator_update_node_commit commits the prepared transaction from
 * coordinator_update_node_prepare.
 */
bool
coordinator_update_node_commit(Coordinator *coordinator, Keeper *keeper)
{
	char sql[BUFSIZE];
	PGSQL *pgsql = &coordinator->pgsql;

	if (IS_EMPTY_STRING_BUFFER(keeper->state.preparedTransactionName))
	{
		log_error("Failed attempt to commit prepared master_update_node: "
				  "no known prepared transaction in current keeper's state");
		return false;
	}

	sprintf(sql,
			"COMMIT PREPARED '%s';", keeper->state.preparedTransactionName);

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL, NULL, NULL))
	{
		log_error("Failed to commit prepared master_update_node "
				  "on the coordinator, see above for details.");
		return false;
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator->pgsql);

	/*
	 * Reset the state's current preparedTransactionName, there is not a
	 * current one anymore.
	 */
	bzero(keeper->state.preparedTransactionName, PREPARED_TRANSACTION_NAMELEN);

	return true;
}


/*
 * coordinator_update_node_rollback rolls back the prepared transaction from
 * coordinator_update_node_prepare.
 */
bool
coordinator_update_node_rollback(Coordinator *coordinator, Keeper *keeper)
{
	char sql[BUFSIZE];
	PGSQL *pgsql = &coordinator->pgsql;

	if (IS_EMPTY_STRING_BUFFER(keeper->state.preparedTransactionName))
	{
		log_error("Failed attempt to commit prepared master_update_node: "
				  "no known prepared transaction in current keeper's state");
		return false;
	}

	sprintf(sql,
			"ROLLBACK PREPARED '%s';", keeper->state.preparedTransactionName);

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL, NULL, NULL))
	{
		log_error("Failed to rollback prepared master_update_node "
				  "on the coordinator, see above for details.");
		return false;
	}

	/* disconnect from PostgreSQL on the coordinator now */
	pgsql_finish(&coordinator->pgsql);

	/*
	 * Reset the state's current preparedTransactionName, there is not a
	 * current one anymore.
	 */
	bzero(keeper->state.preparedTransactionName, PREPARED_TRANSACTION_NAMELEN);

	return true;
}


/*
 * coordinator_update_cleanup issues a ROLLBACK PREPARED on transaction that we
 * leave behind in many situations, when we have an error.
 */
bool
coordinator_update_cleanup(Keeper *keeper)
{
	KeeperStateData *state = &(keeper->state);

	log_trace("coordinator_update_cleanup");

	if (state->current_role == PREP_PROMOTION_STATE
		|| state->current_role == STOP_REPLICATION_STATE)
	{
		/* we expect to have an on-onging prepared transaction, move on. */
		return true;
	}

	/*
	 * Two states are allowed to leave a prepared transaction behind them,
	 * because the next transition is going to take care of it. That happens
	 * when a failover happens after a glitch has been detected on the primary.
	 */
	if (!IS_EMPTY_STRING_BUFFER(state->preparedTransactionName))
	{
		Coordinator coordinator = { 0 };

		if (!coordinator_init_from_monitor(&coordinator, keeper))
		{
			log_error("Failed to connect to the coordinator node at %s:%d, "
					  "see above for details",
					  coordinator.node.host, coordinator.node.port);
			return false;
		}

		log_info("Cancelling node update on coordinator %s:%d: "
				 "ROLLBACK PREPARED \"%s\"",
				 coordinator.node.host, coordinator.node.port,
				 keeper->state.preparedTransactionName);

		if (!coordinator_update_node_rollback(&coordinator, keeper))
		{
			log_error("Failed to clean-up prepared transaction \"%s\" "
					  "on the coordinator %s:%d, see above for details",
					  state->preparedTransactionName,
					  coordinator.node.host, coordinator.node.port);
			return false;
		}
	}

	return true;
}

/*
 * GetPreparedTransactionName cooks the name of the prepared transaction we are
 * going to use on the coordinator.
 */
void
GetPreparedTransactionName(int groupId, char *name)
{
	snprintf(name, PREPARED_TRANSACTION_NAMELEN,
			 "master_update_node %d", groupId);
}


/*
 * coordinator_has_prepared_transaction connects to the coordinator to check if
 * the PostgreSQL system table pg_prepared_xacts contains our prepared
 * transaction.
 *
 * We use that at startup in case we failed, crashed, got OOM-killed or
 * otherwise killed -9 by angry users in between PREPARE TRANSACTION and saving
 * the keeper state.
 */
bool
coordinator_cleanup_lost_transaction(Keeper *keeper, Coordinator *coordinator)
{
	PGSQL *pgsql = &(coordinator->pgsql);
	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];
	const char *sql = "select 1 from pg_prepared_xacts where gid = $1";
	int groupId = keeper->state.current_group;
	SingleValueResultContext context;

	log_trace("coordinator_has_prepared_transaction");

	context.resultType = PGSQL_RESULT_INT;
	context.intVal = 0;
	context.parsedOk = true;

	GetPreparedTransactionName(groupId, transactionName);

	paramValues[0] = transactionName;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, parseSingleValueResult))
	{
		log_error("Failed to get list of prepared transaction on "
				  "the coordinator %s:%d",
				  coordinator->node.host, coordinator->node.port);
		return false;
	}

	if (context.intVal == 1)
	{
		strlcpy(keeper->state.preparedTransactionName,
				transactionName,
				PREPARED_TRANSACTION_NAMELEN);

		return coordinator_update_node_rollback(coordinator, keeper);
	}

	return true;
}


/*
 * coordinator_upsert_poolinfo_port updates the table pg_dist_poolinfo to add
 * the pgSetup->proxyport
 */
bool
coordinator_upsert_poolinfo_port(Coordinator *coordinator, Keeper *keeper)
{
	PGSQL *pgsql = &coordinator->pgsql;
	const char *sql =
		"INSERT INTO pg_dist_poolinfo (nodeid, poolinfo) VALUES ($1, $2)"
		"ON CONFLICT (nodeid) DO UPDATE SET poolinfo = EXCLUDED.poolinfo;";
	int paramCount = 2;
	Oid paramTypes[2] = {INT4OID, TEXTOID};
	const char *paramValues[2];
	char proxyInfo[MAXCONNINFO];

	/*
	 * Prepare a argument for pg_dist_poolinfo table
	 */
	sprintf(proxyInfo, "host=%s port=%d",
			keeper->config.nodename, keeper->config.pgSetup.proxyport);

	paramValues[0] = intToString(keeper->config.groupId).strValue;
	paramValues[1] = proxyInfo;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to add proxyport %d to pg_dist_poolinfo "
				  "on Citus coordinator %s:%d ",
				  keeper->config.pgSetup.proxyport,
				  coordinator->node.host, coordinator->node.port);
		return false;
	}

	return true;
}
