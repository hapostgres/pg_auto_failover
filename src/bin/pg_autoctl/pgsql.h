/*
 * src/bin/pg_autoctl/pgsql.h
 *     Functions for interacting with a postgres server
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGSQL_H
#define PGSQL_H


#include <limits.h>
#include <stdbool.h>

#include "libpq-fe.h"

#include "pgsetup.h"

/*
 * OID values from PostgreSQL src/include/catalog/pg_type.h
 */
#define BOOLOID 16
#define NAMEOID 19
#define INT4OID 23
#define INT8OID 20
#define TEXTOID 25
#define LSNOID 3220

/*
 * Maximum connection info length as used in walreceiver.h
 */
#define MAXCONNINFO 1024


/*
 * Maximum length of serialized pg_lsn value
 * It is taken from postgres file pg_lsn.c.
 * It defines MAXPG_LSNLEN to be 17 and
 * allocates a buffer 1 byte larger. We
 * went for 18 to make buffer allocation simpler.
 */
#define PG_LSN_MAXLENGTH 18

/*
 * pg_stat_replication.sync_state is one if:
 *   sync, async, quorum, potential
 */
#define PGSR_SYNC_STATE_MAXLENGTH 10

/*
 * We receive a list of "other nodes" from the monitor, and we store that list
 * in local memory. We pre-allocate the memory storage, and limit how many node
 * addresses we can handle because of the pre-allocation strategy.
 */
#define NODE_ARRAY_MAX_COUNT 12


/* abstract representation of a Postgres server that we can connect to */
typedef enum
{
	PGSQL_CONN_LOCAL = 0,
	PGSQL_CONN_MONITOR,
	PGSQL_CONN_COORDINATOR
} ConnectionType;

typedef struct PGSQL
{
	ConnectionType connectionType;
	char connectionString[MAXCONNINFO];
	PGconn *connection;
	bool connectFailFast;
} PGSQL;

/* PostgreSQL ("Grand Unified Configuration") setting */
typedef struct GUC
{
	char *name;
	char *value;
} GUC;

/* network address of a node in an HA group */
typedef struct NodeAddress
{
	int nodeId;
	char name[_POSIX_HOST_NAME_MAX];
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char lsn[PG_LSN_MAXLENGTH];
	bool isPrimary;
} NodeAddress;

typedef struct NodeAddressArray
{
	int count;
	NodeAddress nodes[NODE_ARRAY_MAX_COUNT];
} NodeAddressArray;

/*
 * The replicationSource structure is used to pass the bits of a connection
 * string to the primary node around in several function calls. All the
 * information stored in there must fit in a connection string, so MAXCONNINFO
 * is a good proxy for their maximum size.
 */
typedef struct ReplicationSource
{
	NodeAddress primaryNode;
	char userName[NAMEDATALEN];
	char slotName[MAXCONNINFO];
	char password[MAXCONNINFO];
	char maximumBackupRate[MAXCONNINFO];
	char backupDir[MAXCONNINFO];
	char applicationName[MAXCONNINFO];
	SSLOptions sslOptions;
} ReplicationSource;


/*
 * Arrange a generic way to parse PostgreSQL result from a query. Most of the
 * queries we need here return a single row of a single column, so that's what
 * the default context and parsing allows for.
 */

/* callback for parsing query results */
typedef void (ParsePostgresResultCB)(void *context, PGresult *result);

typedef enum
{
	PGSQL_RESULT_BOOL = 1,
	PGSQL_RESULT_INT,
	PGSQL_RESULT_BIGINT,
	PGSQL_RESULT_STRING
} QueryResultType;

/*
 * As a way to communicate the SQL STATE when an error occurs, every
 * pgsql_execute_with_params context structure must have the same first field,
 * an array of 5 characters (plus '\0' at the end).
 */
#define SQLSTATE_LENGTH 6

typedef struct AbstractResultContext
{
	char sqlstate[SQLSTATE_LENGTH];
} AbstractResultContext;

/* data structure for keeping a single-value query result */
typedef struct SingleValueResultContext
{
	char sqlstate[SQLSTATE_LENGTH];
	QueryResultType resultType;
	bool parsedOk;
	bool boolVal;
	int intVal;
	uint64_t bigint;
	char *strVal;
} SingleValueResultContext;


#define CHECK__SETTINGS_SQL \
	"select bool_and(ok) " \
	"from (" \
	"select current_setting('max_wal_senders')::int >= 4" \
	" union all " \
	"select current_setting('max_replication_slots')::int >= 4" \
	" union all " \
	"select current_setting('wal_level') in ('replica', 'logical')" \
	" union all " \
	"select current_setting('wal_log_hints') = 'on'"

#define CHECK_POSTGRESQL_NODE_SETTINGS_SQL \
	CHECK__SETTINGS_SQL \
	") as t(ok) "

#define CHECK_CITUS_NODE_SETTINGS_SQL \
	CHECK__SETTINGS_SQL \
	" union all " \
	"select lib = 'citus' " \
	"from unnest(string_to_array(" \
	"current_setting('shared_preload_libraries'), ',') " \
	" || array['not citus']) " \
	"with ordinality ast(lib, n) where n = 1" \
	") as t(ok) "

bool pgsql_init(PGSQL *pgsql, char *url, ConnectionType connectionType);
void pgsql_finish(PGSQL *pgsql);
void parseSingleValueResult(void *ctx, PGresult *result);
bool pgsql_execute_with_params(PGSQL *pgsql, const char *sql, int paramCount,
							   const Oid *paramTypes, const char **paramValues,
							   void *parseContext, ParsePostgresResultCB *parseFun);
bool pgsql_check_postgresql_settings(PGSQL *pgsql, bool isCitusInstanceKind,
									 bool *settings_are_ok);
bool pgsql_check_monitor_settings(PGSQL *pgsql, bool *settings_are_ok);
bool pgsql_is_in_recovery(PGSQL *pgsql, bool *is_in_recovery);
bool pgsql_reload_conf(PGSQL *pgsql);
bool pgsql_create_replication_slot(PGSQL *pgsql, const char *slotName);
bool pgsql_drop_replication_slot(PGSQL *pgsql, const char *slotName);
bool postgres_sprintf_replicationSlotName(int nodeId, char *slotName, int size);
bool pgsql_set_synchronous_standby_names(PGSQL *pgsql,
										 char *synchronous_standby_names);
bool pgsql_replication_slot_drop_removed(PGSQL *pgsql,
										 NodeAddressArray *nodeArray);
bool pgsql_replication_slot_maintain(PGSQL *pgsql, NodeAddressArray *nodeArray);
bool postgres_sprintf_replicationSlotName(int nodeId, char *slotName, int size);
bool pgsql_enable_synchronous_replication(PGSQL *pgsql);
bool pgsql_disable_synchronous_replication(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_only(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_write(PGSQL *pgsql);
bool pgsql_checkpoint(PGSQL *pgsql);
bool pgsql_get_hba_file_path(PGSQL *pgsql, char *hbaFilePath, int maxPathLength);
bool pgsql_create_database(PGSQL *pgsql, const char *dbname, const char *owner);
bool pgsql_create_extension(PGSQL *pgsql, const char *name);
bool pgsql_create_user(PGSQL *pgsql, const char *userName, const char *password,
					   bool login, bool superuser, bool replication);
bool pgsql_has_replica(PGSQL *pgsql, char *userName, bool *hasReplica);
bool hostname_from_uri(const char *pguri,
					   char *hostname, int maxHostLength, int *port);
bool validate_connection_string(const char *connectionString);
bool pgsql_reset_primary_conninfo(PGSQL *pgsql);

bool pgsql_get_postgres_metadata(PGSQL *pgsql, const char *slotName,
								 bool *pg_is_in_recovery,
								 char *pgsrSyncState, char *currentLSN);

bool pgsql_listen(PGSQL *pgsql, char *channels[]);

bool pgsql_alter_extension_update_to(PGSQL *pgsql,
									 const char *extname, const char *version);

#endif /* PGSQL_H */
