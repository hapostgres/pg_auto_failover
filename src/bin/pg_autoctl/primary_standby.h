/*
 * src/bin/pg_autoctl/primary.h
 *     Management functions that implement the keeper state machine transitions.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef LOCAL_POSTGRES_H
#define LOCAL_POSTGRES_H


#include <stdbool.h>
#include "postgres_fe.h"
#include "pgsql.h"
#include "pgsetup.h"


/* Communication device between node-active and postgres processes */
typedef struct LocalExpectedPostgresStatus
{
	char pgStatusPath[MAXPGPATH];
	KeeperStatePostgres state;
} LocalExpectedPostgresStatus;


/*
 * PostgresVersionInfo holds the connected Postgres server's own version
 * (server_version_num/server_version/version()) and, when installed, the
 * Citus extension's version. Unlike pgsrSyncState/currentLSN, none of this
 * can change without a Postgres restart, so it's fetched once per restart
 * (see keeper_update_pg_state()'s pgIsRunning edge detection) rather than
 * on every periodic report. needsReport stays true from the moment it's
 * fetched until the next successful monitor_report_postgres_version() call.
 */
typedef struct PostgresVersionInfo
{
	int versionNum;                 /* server_version_num */
	char version[NAMEDATALEN];      /* server_version, e.g. "16.3" */
	char versionString[BUFSIZE];    /* version(), full descriptive string */
	char citusVersion[NAMEDATALEN]; /* citus extversion; "" when not installed */
	bool needsReport;

	/*
	 * lastKnownRunning is keeper_update_pg_state()'s own private edge
	 * tracker, exclusively written by that function -- deliberately NOT
	 * the same thing as LocalPostgresServer.pgIsRunning above, which other
	 * code (fsm_transition.c, primary_standby.c) also writes directly
	 * during FSM transitions. Comparing against a value only this
	 * function ever sets is what makes a not-running -> running edge
	 * reliably observable here, regardless of what else touches
	 * pgIsRunning in between two of this function's own calls.
	 */
	bool lastKnownRunning;
} PostgresVersionInfo;


/*
 * LocalPostgresServer represents a local postgres database cluster that
 * we can manage via a SQL connection and operations on the database
 * directory contained in the PostgresSetup.
 *
 * currentLSN value is kept as text for better portability. We do not
 * perform any operation on the value after it was read from database.
 */
typedef struct LocalPostgresServer
{
	PGSQL sqlClient;
	PostgresSetup postgresSetup;
	ReplicationSource replicationSource;
	bool pgIsRunning;
	char pgsrSyncState[PGSR_SYNC_STATE_MAXLENGTH];
	char currentLSN[PG_LSN_MAXLENGTH];
	uint64_t pgFirstStartFailureTs;
	int pgStartRetries;
	PgInstanceKind pgKind;
	LocalExpectedPostgresStatus expectedPgStatus;
	char standbyTargetLSN[PG_LSN_MAXLENGTH];
	char synchronousStandbyNames[BUFSIZE];
	PostgresVersionInfo pgVersion;
} LocalPostgresServer;


void local_postgres_init(LocalPostgresServer *postgres,
						 PostgresSetup *postgresSetup);
bool local_postgres_set_status_path(LocalPostgresServer *postgres, bool unlink);
bool local_postgres_unlink_status_file(LocalPostgresServer *postgres);
void local_postgres_finish(LocalPostgresServer *postgres);
bool local_postgres_update(LocalPostgresServer *postgres,
						   bool postgresNotRunningIsOk);
bool ensure_postgres_service_is_running(LocalPostgresServer *postgres);
bool ensure_postgres_service_is_running_as_subprocess(LocalPostgresServer *postgres);
bool ensure_postgres_service_is_stopped(LocalPostgresServer *postgres);

bool primary_has_replica(LocalPostgresServer *postgres, char *userName,
						 bool *hasStandby);
bool upstream_has_replication_slot(ReplicationSource *upstream,
								   PostgresSetup *pgSetup,
								   bool *hasReplicationSlot);
bool primary_create_replication_slot(LocalPostgresServer *postgres,
									 char *replicationSlotName);
bool primary_drop_replication_slot(LocalPostgresServer *postgres,
								   char *replicationSlotName);
bool primary_drop_replication_slots(LocalPostgresServer *postgres);
bool primary_drop_all_replication_slots(LocalPostgresServer *postgres);
bool primary_set_synchronous_standby_names(LocalPostgresServer *postgres);
bool postgres_replication_slot_create_and_drop(LocalPostgresServer *postgres,
											   NodeAddressArray *nodeArray);
bool postgres_replication_slot_maintain(LocalPostgresServer *postgres,
										NodeAddressArray *nodeArray);
bool primary_disable_synchronous_replication(LocalPostgresServer *postgres);
bool postgres_add_default_settings(LocalPostgresServer *postgres,
								   const char *hostname);
bool primary_create_user_with_hba(LocalPostgresServer *postgres, char *userName,
								  char *password, char *hostname,
								  char *authMethod, HBAEditLevel hbaLevel,
								  int connlimit);
bool primary_create_replication_user(LocalPostgresServer *postgres,
									 char *replicationUser,
									 char *replicationPassword);
bool standby_init_replication_source(LocalPostgresServer *postgres,
									 NodeAddress *upstreamNode,
									 const char *username,
									 const char *password,
									 const char *slotName,
									 const char *maximumBackupRate,
									 const char *backupDirectory,
									 const char *targetLSN,
									 SSLOptions sslOptions,
									 int currentNodeId);
bool standby_init_database(LocalPostgresServer *postgres,
						   const char *hostname,
						   bool skipBaseBackup);
bool primary_rewind_to_standby(LocalPostgresServer *postgres);
bool postgres_maybe_do_crash_recovery(LocalPostgresServer *postgres);
bool standby_promote(LocalPostgresServer *postgres);
bool check_postgresql_settings(LocalPostgresServer *postgres,
							   bool *settings_are_ok);
bool primary_standby_has_caught_up(LocalPostgresServer *postgres);
bool standby_follow_new_primary(LocalPostgresServer *postgres);
bool standby_fetch_missing_wal(LocalPostgresServer *postgres);
bool standby_restart_with_current_replication_source(LocalPostgresServer *postgres);
bool standby_cleanup_as_primary(LocalPostgresServer *postgres);
bool standby_check_timeline_with_upstream(LocalPostgresServer *postgres);


#endif /* LOCAL_POSTGRES_H */
