/*
 * src/bin/pg_autoctl/monitor.c
 *	 API for interacting with the monitor
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <limits.h>
#include <sys/select.h>
#include <unistd.h>

#include "defaults.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "parsing.h"
#include "pgsql.h"

#define STR_ERRCODE_OBJECT_IN_USE "55006"

typedef struct NodeAddressParseContext
{
	char sqlstate[SQLSTATE_LENGTH];
	NodeAddress *node;
	bool parsedOK;
} NodeAddressParseContext;

typedef struct NodeAddressArrayParseContext
{
	char sqlstate[SQLSTATE_LENGTH];
	NodeAddressArray *nodesArray;
	bool parsedOK;
} NodeAddressArrayParseContext;

typedef struct MonitorAssignedStateParseContext
{
	char sqlstate[SQLSTATE_LENGTH];
	MonitorAssignedState *assignedState;
	bool parsedOK;
} MonitorAssignedStateParseContext;

typedef struct NodeReplicationSettingsParseContext
{
	char sqlstate[SQLSTATE_LENGTH];
	int candidatePriority;
	bool replicationQuorum;
	bool parsedOK;
} NodeReplicationSettingsParseContext;

typedef struct MonitorExtensionVersionParseContext
{
	char sqlstate[SQLSTATE_LENGTH];
	MonitorExtensionVersion *version;
	bool parsedOK;
} MonitorExtensionVersionParseContext;

static bool parseNode(PGresult *result, int rowNumber, NodeAddress *node);
static void parseNodeResult(void *ctx, PGresult *result);
static void parseNodeArray(void *ctx, PGresult *result);
static void parseNodeState(void *ctx, PGresult *result);
static void parseNodeReplicationSettings(void *ctx, PGresult *result);
static void printCurrentState(void *ctx, PGresult *result);
static void printLastEvents(void *ctx, PGresult *result);
static void parseCoordinatorNode(void *ctx, PGresult *result);
static void parseExtensionVersion(void *ctx, PGresult *result);

static bool prepare_connection_to_current_system_user(Monitor *source,
													  Monitor *target);

/*
 * monitor_init initialises a Monitor struct to connect to the given
 * database URL.
 */
bool
monitor_init(Monitor *monitor, char *url)
{
	if (!pgsql_init(&monitor->pgsql, url, PGSQL_CONN_MONITOR))
	{
		/* URL must be invalid, pgsql_init logged an error */
		return false;
	}

	return true;
}


/*
 * monitor_get_other_node gets the hostname and port of the other node
 * in the group.
 */
bool
monitor_get_nodes(Monitor *monitor, char *formation, int groupId,
				  NodeAddressArray *nodeArray)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		groupId == -1
		? "SELECT * FROM pgautofailover.get_nodes($1)"
		: "SELECT * FROM pgautofailover.get_nodes($1, $2)";
	int paramCount = 1;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2] = { 0 };
	NodeAddressArrayParseContext parseContext = { { 0 }, nodeArray, false };

	paramValues[0] = formation;

	if (groupId > -1)
	{
		IntString myGroupIdString = intToString(groupId);

		++paramCount;
		paramValues[1] = myGroupIdString.strValue;
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeArray))
	{
		log_error("Failed to get other nodes from the monitor while running "
				  "\"%s\" with formation %s and group %d",
				  sql, formation, groupId);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to get the other nodes from the monitor while "
				  "running \"%s\" with formation %s and group %d because "
				  "it returned an unexpected result. "
				  "See previous line for details.",
				  sql, formation, groupId);
		return false;
	}

	return true;
}


/*
 * monitor_get_other_node gets the hostname and port of the other node
 * in the group.
 */
bool
monitor_get_nodes_as_json(Monitor *monitor, char *formation, int groupId,
						  char *json, int size)
{
	PGSQL *pgsql = &monitor->pgsql;
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_STRING, false };

	const char *sql =
		groupId == -1
		?
		"SELECT jsonb_pretty(jsonb_agg(row_to_json(nodes)))"
		"  FROM pgautofailover.get_nodes($1) as nodes"
		:
		"SELECT jsonb_pretty(jsonb_agg(row_to_json(nodes)))"
		"  FROM pgautofailover.get_nodes($1, $2) as nodes";

	int paramCount = 1;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2] = { 0 };

	paramValues[0] = formation;

	if (groupId > -1)
	{
		IntString myGroupIdString = intToString(groupId);

		++paramCount;
		paramValues[1] = myGroupIdString.strValue;
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to get the nodes from the monitor while running "
				  "\"%s\" with formation %s and group %d",
				  sql, formation, groupId);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOk)
	{
		log_error("Failed to get the other nodes from the monitor while "
				  "running \"%s\" with formation %s and group %d because "
				  "it returned an unexpected result. "
				  "See previous line for details.",
				  sql, formation, groupId);
		return false;
	}

	strlcpy(json, context.strVal, size);

	return true;
}


/*
 * monitor_get_other_node gets the hostname and port of the other node
 * in the group.
 */
bool
monitor_get_other_nodes(Monitor *monitor,
						char *myHost, int myPort, NodeState currentState,
						NodeAddressArray *nodeArray)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		currentState == ANY_STATE
		? "SELECT * FROM pgautofailover.get_other_nodes($1, $2)"
		: "SELECT * FROM pgautofailover.get_other_nodes($1, $2, "
		"$3::pgautofailover.replication_state)";
	int paramCount = 2;
	Oid paramTypes[3] = { TEXTOID, INT4OID, TEXTOID };
	const char *paramValues[3] = { 0 };
	NodeAddressArrayParseContext parseContext = { { 0 }, nodeArray, false };
	IntString myPortString = intToString(myPort);

	paramValues[0] = myHost;
	paramValues[1] = myPortString.strValue;

	if (currentState != ANY_STATE)
	{
		++paramCount;
		paramValues[2] = NodeStateToString(currentState);
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeArray))
	{
		log_error("Failed to get other nodes from the monitor while running "
				  "\"%s\" with host %s and port %d", sql, myHost, myPort);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to get the other nodes from the monitor while running "
				  "\"%s\" with host %s and port %d because it returned an "
				  "unexpected result. See previous line for details.",
				  sql, myHost, myPort);
		return false;
	}

	return true;
}


/*
 * monitor_get_other_node gets the hostname and port of the other node
 * in the group.
 */
bool
monitor_get_other_nodes_as_json(Monitor *monitor,
								char *myHost, int myPort,
								NodeState currentState,
								char *json, int size)
{
	PGSQL *pgsql = &monitor->pgsql;
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_STRING, false };

	const char *sql =
		currentState == ANY_STATE
		?
		"SELECT jsonb_pretty(jsonb_agg(row_to_json(nodes)))"
		" FROM pgautofailover.get_other_nodes($1, $2) as nodes"
		:
		"SELECT jsonb_pretty(jsonb_agg(row_to_json(nodes)))"
		" FROM pgautofailover.get_other_nodes($1, $2, "
		"$3::pgautofailover.replication_state) as nodes";

	int paramCount = 2;
	Oid paramTypes[3] = { TEXTOID, INT4OID, TEXTOID };
	const char *paramValues[3] = { 0 };
	IntString myPortString = intToString(myPort);
	int resultSize = 0;

	paramValues[0] = myHost;
	paramValues[1] = myPortString.strValue;

	if (currentState != ANY_STATE)
	{
		++paramCount;
		paramValues[2] = NodeStateToString(currentState);
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to get the other nodes from the monitor while running "
				  "\"%s\" with host %s and port %d", sql, myHost, myPort);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOk)
	{
		log_error("Failed to get the other nodes from the monitor while running "
				  "\"%s\" with host %s and port %d because it returned an "
				  "unexpected result. See previous line for details.",
				  sql, myHost, myPort);
		return false;
	}

	resultSize = strlcpy(json, context.strVal, size);
	free(context.strVal);

	if (resultSize >= size)
	{
		log_error("Failed to get the other nodes as JSON. A string of %d bytes "
				  "would have been necessary and pg_autoctl only supports up to"
				  "%d bytes", resultSize, size);
		return false;
	}

	return true;
}


/*
 * monitor_get_primary gets the primary node in a give formation and group.
 */
bool
monitor_get_primary(Monitor *monitor, char *formation, int groupId,
					NodeAddress *node)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.get_primary($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];
	NodeAddressParseContext parseContext = { { 0 }, node, false };
	IntString groupIdString = intToString(groupId);

	paramValues[0] = formation;
	paramValues[1] = groupIdString.strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeResult))
	{
		log_error(
			"Failed to get the primary node in the HA group from the monitor "
			"while running \"%s\" with formation \"%s\" and group ID %d",
			sql, formation, groupId);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error(
			"Failed to get the primary node from the monitor while running "
			"\"%s\" with formation \"%s\" and group ID %d because it returned an "
			"unexpected result. See previous line for details.",
			sql, formation, groupId);
		return false;
	}

	/* The monitor function pgautofailover.get_primary only returns 3 fields */
	node->isPrimary = true;

	log_debug("The primary node returned by the monitor is %s:%d, with id %d",
			  node->host, node->port, node->nodeId);

	return true;
}


/*
 * monitor_get_coordinator gets the coordinator node in a given formation.
 */
bool
monitor_get_coordinator(Monitor *monitor, char *formation, NodeAddress *node)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.get_coordinator($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];
	NodeAddressParseContext parseContext = { { 0 }, node, false };

	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseCoordinatorNode))
	{
		log_error("Failed to get the coordinator node from the monitor, "
				  "while running \"%s\" with formation \"%s\".",
				  sql, formation);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to get the coordinator node from the monitor "
				  "while running \"%s\" with formation \"%s\" "
				  "because it returned an unexpected result. "
				  "See previous line for details.",
				  sql, formation);
		return false;
	}

	if (parseContext.node == NULL)
	{
		log_error("Failed to get the coordinator node from the monitor: "
				  "the monitor returned an empty result set, there's no "
				  "known available coordinator node at this time in "
				  "formation \"%s\"", formation);
		return false;
	}

	log_debug("The coordinator node returned by the monitor is %s:%d",
			  node->host, node->port);

	return true;
}


/*
 * monitor_register_node performs the initial registration of a node with the
 * monitor in the given formation.
 *
 * The caller can specify a desired group ID, which will result in the node
 * being added to the group unless it is already full. If the groupId is -1,
 * the monitor will pick a group.
 *
 * The initialState can be used to indicate that the operator wants to
 * initialize the node in a specific state directly. This can be useful to add
 * a standby to an already running primary node, doing the pg_basebackup
 * directly.
 *
 * The initialState can also be used to indicate that the node is already
 * correctly initialised in a particular state. This can be useful when
 * bringing back a keeper after replacing the monitor.
 *
 * The node ID and group ID selected by the monitor, as well as the goal
 * state, are set in assignedState, which must not be NULL.
 */
bool
monitor_register_node(Monitor *monitor, char *formation, char *host, int port,
					  char *dbname, int desiredGroupId, NodeState initialState,
					  PgInstanceKind kind, int candidatePriority, bool quorum,
					  MonitorAssignedState *assignedState)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT * FROM pgautofailover.register_node($1, $2, $3, $4, $5, "
		"$6::pgautofailover.replication_state, $7, $8, $9)";
	int paramCount = 9;
	Oid paramTypes[9] = { TEXTOID, TEXTOID, INT4OID, NAMEOID, INT4OID,
						  TEXTOID, TEXTOID, INT4OID, BOOLOID };
	const char *paramValues[9];
	MonitorAssignedStateParseContext parseContext =
		{ { 0 }, assignedState, false };
	const char *nodeStateString = NodeStateToString(initialState);

	paramValues[0] = formation;
	paramValues[1] = host;
	paramValues[2] = intToString(port).strValue;
	paramValues[3] = dbname;
	paramValues[4] = intToString(desiredGroupId).strValue;
	paramValues[5] = nodeStateString;
	paramValues[6] = nodeKindToString(kind);
	paramValues[7] = intToString(candidatePriority).strValue;
	paramValues[8] = quorum ? "true" : "false";


	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeState))
	{
		/* disconnect from PostgreSQL now */
		pgsql_finish(&monitor->pgsql);

		if (strcmp(parseContext.sqlstate, STR_ERRCODE_OBJECT_IN_USE) == 0)
		{
			log_warn("Failed to register node %s:%d in group %d of "
					 "formation \"%s\" with initial state \"%s\" "
					 "because the monitor is already registering another "
					 "standby, retrying in %ds",
					 host, port, desiredGroupId, formation, nodeStateString,
					 PG_AUTOCTL_KEEPER_SLEEP_TIME);

			sleep(PG_AUTOCTL_KEEPER_SLEEP_TIME);
			return monitor_register_node(monitor, formation, host, port,
										 dbname, desiredGroupId, initialState,
										 kind, candidatePriority, quorum,
										 assignedState);
		}

		log_error("Failed to register node %s:%d in group %d of formation \"%s\" "
				  "with initial state \"%s\", see previous lines for details",
				  host, port, desiredGroupId, formation, nodeStateString);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to register node %s:%d in group %d of formation \"%s\" "
				  "with initial state \"%s\" because the monitor returned an "
				  "unexpected result, see previous lines for details",
				  host, port, desiredGroupId, formation, nodeStateString);
		return false;
	}

	log_info("Registered node %s:%d with id %d in formation \"%s\", "
			 "group %d, state \"%s\"",
			 host, port, assignedState->nodeId,
			 formation, assignedState->groupId,
			 NodeStateToString(assignedState->state));

	return true;
}


/*
 * monitor_node_active communicates the current state of the node to the
 * monitor and puts the new goal state to assignedState, which must not
 * be NULL.
 */
bool
monitor_node_active(Monitor *monitor,
					char *formation, char *host, int port, int nodeId,
					int groupId, NodeState currentState,
					bool pgIsRunning,
					char *currentLSN, char *pgsrSyncState,
					MonitorAssignedState *assignedState)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT * FROM pgautofailover.node_active($1, $2, $3, $4, $5, "
		"$6::pgautofailover.replication_state, $7, $8, $9)";
	int paramCount = 9;
	Oid paramTypes[9] = { TEXTOID, TEXTOID, INT4OID, INT4OID,
						  INT4OID, TEXTOID, BOOLOID, LSNOID, TEXTOID };
	const char *paramValues[9];
	MonitorAssignedStateParseContext parseContext =
		{ { 0 }, assignedState, false };
	const char *nodeStateString = NodeStateToString(currentState);

	paramValues[0] = formation;
	paramValues[1] = host;
	paramValues[2] = intToString(port).strValue;
	paramValues[3] = intToString(nodeId).strValue;
	paramValues[4] = intToString(groupId).strValue;
	paramValues[5] = nodeStateString;
	paramValues[6] = pgIsRunning ? "true" : "false";
	paramValues[7] = currentLSN;
	paramValues[8] = pgsrSyncState;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeState))
	{
		log_error("Failed to get node state for node %d (%s:%d) "
				  "in group %d of formation \"%s\" with initial state "
				  "\"%s\", replication state \"%s\", "
				  "and current lsn \"%s\", "
				  "see previous lines for details",
				  nodeId, host, port, groupId, formation, nodeStateString,
				  pgsrSyncState, currentLSN);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error("Failed to get node state for node %d (%s:%d) in group %d of formation "
				  "\"%s\" with initial state \"%s\", replication state \"%s\","
				  " and current lsn \"%s\""
				  " because the monitor returned an unexpected result, "
				  "see previous lines for details",
				  nodeId, host, port, groupId, formation, nodeStateString,
				  pgsrSyncState, currentLSN);
		return false;
	}

	return true;
}


/*
 * monitor_set_node_candidate_priority updates the monitor on the changes
 * in the node candidate priority.
 */
bool
monitor_set_node_candidate_priority(Monitor *monitor,
									int nodeid, char* nodeName, int nodePort,
									int candidate_priority)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT pgautofailover.set_node_candidate_priority($1, $2, $3, $4)";
	int paramCount = 4;
	Oid paramTypes[4] = { INT4OID, TEXTOID, INT4OID, INT4OID};
	const char *paramValues[4];
	char *candidatePriorityText = intToString(candidate_priority).strValue;
	bool success = true;

	paramValues[0] = intToString(nodeid).strValue;
	paramValues[1] = nodeName,
	paramValues[2] = intToString(nodePort).strValue;
	paramValues[3] = candidatePriorityText;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to update node candidate priority on node %d"
				  " for candidate_priority: \"%s\"",
				  nodeid, candidatePriorityText);

		success = false;
	}

	return success;
}


/*
 * monitor_set_node_replication_quorum updates the monitor on the changes
 * in the node replication quorum.
 */
bool
monitor_set_node_replication_quorum(Monitor *monitor, int nodeid,
									char* nodeName, int nodePort,
									bool replicationQuorum)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT pgautofailover.set_node_replication_quorum($1, $2, $3, $4)";
	int paramCount = 4;
	Oid paramTypes[4] = { INT4OID, TEXTOID, INT4OID, BOOLOID };
	const char *paramValues[4];
	char *replicationQuorumText = replicationQuorum ? "true" : "false";
	bool success = true;

	paramValues[0] = intToString(nodeid).strValue;
	paramValues[1] = nodeName;
	paramValues[2] = intToString(nodePort).strValue;
	paramValues[3] = replicationQuorumText;

	if (!pgsql_execute_with_params(pgsql, sql, paramCount, paramTypes,
								   paramValues, NULL, NULL))
	{
		log_error("Failed to update node replication quorum on node %d"
				  " and replication_quorum: \"%s\"",
				  nodeid, replicationQuorumText);

		success = false;
	}

	return success;
}

/*
 * monitor_get_node_replication_settings retrieves replication settings
 * from the monitor.
 */
bool
monitor_get_node_replication_settings(Monitor *monitor, int nodeid,
		 	 	 	 	 	 	 	  NodeReplicationSettings *settings)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT candidatepriority, replicationquorum FROM pgautofailover.node "
		"WHERE nodeid = $1";
	int paramCount = 1;
	Oid paramTypes[1] = { INT4OID };
	const char *paramValues[1];
	NodeReplicationSettingsParseContext parseContext =
		{ { 0 }, -1, false, false };

	paramValues[0] = intToString(nodeid).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeReplicationSettings))
	{
		log_error("Failed to retrieve node settings for node \"%d\".", nodeid);
		/* disconnect from monitor */
		pgsql_finish(&monitor->pgsql);

		return false;
	}

	/* disconnect from monitor */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		return false;
	}

	settings->candidatePriority = parseContext.candidatePriority;
	settings->replicationQuorum = parseContext.replicationQuorum;

	return true;
}


/*
 * parseNodeReplicationSettings parses nore replication settings
 * from query output.
 */
static void
parseNodeReplicationSettings(void *ctx, PGresult *result)
{
	NodeReplicationSettingsParseContext *context =
		(NodeReplicationSettingsParseContext *) ctx;
	char *value = NULL;
	int errors = 0;

	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 2)
	{
		log_error("Query returned %d columns, expected 2", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 0);
	if (sscanf(value, "%d", &context->candidatePriority) != 1)
	{
		log_error("Invalid failover candidate priority \"%s\" "
				  "returned by monitor", value);
		++errors;
	}

	value = PQgetvalue(result, 0, 1);
	if (value == NULL || ( (*value != 't') && (*value != 'f')))
	{
		log_error("Invalid replication quorum \"%s\" "
				  "returned by monitor", value);
		++errors;
	}
	else
	{
		context->replicationQuorum = (*value) =='t';
	}

	if (errors > 0)
	{
		context->parsedOK = false;
		return;
	}

	/* if we reach this line, then we're good. */
	context->parsedOK = true;
}


/*
 * monitor_get_formation_number_sync_standbys retrieves number-sync-standbys
 * property for formation from the monitor. The function returns true upon
 * success.
 */
bool
monitor_get_formation_number_sync_standbys(Monitor *monitor, char *formation,
										   int *numberSyncStandbys)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT number_sync_standbys FROM pgautofailover.formation "
		"WHERE formationid = $1";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID};
	const char *paramValues[1];
	SingleValueResultContext parseContext = { { 0 }, PGSQL_RESULT_INT, false };
	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseSingleValueResult))
	{
		log_error("Failed to retrieve settings for formation \"%s\".",
				  formation);

		/* disconnect from monitor */
		pgsql_finish(&monitor->pgsql);

		return false;
	}

	/* disconnect from monitor */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOk)
	{
		return false;
	}

	*numberSyncStandbys = parseContext.intVal;

	return true;
}


/*
 * monitor_set_formation_number_sync_standbys sets number-sync-standbys
 * property for formation at the monitor. The function returns true upon
 * success.
 */
bool
monitor_set_formation_number_sync_standbys(Monitor *monitor, char *formation,
										   int numberSyncStandbys)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT pgautofailover.set_formation_number_sync_standbys($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID};
	const char *paramValues[2];
	SingleValueResultContext parseContext = { { 0 }, PGSQL_RESULT_BOOL, false };
	paramValues[0] = formation;
	paramValues[1] = intToString(numberSyncStandbys).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseSingleValueResult))
	{
		log_error("Failed to update number-sync-standbys for formation \"%s\".",
				  formation);

		/* disconnect from monitor */
		pgsql_finish(&monitor->pgsql);

		return false;
	}

	if (!parseContext.parsedOk)
	{
		return false;
	}

	return parseContext.boolVal;
}


/*
 * monitor_remove calls the pgautofailover.monitor_remove function on the
 * monitor.
 */
bool
monitor_remove(Monitor *monitor, char *host, int port)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT pgautofailover.remove_node($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = host;
	paramValues[1] = intToString(port).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to remove node %s:%d from the monitor", host, port);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOk)
	{
		log_error("Failed to remove node %s:%d from the monitor: "
				  "could not parse monitor's result.", host, port);
		return false;
	}

	/*
	 * We ignore the return value of pgautofailover.remove_node:
	 *  - if it's true, then the node has been removed
	 *  - if it's false, then the node didn't exist in the first place
	 *
	 * The only case where we return false here is when we failed to run the
	 * pgautofailover.remove_node function on the monitor, see above.
	 */
	return true;
}


/*
 * monitor_perform_failover calls the pgautofailover.monitor_perform_failover
 * function on the monitor.
 */
bool
monitor_perform_failover(Monitor *monitor, char *formation, int group)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT pgautofailover.perform_failover($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = formation;
	paramValues[1] = intToString(group).strValue;

	/*
	 * pgautofailover.perform_failover() returns VOID.
	 */
	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to perform failover for formation %s and group %d",
				  formation, group);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * parseNode parses a hostname and a port from the libpq result and writes
 * it to the NodeAddressParseContext pointed to by ctx.
 */
static bool
parseNode(PGresult *result, int rowNumber, NodeAddress *node)
{
	char *value = NULL;
	int hostLength = 0;

	if (PQgetisnull(result, rowNumber, 0)
		|| PQgetisnull(result, rowNumber, 1)
		|| PQgetisnull(result, rowNumber, 2))
	{
		log_error("NodeId, hostname or port returned by monitor is NULL");
		return false;
	}

	value = PQgetvalue(result, rowNumber, 0);
	node->nodeId = strtol(value, NULL, 0);
	if (node->nodeId == 0)
	{
		log_error("Invalid nodeId \"%s\" returned by monitor", value);
		return false;
	}

	value = PQgetvalue(result, rowNumber, 1);
	hostLength = strlcpy(node->host, value, _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("Hostname \"%s\" returned by monitor is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, hostLength, _POSIX_HOST_NAME_MAX - 1);
		return false;
	}

	value = PQgetvalue(result, rowNumber, 2);
	node->port = strtol(value, NULL, 0);
	if (node->port == 0)
	{
		log_error("Invalid port number \"%s\" returned by monitor", value);
		return false;
	}

	/*
	 * pgautofailover.get_other_nodes also returns the LSN and is_primary bits
	 * of information.
	 */
	if (PQnfields(result) == 5)
	{
		/* we trust Postgres pg_lsn data type to fit in our PG_LSN_MAXLENGTH */
		value = PQgetvalue(result, rowNumber, 3);
		strlcpy(node->lsn, value, PG_LSN_MAXLENGTH);

		value = PQgetvalue(result, rowNumber, 4);
		node->isPrimary = strcmp(value, "t") == 0;
	}

	return true;
}


/*
 * parseNode parses a hostname and a port from the libpq result and writes
 * it to the NodeAddressParseContext pointed to by ctx.
 */
static void
parseNodeResult(void *ctx, PGresult *result)
{
	NodeAddressParseContext *context = (NodeAddressParseContext *) ctx;

	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 3)
	{
		log_error("Query returned %d columns, expected 3", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	context->parsedOK = parseNode(result, 0, context->node);
}


/*
 * parseNode parses a hostname and a port from the libpq result and writes
 * it to the NodeAddressParseContext pointed to by ctx.
 */
static void
parseNodeArray(void *ctx, PGresult *result)
{
	bool parsedOk = true;
	int rowNumber = 0;
	NodeAddressArrayParseContext *context = (NodeAddressArrayParseContext *) ctx;

	log_debug("parseNodeArray: %d", PQntuples(result));

	/* keep a NULL entry to mark the end of the array */
	if (PQntuples(result) > NODE_ARRAY_MAX_COUNT)
	{
		log_error("Query returned %d rows, pg_auto_failover supports only up "
				  "to %d standby nodes at the moment",
				  PQntuples(result), NODE_ARRAY_MAX_COUNT);
		context->parsedOK = false;
		return;
	}

	/* pgautofailover.get_other_nodes returns 5 columns */
	if (PQnfields(result) != 5)
	{
		log_error("Query returned %d columns, expected 5", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	context->nodesArray->count = PQntuples(result);

	for (rowNumber=0; rowNumber < PQntuples(result); rowNumber++)
	{
		NodeAddress *node = &(context->nodesArray->nodes[rowNumber]);

		parsedOk = parsedOk && parseNode(result, rowNumber, node);
	}

	context->parsedOK = parsedOk;
}


/*
 * printCurrentState loops over pgautofailover.current_state() results and prints
 * them, one per line.
 */
void
printNodeArray(NodeAddressArray *nodesArray)
{
	int nodesArrayIndex = 0;
	int maxNodeNameSize = 5;	/* strlen("Name") + 1, the header */
	char *nameSeparatorHeader = NULL;

	/*
	 * Dynamically adjust our display output to the length of the longer
	 * nodename in the result set
	 */
	for(nodesArrayIndex=0; nodesArrayIndex<nodesArray->count; nodesArrayIndex++)
	{
		NodeAddress node = nodesArray->nodes[nodesArrayIndex];

		if (strlen(node.host) > maxNodeNameSize)
		{
			maxNodeNameSize = strlen(node.host);
		}
	}

	(void) printNodeHeader(maxNodeNameSize);

	for(nodesArrayIndex=0; nodesArrayIndex<nodesArray->count; nodesArrayIndex++)
	{
		NodeAddress *node = &(nodesArray->nodes[nodesArrayIndex]);

		printNodeEntry(node);
	}

	fprintf(stdout, "\n");
}


/*
 * printNodeHeader pretty prints a header for a node list.
 */
void
printNodeHeader(int maxNodeNameSize)
{
	char nameSeparatorHeader[BUFSIZE] = { 0 };

	/* prepare a nice dynamic string of '-' as a header separator */
	for(int i=0; i<=maxNodeNameSize; i++)
	{
		if (i<maxNodeNameSize)
		{
			nameSeparatorHeader[i] = '-';
		}
		else
		{
			nameSeparatorHeader[i] = '\0';
			break;
		}
	}

	fprintf(stdout, "%3s | %*s | %6s | %18s | %8s\n",
			"ID", maxNodeNameSize, "Host", "Port", "LSN", "Primary?");

	fprintf(stdout, "%3s-+-%*s-+-%6s-+-%18s-+-%8s\n",
			"---", maxNodeNameSize, nameSeparatorHeader, "------",
			"------------------", "--------");
}


/*
 * printNodeEntry pretty prints a node.
 */
void
printNodeEntry(NodeAddress *node)
{
	fprintf(stdout, "%3d | %s | %6d | %18s | %8s\n",
			node->nodeId, node->host, node->port, node->lsn,
			node->isPrimary ? "yes" : "no");
}


/*
 * parseNodeState parses a node state coming back from a call to
 * register_node or node_active.
 */
static void
parseNodeState(void *ctx, PGresult *result)
{
	MonitorAssignedStateParseContext *context =
		(MonitorAssignedStateParseContext *) ctx;
	char *value = NULL;
	int errors = 0;

	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 5)
	{
		log_error("Query returned %d columns, expected 5", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 0);
	if (sscanf(value, "%d", &context->assignedState->nodeId) != 1)
	{
		log_error("Invalid node ID \"%s\" returned by monitor", value);
		++errors;
	}

	value = PQgetvalue(result, 0, 1);
	if (sscanf(value, "%d", &context->assignedState->groupId) != 1)
	{
		log_error("Invalid group ID \"%s\" returned by monitor", value);
		++errors;
	}

	value = PQgetvalue(result, 0, 2);
	context->assignedState->state = NodeStateFromString(value);
	if (context->assignedState->state == NO_STATE)
	{
		log_error("Invalid node state \"%s\" returned by monitor", value);
		++errors;
	}

	value = PQgetvalue(result, 0, 3);
	if (sscanf(value, "%d", &context->assignedState->candidatePriority) != 1)
	{
		log_error("Invalid failover candidate priority \"%s\" "
				  "returned by monitor", value);
		++errors;
	}

	value = PQgetvalue(result, 0, 4);
	if (value == NULL || ( (*value != 't') && (*value != 'f')))
	{
		log_error("Invalid replication quorum \"%s\" "
				  "returned by monitor", value);
		++errors;
	}
	else
	{
		context->assignedState->replicationQuorum = (*value) =='t';
	}


	if (errors > 0)
	{
		context->parsedOK = false;
		return;
	}

	/* if we reach this line, then we're good. */
	context->parsedOK = true;
}


/*
 * monitor_print_state calls the function pgautofailover.current_state on the
 * monitor, and prints a line of output per state record obtained.
 */
bool
monitor_print_state(Monitor *monitor, char *formation, int group)
{
	MonitorAssignedStateParseContext context = { 0 };
	PGSQL *pgsql = &monitor->pgsql;
	char *sql = NULL;
	int paramCount = 0;
	Oid paramTypes[2];
	const char *paramValues[2];
	IntString groupStr;

	log_trace("monitor_print_state(%s, %d)", formation, group);

	switch (group)
	{
		case -1:
		{
			sql = "SELECT * FROM pgautofailover.current_state($1)";

			paramCount = 1;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;

			break;
		}

		default:
		{
			sql = "SELECT * FROM pgautofailover.current_state($1,$2)";

			groupStr = intToString(group);

			paramCount = 2;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;
			paramTypes[1] = INT4OID;
			paramValues[1] = groupStr.strValue;

			break;
		}
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &printCurrentState))
	{
		log_error("Failed to retrieve current state from the monitor");
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOK)
	{
		log_error("Failed to parse current state from the monitor");
		return false;
	}

	return true;
}


/*
 * printCurrentState loops over pgautofailover.current_state() results and prints
 * them, one per line.
 */
static void
printCurrentState(void *ctx, PGresult *result)
{
	MonitorAssignedStateParseContext *context =
		(MonitorAssignedStateParseContext *) ctx;
	int currentTupleIndex = 0;
	int nTuples = PQntuples(result);
	int maxNodeNameSize = 5;	/* strlen("Name") + 1, the header */
	char *nameSeparatorHeader = NULL;

	if (PQnfields(result) != 8)
	{
		log_error("Query returned %d columns, expected 8", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	/*
	 * Dynamically adjust our display output to the length of the longer
	 * nodename in the result set
	 */
	for(currentTupleIndex = 0; currentTupleIndex < nTuples; currentTupleIndex++)
	{
		char *nodename = PQgetvalue(result, currentTupleIndex, 0);

		if (strlen(nodename) > maxNodeNameSize)
		{
			maxNodeNameSize = strlen(nodename);
		}
	}

	/* prepare a nice dynamic string of '-' as a header separator */
	nameSeparatorHeader = (char *) malloc((maxNodeNameSize+1) * sizeof(char));

	for(int i=0; i<=maxNodeNameSize; i++)
	{
		if (i<maxNodeNameSize)
		{
			nameSeparatorHeader[i] = '-';
		}
		else
		{
			nameSeparatorHeader[i] = '\0';
		}
	}

	fprintf(stdout, "%*s | %6s | %5s | %5s | %17s | %17s | %8s | %6s\n",
			maxNodeNameSize, "Name", "Port",
			"Group", "Node", "Current State", "Assigned State",
			"Priority", "Quorum");

	fprintf(stdout, "%*s-+-%6s-+-%5s-+-%5s-+-%17s-+-%17s-+-%8s-+-%6s\n",
			maxNodeNameSize, nameSeparatorHeader, "------",
			"-----", "-----", "-----------------", "-----------------",
			"--------", "------");

	free(nameSeparatorHeader);

	for(currentTupleIndex = 0; currentTupleIndex < nTuples; currentTupleIndex++)
	{
		char *nodename = PQgetvalue(result, currentTupleIndex, 0);
		char *nodeport = PQgetvalue(result, currentTupleIndex, 1);
		char *groupId = PQgetvalue(result, currentTupleIndex, 2);
		char *nodeId = PQgetvalue(result, currentTupleIndex, 3);
		char *currentState = PQgetvalue(result, currentTupleIndex, 4);
		char *goalState = PQgetvalue(result, currentTupleIndex, 5);
		char *candidatePriority = PQgetvalue(result, currentTupleIndex, 6);
		char *replicationQuorum = PQgetvalue(result, currentTupleIndex, 7);

		fprintf(stdout, "%*s | %6s | %5s | %5s | %17s | %17s | %8s | %6s\n",
				maxNodeNameSize, nodename, nodeport,
				groupId, nodeId, currentState, goalState,
				candidatePriority, replicationQuorum);
	}
	fprintf(stdout, "\n");

	context->parsedOK = true;

	return;
}


/*
 * monitor_get_state_as_json returns a single string that contains the JSON
 * representation of the current state on the monitor.
 */
bool
monitor_get_state_as_json(Monitor *monitor, char *formation, int group,
						  char *json, int size)
{
	SingleValueResultContext context = { 0 };
	PGSQL *pgsql = &monitor->pgsql;
	char *sql = NULL;
	int paramCount = 0;
	Oid paramTypes[2];
	const char *paramValues[2];
	IntString groupStr;

	log_trace("monitor_get_state_as_json(%s, %d)", formation, group);

	context.resultType = PGSQL_RESULT_STRING;
	context.parsedOk = false;

	switch (group)
	{
		case -1:
		{
			sql = "SELECT jsonb_pretty(jsonb_agg(row_to_json(state)))"
				" FROM pgautofailover.current_state($1) as state";

			paramCount = 1;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;

			break;
		}

		default:
		{
			sql = "SELECT jsonb_pretty(jsonb_agg(row_to_json(state)))"
				"FROM pgautofailover.current_state($1,$2) as state";

			groupStr = intToString(group);

			paramCount = 2;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;
			paramTypes[1] = INT4OID;
			paramValues[1] = groupStr.strValue;

			break;
		}
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to retrieve current state from the monitor");
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOk)
	{
		log_error("Failed to parse current state from the monitor");
		log_error("%s", context.strVal);
		return false;
	}

	strlcpy(json, context.strVal, size);

	return true;
}


/*
 * monitor_print_last_events calls the function pgautofailover.last_events on
 * the monitor, and prints a line of output per event obtained.
 */
bool
monitor_print_last_events(Monitor *monitor, char *formation, int group, int count)
{
	MonitorAssignedStateParseContext context = { 0 };
	PGSQL *pgsql = &monitor->pgsql;
	char *sql = NULL;
	int paramCount = 0;
	Oid paramTypes[3];
	const char *paramValues[3];
	IntString countStr;
	IntString groupStr;

	log_trace("monitor_print_last_events(%s, %d, %d)", formation, group, count);

	switch (group)
	{
		case -1:
		{
			sql = "SELECT * FROM pgautofailover.last_events($1, count => $2)";

			countStr = intToString(count);

			paramCount = 2;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;
			paramTypes[1] = INT4OID;
			paramValues[1] = countStr.strValue;

			break;
		}

		default:
		{
			sql = "SELECT * FROM pgautofailover.last_events($1,$2,$3)";

			countStr = intToString(count);
			groupStr = intToString(group);

			paramCount = 3;
			paramTypes[0] = TEXTOID;
			paramValues[0] = formation;
			paramTypes[1] = INT4OID;
			paramValues[1] = groupStr.strValue;
			paramTypes[2] = INT4OID;
			paramValues[2] = countStr.strValue;

			break;
		}
	}

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &printLastEvents))
	{
		log_error("Failed to retrieve current state from the monitor");
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOK)
	{
		return false;
	}

	return true;
}


/*
 * printLastEcvents loops over pgautofailover.last_events() results and prints
 * them, one per line.
 */
static void
printLastEvents(void *ctx, PGresult *result)
{
	MonitorAssignedStateParseContext *context =
		(MonitorAssignedStateParseContext *) ctx;
	int currentTupleIndex = 0;
	int nTuples = PQntuples(result);

	log_trace("printLastEvents: %d tuples", nTuples);

	if (PQnfields(result) != 14)
	{
		log_error("Query returned %d columns, expected 14", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	fprintf(stdout, "%30s | %10s | %6s | %18s | %18s | %s\n",
			"Event Time", "Formation", "Node",
			"Current State", "Assigned State", "Comment");
	fprintf(stdout, "%30s-+-%10s-+-%6s-+-%18s-+-%18s-+-%10s\n",
			"------------------------------", "----------",
			"------", "------------------", "------------------", "----------");

	for(currentTupleIndex = 0; currentTupleIndex < nTuples; currentTupleIndex++)
	{
		char *eventTime = PQgetvalue(result, currentTupleIndex, 1);
		char *formation = PQgetvalue(result, currentTupleIndex, 2);
		char *groupId = PQgetvalue(result, currentTupleIndex, 4);
		char *nodeId = PQgetvalue(result, currentTupleIndex, 3);
		char *currentState = PQgetvalue(result, currentTupleIndex, 7);
		char *goalState = PQgetvalue(result, currentTupleIndex, 8);
		char *description = PQgetvalue(result, currentTupleIndex, 13);
		char node[BUFSIZE];

		/* for our grid alignment output it's best to have a single col here */
		sprintf(node, "%s/%s", groupId, nodeId);

		fprintf(stdout, "%30s | %10s | %6s | %18s | %18s | %s\n",
				eventTime, formation, node, currentState, goalState, description);
	}
	fprintf(stdout, "\n");

	context->parsedOK = true;

	return;
}


/*
 * monitor_create_formation calls the SQL API on the monitor to create a new
 * formation of the given kind.
 */
bool
monitor_create_formation(Monitor *monitor,
						 char *formation, char *kind, char *dbname,
						 bool hasSecondary, int numberSyncStandbys)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT * FROM pgautofailover.create_formation($1, $2, $3, $4, $5)";
	int paramCount = 5;
	Oid paramTypes[5] = { TEXTOID, TEXTOID, TEXTOID, BOOLOID, INT4OID };
	const char *paramValues[5];

	paramValues[0] = formation;
	paramValues[1] = kind;
	paramValues[2] = dbname;
	paramValues[3] = hasSecondary ? "true" : "false";
	paramValues[4] = intToString(numberSyncStandbys).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to create formation \"%s\" of kind \"%s\", "
				  "see previous lines for details.",
				  formation, kind);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);


	return true;
}


/*
 * monitor_enable_secondary_for_formation enables secondaries for the given
 * formation
 */
bool
monitor_enable_secondary_for_formation(Monitor *monitor, const char *formation)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.enable_secondary($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to enable secondaries on formation \"%s\", "
				  "see previous lines for details.",
				  formation);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * monitor_disable_secondary_for_formation disables secondaries for the given
 * formation. This requires no secondaries to be currently in the formation,
 * function will report an error on the monitor due to an execution error of
 * pgautofailover.disable_secondary when there are still secondaries in the
 * cluster, or more precise nodes that are not in 'sinlge' state.
 */
bool
monitor_disable_secondary_for_formation(Monitor *monitor, const char *formation)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.disable_secondary($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to disable secondaries on formation \"%s\", "
				  "see previous lines for details.",
				  formation);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * monitor_drop_formation calls the SQL API on the monitor to drop formation.
 */
bool
monitor_drop_formation(Monitor *monitor, char *formation)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.drop_formation($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   NULL, NULL))
	{
		log_error("Failed to drop formation \"%s\", "
				  "see previous lines for details.",
				  formation);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * monitor_formation_uri calls the SQL API on the monitor that returns the
 * connection string that can be used by applications to connect to the
 * formation.
 */
bool
monitor_formation_uri(Monitor *monitor, const char *formation,
					  char *connectionString, size_t size)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_STRING, false };
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT formation_uri FROM pgautofailover.formation_uri($1)";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	paramValues[0] = formation;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to list the formation uri for \"%s\", "
				  "see previous lines for details.",
				  formation);
		return false;
	}

	if (!context.parsedOk)
	{
		/* errors have already been logged */
		return false;
	}

	if (context.strVal == NULL || strcmp(context.strVal, "") == 0)
	{
		log_error("Formation \"%s\" currently has no nodes in group 0",
				  formation);
		return false;
	}

	strlcpy(connectionString, context.strVal, size);

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * monitor_synchronous_standby_names returns the value for the Postgres
 * parameter "synchronous_standby_names" to use for a given group. The setting
 * is computed on the monitor depending on the current values of the formation
 * number_sync_standbys and each node's candidate priority and replication
 * quorum properties.
 */
bool
monitor_synchronous_standby_names(Monitor *monitor,
								  char *formation, int groupId,
								  char *synchronous_standby_names, int size)
{
	PGSQL *pgsql = &monitor->pgsql;
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_STRING, false };

	const char *sql =
		"select pgautofailover.synchronous_standby_names($1, $2)";

	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2] = { 0 };
	IntString myGroupIdString = intToString(groupId);

	paramValues[0] = formation;
	paramValues[1] = myGroupIdString.strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to get the synchronous_standby_names setting value "
				  " from the monitor for formation %s and group %d",
				  formation, groupId);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!context.parsedOk)
	{
		log_error("Failed to get the synchronous_standby_names setting value "
				  " from the monitor for formation %s and group %d,"
				  "see above for details",
				  formation, groupId);
		return false;
	}

	strlcpy(synchronous_standby_names, context.strVal, size);

	return true;
}


/*
 * monitor_set_nodename sets the nodename on the monitor, using a simple SQL
 * update command.
 */
bool
monitor_set_nodename(Monitor *monitor, int nodeId, const char *nodename)
{
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT * FROM pgautofailover.set_node_nodename($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { INT8OID, TEXTOID };
	const char *paramValues[2];

	NodeAddress node = { 0 };
	NodeAddressParseContext parseContext = { { 0 }, &node, false };

	paramValues[0] = intToString(nodeId).strValue;
	paramValues[1] = nodename;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &parseContext, parseNodeResult))
	{
		log_error("Failed to set_node_nodename of node %d from the monitor",
				  nodeId);
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	if (!parseContext.parsedOK)
	{
		log_error(
			"Failed to set node %d nodename to \"%s\" on the monitor "
			"because it returned an unexpected result. "
			"See previous line for details.",
			nodeId, nodename);
		return false;
	}

	return true;
}


/*
 * parseCoordinatorNode parses a hostname and a port from the libpq result and
 * writes it to the NodeAddressParseContext pointed to by ctx. This is about
 * the same as parseNode: the only difference is that an empty result set is
 * not an error condition in parseCoordinatorNode.
 */
static void
parseCoordinatorNode(void *ctx, PGresult *result)
{
	NodeAddressParseContext *context = (NodeAddressParseContext *) ctx;
	char *value = NULL;
	int hostLength = 0;

	/* no rows, set the node to NULL, return */
	if (PQntuples(result) == 0)
	{
		context->node = NULL;
		context->parsedOK = true;
		return;
	}

	/* we have rows: we accept only one */
	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 2)
	{
		log_error("Query returned %d columns, expected 2", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	if (PQgetisnull(result, 0, 0) || PQgetisnull(result, 0, 1))
	{
		log_error("Hostname or port returned by monitor is NULL");
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 0);
	hostLength = strlcpy(context->node->host, value, _POSIX_HOST_NAME_MAX);
	if (hostLength >= _POSIX_HOST_NAME_MAX)
	{
		log_error("Hostname \"%s\" returned by monitor is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, hostLength, _POSIX_HOST_NAME_MAX - 1);
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 1);
	context->node->port = strtol(value, NULL, 0);
	if (context->node->port == 0)
	{
		log_error("Invalid port number \"%s\" returned by monitor", value);
		context->parsedOK = false;
	}

	context->parsedOK = true;
}


/*
 * monitor_start_maintenance calls the pgautofailover.start_maintenance(node,
 * port) on the monitor, so that the monitor assigns the MAINTENANCE_STATE at
 * the next call to node_active().
 */
bool
monitor_start_maintenance(Monitor *monitor, char *host, int port)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT pgautofailover.start_maintenance($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = host;
	paramValues[1] = intToString(port).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to start_maintenance of node %s:%d from the monitor",
				  host, port);
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to start_maintenance of node %s:%d from the monitor: "
				  "could not parse monitor's result.", host, port);
		return false;
	}

	return context.boolVal;
}


/*
 * monitor_stop_maintenance calls the pgautofailover.start_maintenance(node,
 * port) on the monitor, so that the monitor assigns the CATCHINGUP_STATE at
 * the next call to node_active().
 */
bool
monitor_stop_maintenance(Monitor *monitor, char *host, int port)
{
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql = "SELECT pgautofailover.stop_maintenance($1, $2)";
	int paramCount = 2;
	Oid paramTypes[2] = { TEXTOID, INT4OID };
	const char *paramValues[2];

	paramValues[0] = host;
	paramValues[1] = intToString(port).strValue;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseSingleValueResult))
	{
		log_error("Failed to stop_maintenance of node %s:%d from the monitor",
				  host, port);
		return false;
	}

	if (!context.parsedOk)
	{
		log_error("Failed to stop_maintenance of node %s:%d from the monitor: "
				  "could not parse monitor's result.", host, port);
		return false;
	}

	return context.boolVal;
}


/*
 * monitor_get_notifications listens to notifications from the monitor.
 *
 * Use the select(2) facility to check if something is ready to be read on the
 * PQconn socket for us. When it's the case, return the next notification
 * message from the "state" channel. Other channel messages are sent to the log
 * directly.
 *
 * When the function returns true, it's safe for the caller to sleep, otherwise
 * it's expected that the caller keeps polling the results to drain the queue
 * of notifications received from the previous calls loop.
 */
bool
monitor_get_notifications(Monitor *monitor)
{
	PGconn *connection = monitor->pgsql.connection;
    PGnotify   *notify;
	int         sock;
	fd_set      input_mask;

	if (connection == NULL)
	{
 		log_warn("Lost connection.");
		return false;
	}

	sock = PQsocket(connection);

	if (sock < 0)
		return false;	/* shouldn't happen */

	FD_ZERO(&input_mask);
	FD_SET(sock, &input_mask);

	if (select(sock + 1, &input_mask, NULL, NULL, NULL) < 0)
	{
		log_warn("select() failed: %s\n", strerror(errno));
		return false;
	}

	/* Now check for input */
	PQconsumeInput(connection);
	while ((notify = PQnotifies(connection)) != NULL)
	{
		if (strcmp(notify->relname, "log") == 0)
		{
			log_info("%s", notify->extra);
		}
		else if (strcmp(notify->relname, "state") == 0)
		{
			StateNotification notification = { 0 };

			log_debug("received \"%s\"", notify->extra);

			/* the parsing scribbles on the message, make a copy now */
			strlcpy(notification.message, notify->extra, BUFSIZE);

			/* errors are logged by parse_state_notification_message */
			if (parse_state_notification_message(&notification))
			{
				log_info("New state for %s:%d in formation \"%s\": %s/%s",
						 notification.nodeName,
						 notification.nodePort,
						 notification.formationId,
						 NodeStateToString(notification.reportedState),
						 NodeStateToString(notification.goalState));
			}
		}
		else
		{
			log_warn("BUG: received unknown notification on channel \"%s\": %s",
					 notify->relname, notify->extra);
		}

		PQfreemem(notify);
		PQconsumeInput(connection);
	}

	return true;
}


/*
 * monitor_wait_until_primary_applied_settings receives notifications and
 * watches for the following "apply_settings" set of transitions:
 *
 *  - primary/apply_settings
 *  - apply_settings/apply_settings
 *  - apply_settings/primary
 *  - primary/primary
 *
 * If we lose the monitor connection while watching for the transition steps
 * then we stop watching. It's a best effort attempt at having the CLI be
 * useful for its user, the main one being the test suite.
 */
bool
monitor_wait_until_primary_applied_settings(Monitor *monitor,
											const char *formation)
{
	PGconn *connection = monitor->pgsql.connection;
	bool		applySettingsTransitionInProgress = false;
	bool		applySettingsTransitionDone = false;

	if (connection == NULL)
	{
 		log_warn("Lost connection.");
		return false;
	}

	log_info("Waiting for the settings to have been applied to "
			 "the monitor and primary node");

	while (!applySettingsTransitionDone)
	{
		/* Sleep until something happens on the connection. */
		int         sock;
		fd_set      input_mask;
		PGnotify   *notify;

		sock = PQsocket(connection);

		if (sock < 0)
			return false;	/* shouldn't happen */

		FD_ZERO(&input_mask);
		FD_SET(sock, &input_mask);

		if (select(sock + 1, &input_mask, NULL, NULL, NULL) < 0)
		{
			log_warn("select() failed: %s\n", strerror(errno));
			return false;
		}

		/* Now check for input */
		PQconsumeInput(connection);
		while ((notify = PQnotifies(connection)) != NULL)
		{
			StateNotification notification = { 0 };

			if (strcmp(notify->relname, "state") != 0)
			{
				log_warn("%s: %s", notify->relname, notify->extra);
				continue;
			}

			log_debug("received \"%s\"", notify->extra);

			/* the parsing scribbles on the message, make a copy now */
			strlcpy(notification.message, notify->extra, BUFSIZE);

			/* errors are logged by parse_state_notification_message */
			if (!parse_state_notification_message(&notification))
			{
				log_warn("Failed to parse notification message \"%s\"",
						 notify->extra);
				continue;
			}

			/* filter notifications for our own formation */
			if (strcmp(notification.formationId, formation) != 0)
			{
				continue;
			}

			if (notification.reportedState == PRIMARY_STATE
				&& notification.goalState == APPLY_SETTINGS_STATE)
			{
				applySettingsTransitionInProgress = true;

				log_debug("step 1/4: primary node %d (%s:%d) is assigned \"%s\"",
						  notification.nodeId,
						  notification.nodeName,
						  notification.nodePort,
						  NodeStateToString(notification.goalState));
			}
			else if (notification.reportedState == APPLY_SETTINGS_STATE
					 && notification.goalState == APPLY_SETTINGS_STATE)
			{
				applySettingsTransitionInProgress = true;

				log_debug("step 2/4: primary node %d (%s:%d) reported \"%s\"",
						  notification.nodeId,
						  notification.nodeName,
						  notification.nodePort,
						  NodeStateToString(notification.reportedState));
			}
			else if (notification.reportedState == APPLY_SETTINGS_STATE
					 && notification.goalState == PRIMARY_STATE)
			{
				applySettingsTransitionInProgress = true;

				log_debug("step 3/4: primary node %d (%s:%d) is assigned \"%s\"",
						  notification.nodeId,
						  notification.nodeName,
						  notification.nodePort,
						  NodeStateToString(notification.goalState));
			}
			else if (applySettingsTransitionInProgress
					 && notification.reportedState == PRIMARY_STATE
					 && notification.goalState == PRIMARY_STATE)
			{
				applySettingsTransitionDone = true;

				log_debug("step 4/4: primary node %d (%s:%d) reported \"%s\"",
						  notification.nodeId,
						  notification.nodeName,
						  notification.nodePort,
						  NodeStateToString(notification.reportedState));
			}

			/* prepare next iteration */
			PQfreemem(notify);
			PQconsumeInput(connection);
		}
	}

	/* disconnect from monitor */
	pgsql_finish(&monitor->pgsql);

	return applySettingsTransitionDone;
}


/*
 * monitor_wait_until_node_reported_maintenance receives notifications and
 * watches for the given node to have goalState and reportedState set to given
 * state.
 *
 * If we lose the monitor connection while watching for the transition steps
 * then we stop watching. It's a best effort attempt at having the CLI be
 * useful for its user, the main one being the test suite.
 */
bool
monitor_wait_until_node_reported_state(Monitor *monitor,
									   int nodeId,
									   NodeState state)
{
	PGconn *connection = monitor->pgsql.connection;
	bool reachedMaintenance = false;

	if (connection == NULL)
	{
 		log_warn("Lost connection.");
		return false;
	}

	while (!reachedMaintenance)
	{
		/* Sleep until something happens on the connection. */
		int         sock;
		fd_set      input_mask;
		PGnotify   *notify;

		sock = PQsocket(connection);

		if (sock < 0)
			return false;	/* shouldn't happen */

		FD_ZERO(&input_mask);
		FD_SET(sock, &input_mask);

		if (select(sock + 1, &input_mask, NULL, NULL, NULL) < 0)
		{
			log_warn("select() failed: %s\n", strerror(errno));
			return false;
		}

		/* Now check for input */
		PQconsumeInput(connection);
		while ((notify = PQnotifies(connection)) != NULL)
		{
			StateNotification notification = { 0 };

			if (strcmp(notify->relname, "state") != 0)
			{
				log_warn("%s: %s", notify->relname, notify->extra);
				continue;
			}

			log_debug("received \"%s\"", notify->extra);

			/* the parsing scribbles on the message, make a copy now */
			strlcpy(notification.message, notify->extra, BUFSIZE);

			/* errors are logged by parse_state_notification_message */
			if (!parse_state_notification_message(&notification))
			{
				log_warn("Failed to parse notification message \"%s\"",
						 notify->extra);
				continue;
			}

			/* filter notifications for our own formation */
			if (notification.nodeId != nodeId)
			{
				continue;
			}

			if (notification.reportedState == state
				&& notification.goalState == state)
			{
				reachedMaintenance = true;

				log_debug("node %d (%s:%d) is now in state \"%s\"",
						  notification.nodeId,
						  notification.nodeName,
						  notification.nodePort,
						  NodeStateToString(state));
			}

			/* prepare next iteration */
			PQfreemem(notify);
			PQconsumeInput(connection);
		}
	}

	/* disconnect from monitor */
	pgsql_finish(&monitor->pgsql);

	return reachedMaintenance;
}


/*
 * monitor_get_extension_version gets the current extension version from the
 * Monitor's Postgres catalog pg_available_extensions.
 */
bool
monitor_get_extension_version(Monitor *monitor, MonitorExtensionVersion *version)
{
	MonitorExtensionVersionParseContext context = { { 0 }, version, false };
	PGSQL *pgsql = &monitor->pgsql;
	const char *sql =
		"SELECT default_version, installed_version"
		"  FROM pg_available_extensions WHERE name = $1";
	int paramCount = 1;
	Oid paramTypes[1] = { TEXTOID };
	const char *paramValues[1];

	paramValues[0] = PG_AUTOCTL_MONITOR_EXTENSION_NAME;

	if (!pgsql_execute_with_params(pgsql, sql,
								   paramCount, paramTypes, paramValues,
								   &context, &parseExtensionVersion))
	{
		log_error("Failed to get the current version for extension \"%s\", "
				  "see previous lines for details.",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		return false;
	}

	if (!context.parsedOK)
	{
		/* errors have already been logged */
		return false;
	}

	/* disconnect from PostgreSQL now */
	pgsql_finish(&monitor->pgsql);

	return true;
}


/*
 * parseExtensionVersion parses the resultset of a query on the Postgres
 * pg_available_extension_versions catalogs.
 */
static void
parseExtensionVersion(void *ctx, PGresult *result)
{
	MonitorExtensionVersionParseContext *context =
		(MonitorExtensionVersionParseContext *) ctx;

	char *value = NULL;
	int length = -1;

	/* we have rows: we accept only one */
	if (PQntuples(result) != 1)
	{
		log_error("Query returned %d rows, expected 1", PQntuples(result));
		context->parsedOK = false;
		return;
	}

	if (PQnfields(result) != 2)
	{
		log_error("Query returned %d columns, expected 2", PQnfields(result));
		context->parsedOK = false;
		return;
	}

	if (PQgetisnull(result, 0, 0) || PQgetisnull(result, 0, 1))
	{
		log_error("default_version or installed_version for extension \"%s\" "
				  "is NULL ", PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 0);
 	length = strlcpy(context->version->defaultVersion, value, BUFSIZE);
	if (length >= BUFSIZE)
	{
		log_error("default_version \"%s\" returned by monitor is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, length, BUFSIZE - 1);
		context->parsedOK = false;
		return;
	}

	value = PQgetvalue(result, 0, 1);
 	length = strlcpy(context->version->installedVersion, value, BUFSIZE);
	if (length >= BUFSIZE)
	{
		log_error("installed_version \"%s\" returned by monitor is %d characters, "
				  "the maximum supported by pg_autoctl is %d",
				  value, length, BUFSIZE - 1);
		context->parsedOK = false;
		return;
	}

	context->parsedOK = true;
}

/*
 * monitor_extension_update executes ALTER EXTENSION ... UPDATE TO ...
 */
bool
monitor_extension_update(Monitor *monitor, const char *targetVersion)
{
	PGSQL *pgsql = &monitor->pgsql;

	return pgsql_alter_extension_update_to(pgsql,
										   PG_AUTOCTL_MONITOR_EXTENSION_NAME,
										   targetVersion);
}


/*
 * monitor_ensure_extension_version checks that we are running a extension
 * version on the monitor that we are compatible with in pg_autoctl. If that's
 * not the case, we blindly try to update the extension version on the monitor
 * to the target version we have in our default.h.
 *
 * NOTE: we don't check here if the update is an upgrade or a downgrade, we
 * rely on the extension's update path to be free of downgrade paths (such as
 * pgautofailover--1.2--1.1.sql).
 */
bool
monitor_ensure_extension_version(Monitor *monitor,
								 MonitorExtensionVersion *version)
{
	const char *extensionVersion = PG_AUTOCTL_EXTENSION_VERSION;

	/* in test environement, we can export any target version we want */
	if (getenv(PG_AUTOCTL_DEBUG) != NULL)
	{
		char *val = getenv(PG_AUTOCTL_EXTENSION_VERSION_VAR);

		if (val != NULL)
		{
			extensionVersion = val;
			log_debug("monitor_ensure_extension_version targets extension "
					  "version \"%s\" - as per environment.",
					  extensionVersion);
		}
	}

	if (!monitor_get_extension_version(monitor, version))
	{
		log_fatal("Failed to check version compatibility with the monitor "
				  "extension \"%s\", see above for details",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		return false;
	}

	if (strcmp(version->installedVersion, extensionVersion) != 0)
	{
		Monitor dbOwnerMonitor = { 0 };

		log_warn("This version of pg_autoctl requires the extension \"%s\" "
				 "version \"%s\" to be installed on the monitor, current "
				 "version is \"%s\".",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME,
				 extensionVersion,
				 version->installedVersion);

		/*
		 * Ok, let's try to update the extension then.
		 *
		 * For that we need to connect as the owner of the database, which was
		 * the current $USER at the time of the `pg_autoctl create monitor`
		 * command.
		 */
		if (!prepare_connection_to_current_system_user(monitor,
													   &dbOwnerMonitor))
		{
			log_error("Failed to update extension \"%s\" to version \"%s\": "
					  "failed prepare a connection string to the "
					  "monitor as the database owner",
					  PG_AUTOCTL_MONITOR_EXTENSION_NAME,
					  extensionVersion);
			return false;
		}

		if (!monitor_extension_update(&dbOwnerMonitor,
									  extensionVersion))
		{
			log_fatal("Failed to update extension \"%s\" to version \"%s\" "
					  "on the monitor, see above for details",
					  PG_AUTOCTL_MONITOR_EXTENSION_NAME,
					  extensionVersion);
			return false;
		}

		if (!monitor_get_extension_version(monitor, version))
		{
			log_fatal("Failed to check version compatibility with the monitor "
					  "extension \"%s\", see above for details",
					  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
			return false;
		}

		log_info("Updated extension \"%s\" to version \"%s\"",
				 PG_AUTOCTL_MONITOR_EXTENSION_NAME,
				 version->installedVersion);

		return true;
	}

	/* just mention we checked, and it's ok */
	log_info("The version of extenstion \"%s\" is \"%s\" on the monitor",
			 PG_AUTOCTL_MONITOR_EXTENSION_NAME, version->installedVersion);

	return true;
}


/*
 * prepare_connection_to_current_system_user changes a given pguri to remove
 * its "user" connection parameter, filling in the pre-allocated keywords and
 * values string arrays.
 *
 * Postgres docs at the following address show 30 connection parameters, so the
 * arrays should allocate 31 entries at least. The last one is going to be
 * NULL.
 *
 *  https://www.postgresql.org/docs/current/libpq-connect.html
 */
static bool
prepare_connection_to_current_system_user(Monitor *source, Monitor *target)
{
	const char *keywords[41] = { 0 };
	const char *values[41] = { 0 };

	char *errmsg;
	PQconninfoOption *conninfo, *option;
	int argCount = 0;

	conninfo = PQconninfoParse(source->pgsql.connectionString, &errmsg);
	if (conninfo == NULL)
	{
		log_error("Failed to parse pguri \"%s\": %s",
				  source->pgsql.connectionString, errmsg);
		PQfreemem(errmsg);
		return false;
	}

	for (option = conninfo; option->keyword != NULL; option++)
	{
		if (strcmp(option->keyword, "user") == 0)
		{
			/* skip the user, $USER is what we want to use here */
			continue;
		}
		else if (option->val)
		{
			if (argCount == 40)
			{
				log_error("Failed to parse Postgres URI options: "
						  "pg_autoctl supports up to 40 options "
						  "and we are parsing more than that.");
				return false;
			}
			keywords[argCount] = option->keyword;
			values[argCount] = option->val;
			++argCount;
		}
	}
	keywords[argCount] = NULL;
	values[argCount] = NULL;

	/* open the connection now, and check that everything is ok */
	target->pgsql.connection = PQconnectdbParams(keywords, values, 0);

	/* Check to see that the backend connection was successfully made */
	if (PQstatus(target->pgsql.connection) != CONNECTION_OK)
	{
		log_error("Connection to database failed: %s",
				  PQerrorMessage(target->pgsql.connection));
		pgsql_finish(&(target->pgsql));
		PQconninfoFree(conninfo);
		return false;
	}

	PQconninfoFree(conninfo);

	return true;
}


/*
 * ensure_monitor_pg_running checks if monitor is running, attempts to restart if it is not.
 * The function verifies if the extension version is the same as expected.  It returns true
 * if the monitor is up and running.
 */
bool
ensure_monitor_pg_running(Monitor *monitor, struct MonitorConfig *mconfig)
{
	MonitorExtensionVersion version = { 0 };
	char postgresUri[MAXCONNINFO];

	if (!pg_is_running(mconfig->pgSetup.pg_ctl, mconfig->pgSetup.pgdata))
	{
		log_info("Postgres is not running, starting postgres");
		pgsql_finish(&(monitor->pgsql));

		if (!pg_ctl_start(mconfig->pgSetup.pg_ctl,
						  mconfig->pgSetup.pgdata,
						  mconfig->pgSetup.pgport,
						  mconfig->pgSetup.listen_addresses))
		{
			log_error("Failed to start PostgreSQL, see above for details");
			return false;
		}

		/*
		 * Check version compatibility.
		 *
		 * The function terminates any existing connection during clenaup
		 * therefore it is not called when PG is found to be running to not
		 * to intervene pgsql_listen call.
		 */
		if (!monitor_ensure_extension_version(monitor, &version))
		{
			/* errors have already been logged */
			return false;
		}

	}

	return true;
}


/*
 * monitor_service_run watches over monitor process, restarts if it is necessary,
 * also loops over a LISTEN command that is notified at every
 * change of state on the monitor, and prints the change on stdout.
 */
bool
monitor_service_run(Monitor *monitor,  struct MonitorConfig *mconfig)
{
	char *channels[] = { "log", "state", NULL };
	char postgresUri[MAXCONNINFO];

	/* We exit the loop if we can't get monitor to be running during the start */
	if (!ensure_monitor_pg_running(monitor, mconfig))
	{
		/* errors were already logged */
		log_warn("Failed to ensure PostgreSQL is running, exiting the service");
		return false;
	}

	/* Now get the the Monitor URI to display it to the user, and move along */
	if (monitor_config_get_postgres_uri(mconfig, postgresUri, MAXCONNINFO))
	{
		log_info("pg_auto_failover monitor is ready at %s", postgresUri);
	}

	log_info("Contacting the monitor to LISTEN to its events.");
	pgsql_listen(&(monitor->pgsql), channels);

	/*
	 * Main loop for notifications.
	 */
	for (;;)
	{
		if (!ensure_monitor_pg_running(monitor, mconfig))
		{
			log_warn("Failed to ensure PostgreSQL is running, "
					 "retrying in %d seconds",
					 PG_AUTOCTL_MONITOR_SLEEP_TIME);
			sleep(PG_AUTOCTL_MONITOR_SLEEP_TIME);
			continue;
		}

		if (!monitor_get_notifications(monitor))
		{
			log_warn("Re-establishing connection. We might miss notifications.");
			pgsql_finish(&(monitor->pgsql));

			pgsql_listen(&(monitor->pgsql), channels);

			/* skip sleeping */
			continue;
		}

		sleep(PG_AUTOCTL_MONITOR_SLEEP_TIME);
	}
	pgsql_finish(&(monitor->pgsql));
}
