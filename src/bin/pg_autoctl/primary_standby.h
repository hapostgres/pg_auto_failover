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
} LocalPostgresServer;


void local_postgres_init(LocalPostgresServer *postgres,
						 PostgresSetup *postgresSetup);
bool local_postgres_set_status_path(LocalPostgresServer *postgres, bool unlink);
void local_postgres_finish(LocalPostgresServer *postgres);
bool local_postgres_update(LocalPostgresServer *postgres,
						   bool postgresNotRunningIsOk);
bool ensure_local_postgres_is_running(LocalPostgresServer *postgres);
bool ensure_postgres_service_is_running(LocalPostgresServer *postgres);
bool ensure_postgres_service_is_stopped(LocalPostgresServer *postgres);

bool primary_has_replica(LocalPostgresServer *postgres, char *userName,
						 bool *hasStandby);
bool primary_create_replication_slot(LocalPostgresServer *postgres,
									 char *replicationSlotName);
bool primary_drop_replication_slot(LocalPostgresServer *postgres,
								   char *replicationSlotName);
bool primary_drop_replication_slots(LocalPostgresServer *postgres);
bool primary_set_synchronous_standby_names(LocalPostgresServer *postgres,
										   char *synchronous_standby_names);
bool postgres_replication_slot_drop_removed(LocalPostgresServer *postgres,
											NodeAddressArray *nodeArray);
bool postgres_replication_slot_maintain(LocalPostgresServer *postgres,
										NodeAddressArray *nodeArray);
bool primary_enable_synchronous_replication(LocalPostgresServer *postgres);
bool primary_disable_synchronous_replication(LocalPostgresServer *postgres);
bool postgres_add_default_settings(LocalPostgresServer *postgres);
bool primary_create_replication_user(LocalPostgresServer *postgres,
									 char *replicationUser,
									 char *replicationPassword);
bool primary_add_standby_to_hba(LocalPostgresServer *postgres,
								char *standbyHost, const char *replicationPassword);
bool standby_init_replication_source(LocalPostgresServer *postgres,
									 NodeAddress *primaryNode,
									 const char *username,
									 const char *password,
									 const char *slotName,
									 const char *maximumBackupRate,
									 const char *backupDirectory,
									 SSLOptions sslOptions,
									 int currentNodeId);
bool standby_init_database(LocalPostgresServer *postgres, const char *hostname);
bool primary_rewind_to_standby(LocalPostgresServer *postgres);
bool standby_promote(LocalPostgresServer *postgres);
bool check_postgresql_settings(LocalPostgresServer *postgres,
							   bool *settings_are_ok);


#endif /* LOCAL_POSTGRES_H */
