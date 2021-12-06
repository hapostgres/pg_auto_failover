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

#include "postgres.h"
#include "libpq-fe.h"
#include "portability/instr_time.h"

#if PG_MAJORVERSION_NUM >= 15
#include "common/pg_prng.h"
#endif

#include "defaults.h"
#include "pgsetup.h"
#include "state.h"


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
	PGSQL_CONN_COORDINATOR,
	PGSQL_CONN_UPSTREAM,
	PGSQL_CONN_APP
} ConnectionType;


/*
 * Retry policy to follow when we fail to connect to a Postgres URI.
 *
 * In almost all the code base the retry mechanism is implemented in the main
 * loop so we want to fail fast and let the main loop handle the connection
 * retry and the different network timeouts that we have, including the network
 * partition detection timeout.
 *
 * In the initialisation code path though, pg_autoctl might be launched from
 * provisioning script on a set of nodes in parallel, and in that case we need
 * to secure a connection and implement a retry policy at the point in the code
 * where we open a connection, so that it's transparent to the caller.
 *
 * When we do retry connecting, we implement an Exponential Backoff with
 * Decorrelated Jitter algorithm as proven useful in the following article:
 *
 *  https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/
 */
typedef struct ConnectionRetryPolicy
{
	int maxT;                   /* maximum time spent retrying (seconds) */
	int maxR;                   /* maximum number of retries, might be zero */
	int maxSleepTime;           /* in millisecond, used to cap sleepTime */
	int baseSleepTime;          /* in millisecond, base time to sleep for */
	int sleepTime;              /* in millisecond, time waited for last round */

	instr_time startTime;       /* time of the first attempt */
	instr_time connectTime;     /* time of successful connection */
	int attempts;               /* how many attempts have been made so far */

#if PG_MAJORVERSION_NUM >= 15
	pg_prng_state prng_state;
#endif
} ConnectionRetryPolicy;

/*
 * Denote if the connetion is going to be used for one, or multiple statements.
 * This is used by psql_* functions to know if a connection is to be closed
 * after successful completion, or if the the connection is to be maintained
 * open for further queries.
 *
 * A common use case for maintaining a connection open, is while wishing to open
 * and maintain a transaction block. Another, is while listening for events.
 */
typedef enum
{
	PGSQL_CONNECTION_SINGLE_STATEMENT = 0,
	PGSQL_CONNECTION_MULTI_STATEMENT
} ConnectionStatementType;

/*
 * Allow higher level code to distinguish between failure to connect to the
 * target Postgres service and failure to run a query or obtain the expected
 * result. To that end we expose PQstatus() of the connection.
 *
 * We don't use the same enum values as in libpq because we want to have the
 * unknown value when we didn't try to connect yet.
 */
typedef enum
{
	PG_CONNECTION_UNKNOWN = 0,
	PG_CONNECTION_OK,
	PG_CONNECTION_BAD
} PGConnStatus;

/* notification processing */
typedef bool (*ProcessNotificationFunction)(int notificationGroupId,
											int64_t notificationNodeId,
											char *channel, char *payload);

typedef struct PGSQL
{
	ConnectionType connectionType;
	ConnectionStatementType connectionStatementType;
	char connectionString[MAXCONNINFO];
	PGconn *connection;
	ConnectionRetryPolicy retryPolicy;
	PGConnStatus status;

	ProcessNotificationFunction notificationProcessFunction;
	int notificationGroupId;
	int64_t notificationNodeId;
	bool notificationReceived;
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
	int64_t nodeId;
	char name[_POSIX_HOST_NAME_MAX];
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	int tli;
	char lsn[PG_LSN_MAXLENGTH];
	bool isPrimary;
} NodeAddress;

typedef struct NodeAddressArray
{
	int count;
	NodeAddress nodes[NODE_ARRAY_MAX_COUNT];
} NodeAddressArray;


/*
 * TimeLineHistoryEntry is taken from Postgres definitions and adapted to
 * client-size code where we don't have all the necessary infrastruture. In
 * particular we don't define a XLogRecPtr data type nor do we define a
 * TimeLineID data type.
 *
 * Zero is used indicate an invalid pointer. Bootstrap skips the first possible
 * WAL segment, initializing the first WAL page at WAL segment size, so no XLOG
 * record can begin at zero.
 */
#define InvalidXLogRecPtr 0
#define XLogRecPtrIsInvalid(r) ((r) == InvalidXLogRecPtr)

#define PG_AUTOCTL_MAX_TIMELINES 1024

typedef struct TimeLineHistoryEntry
{
	uint32_t tli;
	uint64_t begin;         /* inclusive */
	uint64_t end;           /* exclusive, InvalidXLogRecPtr means infinity */
} TimeLineHistoryEntry;


typedef struct TimeLineHistory
{
	int count;
	TimeLineHistoryEntry history[PG_AUTOCTL_MAX_TIMELINES];
} TimeLineHistory;


/*
 * The IdentifySystem contains information that is parsed from the
 * IDENTIFY_SYSTEM replication command, and then the TIMELINE_HISTORY result.
 */
typedef struct IdentifySystem
{
	uint64_t identifier;
	uint32_t timeline;
	char xlogpos[PG_LSN_MAXLENGTH];
	char dbname[NAMEDATALEN];
	TimeLineHistory timelines;
} IdentifySystem;


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
	char maximumBackupRate[MAXIMUM_BACKUP_RATE_LEN];
	char backupDir[MAXCONNINFO];
	char applicationName[MAXCONNINFO];
	char targetLSN[PG_LSN_MAXLENGTH];
	char targetAction[NAMEDATALEN];
	char targetTimeline[NAMEDATALEN];
	SSLOptions sslOptions;
	IdentifySystem system;
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

#define STR_ERRCODE_CLASS_CONNECTION_EXCEPTION "08"

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
	int ntuples;
	bool boolVal;
	int intVal;
	uint64_t bigint;
	char *strVal;
} SingleValueResultContext;


#define CHECK__SETTINGS_SQL \
	"select bool_and(ok) " \
	"from (" \
	"select current_setting('max_wal_senders')::int >= 12" \
	" union all " \
	"select current_setting('max_replication_slots')::int >= 12" \
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

void pgsql_set_retry_policy(ConnectionRetryPolicy *retryPolicy,
							int maxT,
							int maxR,
							int maxSleepTime,
							int baseSleepTime);
void pgsql_set_main_loop_retry_policy(ConnectionRetryPolicy *retryPolicy);
void pgsql_set_init_retry_policy(ConnectionRetryPolicy *retryPolicy);
void pgsql_set_interactive_retry_policy(ConnectionRetryPolicy *retryPolicy);
void pgsql_set_monitor_interactive_retry_policy(ConnectionRetryPolicy *retryPolicy);
int pgsql_compute_connection_retry_sleep_time(ConnectionRetryPolicy *retryPolicy);
bool pgsql_retry_policy_expired(ConnectionRetryPolicy *retryPolicy);

void pgsql_finish(PGSQL *pgsql);
void parseSingleValueResult(void *ctx, PGresult *result);
void fetchedRows(void *ctx, PGresult *result);
bool pgsql_begin(PGSQL *pgsql);
bool pgsql_commit(PGSQL *pgsql);
bool pgsql_rollback(PGSQL *pgsql);
bool pgsql_execute(PGSQL *pgsql, const char *sql);
bool pgsql_execute_with_params(PGSQL *pgsql, const char *sql, int paramCount,
							   const Oid *paramTypes, const char **paramValues,
							   void *parseContext, ParsePostgresResultCB *parseFun);
bool pgsql_check_postgresql_settings(PGSQL *pgsql, bool isCitusInstanceKind,
									 bool *settings_are_ok);
bool pgsql_check_monitor_settings(PGSQL *pgsql, bool *settings_are_ok);
bool pgsql_is_in_recovery(PGSQL *pgsql, bool *is_in_recovery);
bool pgsql_reload_conf(PGSQL *pgsql);
bool pgsql_replication_slot_exists(PGSQL *pgsql, const char *slotName,
								   bool *slotExists);
bool pgsql_create_replication_slot(PGSQL *pgsql, const char *slotName);
bool pgsql_drop_replication_slot(PGSQL *pgsql, const char *slotName);
bool postgres_sprintf_replicationSlotName(int64_t nodeId, char *slotName, int size);
bool pgsql_set_synchronous_standby_names(PGSQL *pgsql,
										 char *synchronous_standby_names);
bool pgsql_replication_slot_create_and_drop(PGSQL *pgsql,
											NodeAddressArray *nodeArray);
bool pgsql_replication_slot_maintain(PGSQL *pgsql, NodeAddressArray *nodeArray);
bool pgsql_disable_synchronous_replication(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_only(PGSQL *pgsql);
bool pgsql_set_default_transaction_mode_read_write(PGSQL *pgsql);
bool pgsql_checkpoint(PGSQL *pgsql);
bool pgsql_get_hba_file_path(PGSQL *pgsql, char *hbaFilePath, int maxPathLength);
bool pgsql_create_database(PGSQL *pgsql, const char *dbname, const char *owner);
bool pgsql_create_extension(PGSQL *pgsql, const char *name);
bool pgsql_create_user(PGSQL *pgsql, const char *userName, const char *password,
					   bool login, bool superuser, bool replication,
					   int connlimit);
bool pgsql_has_replica(PGSQL *pgsql, char *userName, bool *hasReplica);
bool hostname_from_uri(const char *pguri,
					   char *hostname, int maxHostLength, int *port);
bool validate_connection_string(const char *connectionString);
bool pgsql_reset_primary_conninfo(PGSQL *pgsql);

bool pgsql_get_postgres_metadata(PGSQL *pgsql,
								 bool *pg_is_in_recovery,
								 char *pgsrSyncState, char *currentLSN,
								 PostgresControlData *control);

bool pgsql_one_slot_has_reached_target_lsn(PGSQL *pgsql,
										   char *targetLSN,
										   char *currentLSN,
										   bool *hasReachedLSN);
bool pgsql_has_reached_target_lsn(PGSQL *pgsql, char *targetLSN,
								  char *currentLSN, bool *hasReachedLSN);
bool pgsql_identify_system(PGSQL *pgsql, IdentifySystem *system);
bool pgsql_listen(PGSQL *pgsql, char *channels[]);
bool pgsql_prepare_to_wait(PGSQL *pgsql);

bool pgsql_alter_extension_update_to(PGSQL *pgsql,
									 const char *extname, const char *version);

bool parseTimeLineHistory(const char *filename, const char *content,
						  IdentifySystem *system);


#endif /* PGSQL_H */
