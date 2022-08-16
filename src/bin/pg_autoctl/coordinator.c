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

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "coordinator.h"
#include "nodestate_utils.h"
#include "libpq-fe.h"
#include "log.h"
#include "parsing.h"
#include "string_utils.h"

static bool coordinator_master_activate_node_returns_record(PGSQL *pgsql,
															bool *returnsRecord);

static void parseRemovedNodeIds(void *ctx, PGresult *result);


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
	sformat(connInfo, sizeof(connInfo),
			"host=%s port=%d dbname=%s user=%s",
			node->host, node->port,
			keeper->config.pgSetup.dbname,
			pg_setup_get_username(&(keeper->config.pgSetup)));

	if (!pgsql_init(&coordinator->pgsql, connInfo, PGSQL_CONN_COORDINATOR))
	{
		/* URL must be invalid, pgsql_init logged an error */
		return false;
	}

	return true;
}


/*
 * coordinator_init_from_monitor connects to the monitor to fetch the hostname
 * and port of the coordinator, then initializes the Coordinator data structure
 * and PostgreSQL client connection details.
 */
bool
coordinator_init_from_monitor(Coordinator *coordinator, Keeper *keeper)
{
	Monitor *monitor = &(keeper->monitor);
	KeeperConfig *config = &(keeper->config);
	CoordinatorNodeAddress coordinatorNodeAddress = { 0 };

	if (!monitor_get_coordinator(monitor,
								 config->formation,
								 &coordinatorNodeAddress))
	{
		log_error("Failed to get the coordinator node from the monitor, "
				  "see above for details");
		return false;
	}

	if (!coordinator_init(coordinator, &(coordinatorNodeAddress.node), keeper))
	{
		/* that would be very surprising at this point */
		log_error("Failed to contact the coordinator because its URL is "
				  "invalid, see above for details");
		return false;
	}

	return true;
}


/*
 * coordinator_init_from_keeper builds a coordinator instance that points to
 * the local node, which is assumed to be a coordinator itself. Remember that
 * the keeper->postgres.pgKind can be one of "standalone", "coordinator", or
 * "worker" (see enum PgInstanceKind in pgsetup.h).
 */
bool
coordinator_init_from_keeper(Coordinator *coordinator, Keeper *keeper)
{
	NodeAddress coordinatorNodeAddress = { 0 };

	if (keeper->postgres.pgKind != NODE_KIND_CITUS_COORDINATOR)
	{
		/* that's a bug, highly unexpected, message inteded to a developper */
		log_error("BUG: coordinator_init_from_keeper called with a node "
				  "kind that is not a coordinator: \"%s\"",
				  nodeKindToString(keeper->postgres.pgKind));
		return false;
	}

	/* at the moment the Coordinator NodeAddress only uses host:port */
	strlcpy(coordinatorNodeAddress.host,
			keeper->postgres.postgresSetup.pghost,
			sizeof(coordinatorNodeAddress.host));

	coordinatorNodeAddress.port = keeper->postgres.postgresSetup.pgport;

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
 * coordinator_add_node calls master_add_node() on the coordinator node to add
 * the current PostgreSQL instance as a Citus worker node.
 */
bool
coordinator_add_node(Coordinator *coordinator, Keeper *keeper,
					 int *nodeid)
{
	PGSQL *pgsql = &coordinator->pgsql;

	char *sql =
		"SELECT master_add_node($1, $2, groupid => $3, "
		"noderole => $4::noderole, nodecluster => $5);";

	int paramCount = 5;
	Oid paramTypes[5] = { TEXTOID, INT4OID, INT4OID, TEXTOID, NAMEOID };
	const char *paramValues[5];

	char *citusRoleStr =
		keeper->config.citusRole == CITUS_ROLE_PRIMARY ? "primary" : "secondary";

	char *clusterName =
		IS_EMPTY_STRING_BUFFER(keeper->config.pgSetup.citusClusterName)
		? "default"
		: keeper->config.pgSetup.citusClusterName;

	SingleValueResultContext parseContext = { { 0 }, PGSQL_RESULT_INT, false };

	paramValues[0] = keeper->config.hostname;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;
	paramValues[2] = intToString(keeper->config.groupId).strValue;
	paramValues[3] = citusRoleStr;
	paramValues[4] = clusterName;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseSingleValueResult))
	{
		log_error("Failed to add active node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	if (!parseContext.parsedOk)
	{
		log_error("Failed to add node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\" "
				  "because the coordinator returned an unexpected result, "
				  "see previous lines for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	*nodeid = parseContext.intVal;

	return true;
}


/*
 * coordinator_add_inactive_node calls master_add_inactive_node() on the
 * coordinator node to add the current PostgreSQL instance as a worker.
 */
bool
coordinator_add_inactive_node(Coordinator *coordinator, Keeper *keeper,
							  int *nodeid)
{
	PGSQL *pgsql = &coordinator->pgsql;

	/* the master_add_inactive_node signature changed in Citus 9.0 */
	bool returnsRecord = false;
	char *sqlRecord =
		"SELECT nodeid FROM "
		"master_add_inactive_node($1, $2, groupid => $3, "
		"noderole => $4::noderole, nodecluster => $5);";
	char *sqlInteger =
		"SELECT master_add_inactive_node($1, $2, groupid => $3, "
		"noderole => $4::noderole, nodecluster => $5);";

	char *sql;
	int paramCount = 5;
	Oid paramTypes[5] = { TEXTOID, INT4OID, INT4OID, TEXTOID, NAMEOID };
	const char *paramValues[5];

	char *citusRoleStr =
		keeper->config.citusRole == CITUS_ROLE_PRIMARY ? "primary" : "secondary";

	char *clusterName =
		IS_EMPTY_STRING_BUFFER(keeper->config.pgSetup.citusClusterName)
		? "default"
		: keeper->config.pgSetup.citusClusterName;

	SingleValueResultContext parseContext = { { 0 }, PGSQL_RESULT_INT, false };

	if (!coordinator_master_activate_node_returns_record(pgsql, &returnsRecord))
	{
		log_error("Failed to add inactive node %s:%d, see above for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport);
		return false;
	}

	if (returnsRecord)
	{
		sql = sqlRecord;
	}
	else
	{
		sql = sqlInteger;
	}

	paramValues[0] = keeper->config.hostname;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;
	paramValues[2] = intToString(keeper->config.groupId).strValue;
	paramValues[3] = citusRoleStr;
	paramValues[4] = clusterName;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseSingleValueResult))
	{
		log_error("Failed to add inactive node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	if (!parseContext.parsedOk)
	{
		log_error("Failed to add inactive node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\" "
				  "because the coordinator returned an unexpected result, "
				  "see previous lines for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	*nodeid = parseContext.intVal;

	return true;
}


/*
 * coordinator_add_inactive_node calls master_activate_node() on the
 * coordinator node to activate the current PostgreSQL instance as a worker.
 */
bool
coordinator_activate_node(Coordinator *coordinator, Keeper *keeper,
						  int *nodeid)
{
	PGSQL *pgsql = &coordinator->pgsql;

	/* the master_activate_node signature changed in Citus 9.0 */
	bool returnsRecord = false;
	char *sqlRecord = "SELECT nodeid FROM master_activate_node($1, $2)";
	char *sqlInteger = "SELECT master_activate_node($1, $2)";

	char *sql;
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	SingleValueResultContext parseContext;
	parseContext.resultType = PGSQL_RESULT_INT;
	parseContext.parsedOk = false;

	if (!coordinator_master_activate_node_returns_record(pgsql, &returnsRecord))
	{
		log_error("Failed to activate node %s:%d, see above for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport);
		return false;
	}

	if (returnsRecord)
	{
		sql = sqlRecord;
	}
	else
	{
		sql = sqlInteger;
	}

	paramValues[0] = keeper->config.hostname;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseSingleValueResult))
	{
		log_warn("Failed to activate node %s:%d on Citus coordinator %s:%d "
				 " of formation \"%s\", see previous lines for details",
				 keeper->config.hostname, keeper->config.pgSetup.pgport,
				 coordinator->node.host, coordinator->node.port,
				 keeper->config.formation);
		return false;
	}

	if (!parseContext.parsedOk)
	{
		log_warn("Failed to activate node %s:%d on Citus coordinator %s:%d "
				 " of formation \"%s\" "
				 "because the coordinator returned an unexpected result, "
				 "see previous lines for details",
				 keeper->config.hostname, keeper->config.pgSetup.pgport,
				 coordinator->node.host, coordinator->node.port,
				 keeper->config.formation);
		return false;
	}

	*nodeid = parseContext.intVal;

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
		"SELECT master_remove_node($1, $2) "
		" FROM pg_dist_node "
		"WHERE nodename = $1 and nodeport = $2";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = keeper->config.hostname;
	paramValues[1] = intToString(keeper->config.pgSetup.pgport).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to remove node %s:%d on Citus coordinator %s:%d "
				  " of formation \"%s\", see previous lines for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport,
				  coordinator->node.host, coordinator->node.port,
				  keeper->config.formation);
		return false;
	}

	return true;
}


/*
 * coordinator_udpate_node_transaction_is_prepared sets its parameter
 * transactionHasBeenPrepared to true when a prepared transctions is in flight
 * for our current ${groupid}, as seen in the Postgres catalogs.
 *
 * It returns true when it succeeds and false when it fails to check for the
 * Postgres catalogs for some reason (connection issue, privileges, etc).
 */
bool
coordinator_udpate_node_transaction_is_prepared(Coordinator *coordinator,
												Keeper *keeper,
												bool *transactionHasBeenPrepared)
{
	SingleValueResultContext context;
	char *sql = "SELECT 1 FROM pg_prepared_xacts WHERE gid = $1";
	PGSQL *pgsql = &coordinator->pgsql;

	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	int groupId = keeper->state.current_group;

	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	GetPreparedTransactionName(groupId, transactionName);

	paramValues[0] = transactionName;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &fetchedRows))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to look up pg_prepared_xacts, see above for details");
		return false;
	}

	/* Our query returns one row when the prepared transaction exists */
	*transactionHasBeenPrepared = (context.intVal == 1);

	return true;
}


/*
 * coordinator_supports_force_master_update_node probes the possibility to call
 * master_update_node with the force flag to guarantee a failover during lock
 * contention on the database.
 *
 * The function puts the status of force flag into supportForForce upon successful
 * run and returns true. It returns false upon failure.
 */
static bool
coordinator_supports_force_master_update_node(PGSQL *pgsql,
											  bool *supportForForce)
{
	/*
	 * sql query for probing the availability of force and lock_cooldown fields in
	 * master_update_node together with 3 arguments without default values.
	 */
	const char *sql = "SELECT count(*) > 0 AS has_force_support\n"
					  "  FROM pg_proc\n"
					  " WHERE proname = 'master_update_node'\n"
					  "   AND proargnames @> ARRAY['force', 'lock_cooldown']\n"
					  "   AND pronargs - pronargdefaults = 3";
	SingleValueResultContext context;

	context.resultType = PGSQL_RESULT_BOOL;
	context.parsedOk = false;

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

	/* boolval contains the boolean value returned by the query */
	*supportForForce = context.boolVal;
	return true;
}


/*
 * coordinator_master_activate_node_returns_record probes the signature of the
 * master_activate_node function, which changed in Citus 9.0 to return an int
 * rather than a record.
 */
static bool
coordinator_master_activate_node_returns_record(PGSQL *pgsql,
												bool *returnsRecord)
{
	const char *sql =
		"SELECT typname = 'record' FROM pg_type pt JOIN pg_proc pp "
		"ON (pt.oid = pp.prorettype) where pp.proname = 'master_activate_node'";
	SingleValueResultContext context;

	context.resultType = PGSQL_RESULT_BOOL;
	context.parsedOk = false;

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

	/* boolval contains the boolean value returned by the query */
	*returnsRecord = context.boolVal;
	return true;
}


/*
 * coordinator_update_node_prepare call master_update_node() on the formation's
 * coordinator node, in a prepared transaction named "master_update_node
 * ${groupid}".
 */
bool
coordinator_update_node_prepare(Coordinator *coordinator, Keeper *keeper)
{
	SingleValueResultContext context = { 0 };
	char sql[BUFSIZE];
	PGSQL *pgsql = &(coordinator->pgsql);
	bool supportForForce = false;

	char transactionName[PREPARED_TRANSACTION_NAMELEN] = { 0 };
	int groupId = keeper->state.current_group;

	GetPreparedTransactionName(groupId, transactionName);

	if (!pgsql_begin(pgsql))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	bool transactionHasAlreadyBeenPrepared = false;

	if (!coordinator_udpate_node_transaction_is_prepared(
			coordinator,
			keeper,
			&transactionHasAlreadyBeenPrepared))
	{
		log_error("Failed to update node %s:%d on the coordinator, "
				  "see above for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport);
		return false;
	}

	if (transactionHasAlreadyBeenPrepared)
	{
		log_warn("Transaction \"%s\" has already been prepared, skipping",
				 transactionName);
		log_debug("ROLLBACK");

		if (!pgsql_rollback(pgsql))
		{
			log_error("Failed to ROLLBACK failed master_update_node transaction "
					  " on the coordinator, see above for details.");
			return false;
		}

		return true;
	}

	if (!coordinator_supports_force_master_update_node(pgsql, &supportForForce))
	{
		log_error("Failed to update node %s:%d on the coordinator, "
				  "see above for details",
				  keeper->config.hostname, keeper->config.pgSetup.pgport);
		return false;
	}

	if (!supportForForce)
	{
		log_warn("Current version of citus does not support a forced "
				 " master_update_node. "
				 "Failover needs to wait till all pending transactions "
				 "on the old worker are either committed or aborted, "
				 "which might take a while. For faster "
				 "failovers update the citus extension to the latest version.");
	}

	/*
	 * Now call master_update_node() on the nodeid from our group. Also make
	 * sure that the metadata are in sync in between the monitor, the keeper,
	 * and the coordinator node by adding all we know in the WHERE clause of
	 * the query.
	 *
	 * We have the groupid from the monitor, but we don't have the
	 * nodeid from the coordinator on the keeper: we don't need it, that's
	 * private data handled by the coordinator, and the coordinator is going to
	 * provide for that information itself with the following SQL query.
	 */
	if (supportForForce)
	{
		const int paramCount = 5;
		Oid paramTypes[5] = { INT4OID, TEXTOID, INT4OID, TEXTOID, INT4OID };
		const char *paramValues[5];

		sformat(sql,
				sizeof(sql),
				"SELECT master_update_node(nodeid, $2, $3, "
				"                          force => true, lock_cooldown => $5)"
				"  FROM pg_dist_node "
				" WHERE groupid = $1"
				"   and noderole = 'primary'"
				"   and not exists"
				"        (select 1 from pg_prepared_xacts where gid = $4)");

		paramValues[0] = intToString(groupId).strValue;
		paramValues[1] = keeper->config.hostname;
		paramValues[2] = intToString(keeper->config.pgSetup.pgport).strValue;
		paramValues[3] = transactionName;
		paramValues[4] = intToString(
			keeper->config.citus_master_update_node_lock_cooldown).strValue;

		if (!pgsql_execute_with_params(pgsql, sql,
									   paramCount, paramTypes, paramValues,
									   &context, &fetchedRows))
		{
			log_error("Failed to update node on the coordinator, "
					  "see above for details.");
			return false;
		}
	}
	else
	{
		const int paramCount = 4;
		Oid paramTypes[4] = { INT4OID, TEXTOID, INT4OID, TEXTOID };
		const char *paramValues[4];

		sformat(sql,
				sizeof(sql),
				"SELECT master_update_node(nodeid, $2, $3)"
				"  FROM pg_dist_node "
				" WHERE groupid = $1"
				"   and noderole = 'primary'"
				"   and not exists"
				"        (select 1 from pg_prepared_xacts where gid = $4)");

		paramValues[0] = intToString(groupId).strValue;
		paramValues[1] = keeper->config.hostname;
		paramValues[2] = intToString(keeper->config.pgSetup.pgport).strValue;
		paramValues[3] = transactionName;

		if (!pgsql_execute_with_params(pgsql, sql,
									   paramCount, paramTypes, paramValues,
									   &context, &fetchedRows))
		{
			log_error("Failed to update node on the coordinator, "
					  "see above for details.");
			return false;
		}
	}


	/*
	 * We expect our SQL query to find the current Citus 'primary' node and
	 * call master_update_node() to change its host:port metadata.
	 *
	 * Also the query is protected against the prepared transaction having been
	 * prepared in a previous run already, though we know about that in the
	 * boolean transactionHasAlreadyBeenPrepared that we fetch from the
	 * coordinator explicitely in a previous query.
	 *
	 * So if the query returns zero rows (or anything other than one row
	 * really) then the only explaination is that the target node isn't
	 * registered in pg_dist_node: there is currently no row for our groupId in
	 * pg_dist_node. This may happen when all the nodes have been previously
	 * removed, and a new node is now added.
	 */
	if (!context.parsedOk || context.intVal != 1)
	{
		/* we still want to PREPARE TRANSACTION here */
		log_info("There is currently no node in group %d "
				 "for nodecluster 'default' on the coordinator, continuing",
				 groupId);
	}

	if (keeper->config.pgSetup.proxyport > 0)
	{
		if (!coordinator_upsert_poolinfo_port(coordinator, keeper))
		{
			log_error("Failed to add proxyport to pg_dist_poolinfo, "
					  "see above for details");
			return false;
		}
	}

	sformat(sql, sizeof(sql), "PREPARE TRANSACTION '%s'", transactionName);

	if (!pgsql_execute(pgsql, sql))
	{
		log_error("Failed to update node on the coordinator, "
				  "see above for details.");
		return false;
	}

	/* and disconnect now that we prepared the transaction */
	pgsql_finish(pgsql);

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
	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	int groupId = keeper->state.current_group;
	PGSQL *pgsql = &coordinator->pgsql;

	GetPreparedTransactionName(groupId, transactionName);

	sformat(sql, sizeof(sql), "COMMIT PREPARED '%s'", transactionName);

	if (!pgsql_execute(pgsql, sql))
	{
		log_error("Failed to commit prepared master_update_node "
				  "on the coordinator, see above for details.");
		return false;
	}

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
	char transactionName[PREPARED_TRANSACTION_NAMELEN];
	int groupId = keeper->state.current_group;
	PGSQL *pgsql = &coordinator->pgsql;

	GetPreparedTransactionName(groupId, transactionName);

	sformat(sql, sizeof(sql), "ROLLBACK PREPARED '%s'", transactionName);

	if (!pgsql_execute(pgsql, sql))
	{
		log_error("Failed to rollback prepared master_update_node "
				  "on the coordinator, see above for details.");
		return false;
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
	sformat(name, PREPARED_TRANSACTION_NAMELEN,
			"master_update_node %d", groupId);
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
	Oid paramTypes[2] = { INT4OID, TEXTOID };
	const char *paramValues[2];
	char proxyInfo[MAXCONNINFO];

	/*
	 * Prepare a argument for pg_dist_poolinfo table
	 */
	sformat(proxyInfo, sizeof(proxyInfo), "host=%s port=%d",
			keeper->config.hostname, keeper->config.pgSetup.proxyport);

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


/*
 * coordinator_node_is_registered checks to see if the coordinator node itself
 * has been registered in pg_dist_node.
 */
bool
coordinator_node_is_registered(Coordinator *coordinator, bool *isRegistered)
{
	SingleValueResultContext context;
	PGSQL *pgsql = &coordinator->pgsql;
	const char *sql = "SELECT 1 FROM pg_dist_node WHERE groupid = 0";

	if (!pgsql_execute_with_params(pgsql, sql, 0, NULL, NULL,
								   &context, &fetchedRows))
	{
		log_error("Failed to check if the coordinator is registered in "
				  "pg_dist_node, see above for details");
		return false;
	}

	*isRegistered = context.parsedOk && context.intVal == 1;

	return true;
}


typedef struct RemovedNodeIdsContext
{
	char sqlstate[SQLSTATE_LENGTH];
	bool parsedOK;
} RemovedNodeIdsContext;


/*
 * coordinator_remove_dropped_nodes calls Citus function master_remove_node on
 * nodes that are still in pg_dist_node but no longer returned by the monitor's
 * pgautofailover.current_state function. The result of the monitor state
 * function is expected in the nodesArray parameter.
 */
bool
coordinator_remove_dropped_nodes(Coordinator *coordinator,
								 CurrentNodeStateArray *nodesArray)
{
	PQExpBuffer query = createPQExpBuffer();
	PQExpBuffer values = createPQExpBuffer();

	/* *INDENT-OFF* */
	char *sqlTemplate =
		/*
		 * We join the coordinator pg_dist_node table with the monitor's list
		 * of nodes in the formation, and remove from the coordinator any node
		 * for which there is no entry on the monitor in the same groupId and
		 * nodecluster.
		 *
		 * That way, during a failover, as long as we have a secondary in the
		 * same group, we leave it to master_update_node to edit the entry in
		 * the coordinator during normal operations.
		 */
		" WITH nodes(groupid, nodecluster) as "
		" ( "
		"     VALUES %s "
		" ), "
		"      nodes_to_drop(groupid, nodename, nodeport) as "
		" ( "
		"   SELECT pg_dist_node.groupid, "
		"          pg_dist_node.nodename, "
		"          pg_dist_node.nodeport "
		"     FROM pg_dist_node "
		"          LEFT JOIN nodes "
		"                 ON pg_dist_node.groupid = nodes.groupid "
		"                AND pg_dist_node.nodecluster = nodes.nodecluster "
		"    WHERE nodes.groupid IS NULL "
		" ) "
		"  SELECT groupid, nodename, nodeport, "
		"         master_remove_node(nodename, nodeport) "
		"    FROM nodes_to_drop";
	/* *INDENT-ON* */

	int paramCount = 0;
	Oid paramTypes[2 * NODE_ARRAY_MAX_COUNT] = { 0 };

	const char *paramValues[2 * NODE_ARRAY_MAX_COUNT] = { 0 };
	IntString groupIdStrings[NODE_ARRAY_MAX_COUNT] = { 0 };

	RemovedNodeIdsContext context = { { 0 }, false };

	/* when the array is empty we're done already */
	if (nodesArray->count == 0)
	{
		return true;
	}

	/* prepare the VALUES string */
	for (int i = 0; i < nodesArray->count; i++)
	{
		/*
		 * VALUES ($1::int), ($2), ($3) ...
		 *
		 * We fill in the values with the nodes group ids, that are then
		 * matched with the pg_dist_node.groupid column.
		 */
		appendPQExpBuffer(values,
						  "%s($%d%s, $%d%s)",
						  paramCount == 0 ? "" : ", ",
						  paramCount + 1,
						  paramCount == 0 ? "::int" : "",
						  paramCount + 2,
						  paramCount == 0 ? "::text" : "");

		/* also fill-in paramTypes */
		paramTypes[paramCount] = INT4OID;
		paramTypes[paramCount + 1] = TEXTOID;

		/* also fill-in paramValues */
		groupIdStrings[i] = intToString(nodesArray->nodes[i].groupId);

		paramValues[paramCount++] = groupIdStrings[i].strValue;
		paramValues[paramCount++] = nodesArray->nodes[i].citusClusterName;
	}

	/* add the computed ($1,$2), ... string to the query "template" */
	appendPQExpBuffer(query, sqlTemplate, values->data);

	PGSQL *pgsql = &coordinator->pgsql;

	if (!pgsql_execute_with_params(pgsql, query->data,
								   paramCount, paramTypes, paramValues,
								   &context, parseRemovedNodeIds))
	{
		log_error("Failed to check if pg_dist_node contains entries for nodes "
				  "that have been deleted from the monitor");

		PQfreemem(query);
		PQfreemem(values);

		return false;
	}

	PQfreemem(query);
	PQfreemem(values);

	return context.parsedOK;
}


/*
 * parseRemovedNodeIds parses a nodeId from the libpq result and displays a log
 * entry for each removed Node from the pg_dist_node table.
 */
static void
parseRemovedNodeIds(void *ctx, PGresult *result)
{
	RemovedNodeIdsContext *context = (RemovedNodeIdsContext *) ctx;

	if (PQntuples(result) > NODE_ARRAY_MAX_COUNT)
	{
		log_error("Query returned %d rows, pg_auto_failover supports only up "
				  "to %d nodes at the moment",
				  PQntuples(result), NODE_ARRAY_MAX_COUNT);
		context->parsedOK = false;
		return;
	}

	/* our query returns 4 columns */
	if (PQnfields(result) != 4)
	{
		log_error("Query returned %d columns, expected 4", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	for (int rowNumber = 0; rowNumber < PQntuples(result); rowNumber++)
	{
		char *value = PQgetvalue(result, rowNumber, 0);

		int groupId = strtol(value, NULL, 0);
		if (groupId == 0)
		{
			log_error("Invalid groupId \"%s\" returned by monitor", value);
			context->parsedOK = false;
			return;
		}

		char *nodehost = PQgetvalue(result, rowNumber, 1);
		char *nodeport = PQgetvalue(result, rowNumber, 2);

		log_info("Citus worker node in group %d (%s:%s) "
				 "has been removed from pg_dist_node "
				 "after being dropped from the monitor",
				 groupId, nodehost, nodeport);
	}

	context->parsedOK = true;
}
