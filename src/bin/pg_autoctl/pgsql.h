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

#include "libpq-fe.h"


/*
 * OID values from PostgreSQL src/include/catalog/pg_type.h
 */
#define BOOLOID 16
#define INT4OID 23
#define INT8OID 20
#define TEXTOID 25

/*
 * Maximum connection info length as used in walreceiver.h
 */
#define MAXCONNINFO 1024

/*
 * pg_stat_replication.sync_state is one if:
 *   sync, async, quorum, potential
 */
#define PGSR_SYNC_STATE_MAXLENGTH 10


/* abstract representation of a Postgres server that we can connect to */
typedef struct PGSQL
{
	char	connectionString[MAXCONNINFO];
	PGconn *connection;
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
	char host[_POSIX_HOST_NAME_MAX];
	int port;
} NodeAddress;

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

/* data structure for keeping a single-value query result */
typedef struct SingleValueResultContext
{
	QueryResultType resultType;
	bool parsedOk;
	bool boolVal;
	int intVal;
	uint64_t bigint;
	char *strVal;
} SingleValueResultContext;


#define CHECK__SETTINGS_SQL											\
	"select bool_and(ok) "											\
	"from ("														\
	"select current_setting('max_wal_senders')::int >= 4"			\
	" union all "													\
	"select current_setting('max_replication_slots')::int >= 4"		\
	" union all "													\
	"select current_setting('wal_level') in ('replica', 'logical')"	\
	" union all "													\
	"select current_setting('wal_log_hints') = 'on'"

#define CHECK_POSTGRESQL_NODE_SETTINGS_SQL \
	CHECK__SETTINGS_SQL					   \
	") as t(ok) "

#define CHECK_CITUS_NODE_SETTINGS_SQL						\
	CHECK__SETTINGS_SQL										\
	" union all "											\
	"select lib = 'citus' "									\
	"from unnest(string_to_array("							\
	"current_setting('shared_preload_libraries'), ',') "	\
	" || array['not citus']) "								\
	"with ordinality ast(lib, n) where n = 1"				\
	") as t(ok) "

bool pgsql_init(PGSQL *pgsql, char *url);
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
bool pgsql_drop_replication_slot(PGSQL *pgsql, const char *slotName, bool verbose);
bool pgsql_enable_synchronous_replication(PGSQL *pgsql);
bool pgsql_disable_synchronous_replication(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_only(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_write(PGSQL *pgsql);
bool pgsql_checkpoint(PGSQL *pgsql);
bool pgsql_get_config_file_path(PGSQL *pgsql, char *configFilePath, int maxPathLength);
bool pgsql_get_hba_file_path(PGSQL *pgsql, char *hbaFilePath, int maxPathLength);
bool pgsql_create_database(PGSQL *pgsql, const char *dbname, const char *owner);
bool pgsql_create_extension(PGSQL *pgsql, const char *name);
bool pgsql_create_user(PGSQL *pgsql, const char *userName, const char *password,
					   bool login, bool superuser, bool replication);
bool pgsql_has_replica(PGSQL *pgsql, char *userName, bool *hasReplica);
bool hostname_from_uri(const char *pguri, char *hostname, int maxHostLength);
int make_conninfo_field_str(char *destination, const char *key, const char *value);
int make_conninfo_field_int(char *destination, const char *key, int value);
bool validate_connection_string(const char *connectionString);

bool pgsql_get_sync_state_and_wal_lag(PGSQL *pgsql, const char *slotName,
									  char *pgsrSyncState, int64_t *walLag,
									  bool missing_ok);
bool pgsql_get_wal_lag_from_standby(PGSQL *pgsql, int64_t *walLag);

bool pgsql_listen(PGSQL *pgsql, char *channels[]);

#endif /* PGSQL_H */
