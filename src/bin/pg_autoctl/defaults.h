/*
 * src/bin/pg_autoctl/defaults.h
 *     Default values for pg_autoctl configuration settings
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef DEFAULTS_H
#define DEFAULTS_H

/* to be written in the state file */
#define PG_AUTOCTL_STATE_VERSION 1

/* additional version information for printing version on CLI */
#define PG_AUTOCTL_VERSION "1.1.0"

/* version of the extension that we requite to talk to on the monitor */
#define PG_AUTOCTL_EXTENSION_VERSION "1.1"

/* environment variable to use to make DEBUG facilities available */
#define PG_AUTOCTL_DEBUG "PG_AUTOCTL_DEBUG"
#define PG_AUTOCTL_EXTENSION_VERSION_VAR "PG_AUTOCTL_EXTENSION_VERSION"

/* default values for the pg_autoctl settings */
#define POSTGRES_PORT 5432
#define POSTGRES_DEFAULT_LISTEN_ADDRESSES "*"
#define DEFAULT_DATABASE_NAME "postgres"
#define DEFAULT_USERNAME "postgres"
#define DEFAULT_AUTH_METHOD "trust"
#define REPLICATION_SLOT_NAME_DEFAULT "pgautofailover_standby"
#define REPLICATION_SLOT_NAME_PATTERN "^pgautofailover_standby_"
#define REPLICATION_PASSWORD_DEFAULT NULL
#define REPLICATION_APPLICATION_NAME_PREFIX "pgautofailover_standby_"
#define FORMATION_DEFAULT "default"
#define GROUP_ID_DEFAULT 0
#define POSTGRES_CONNECT_TIMEOUT "5"
#define MAXIMUM_BACKUP_RATE "100M"

/* retry PQping for a maximum of 15 mins */
#define POSTGRES_PING_RETRY_TIMEOUT 900

#define PG_AUTOCTL_MONITOR_DISABLED "PG_AUTOCTL_DISABLED"

#define NETWORK_PARTITION_TIMEOUT 20
#define PREPARE_PROMOTION_CATCHUP_TIMEOUT 30
#define PREPARE_PROMOTION_WALRECEIVER_TIMEOUT 5

#define PG_AUTOCTL_KEEPER_SLEEP_TIME 5
#define PG_AUTOCTL_MONITOR_SLEEP_TIME 1

#define PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT 30

#define COORDINATOR_IS_READY_TIMEOUT 300

#define POSTGRESQL_FAILS_TO_START_TIMEOUT 20
#define POSTGRESQL_FAILS_TO_START_RETRIES 3

#define FAILOVER_FORMATION_NUMBER_SYNC_STANDBYS 1
#define FAILOVER_NODE_CANDIDATE_PRIORITY 100
#define FAILOVER_NODE_REPLICATION_QUORUM true

/* internal default for allocating strings  */
#define BUFSIZE 1024

#define AWAIT_PROMOTION_SLEEP_TIME_MS 1000

#define KEEPER_CONFIGURATION_FILENAME "pg_autoctl.cfg"
#define KEEPER_STATE_FILENAME "pg_autoctl.state"
#define KEEPER_PID_FILENAME "pg_autoctl.pid"
#define KEEPER_INIT_FILENAME "pg_autoctl.init"

#define KEEPER_SYSTEMD_SERVICE "pgautofailover"
#define KEEPER_SYSTEMD_FILENAME "pgautofailover.service"

/* pg_auto_failover monitor related constants */
#define PG_AUTOCTL_HEALTH_USERNAME "pgautofailover_monitor"
#define PG_AUTOCTL_REPLICA_USERNAME "pgautofailover_replicator"

#define PG_AUTOCTL_MONITOR_DBNAME "pg_auto_failover"
#define PG_AUTOCTL_MONITOR_EXTENSION_NAME "pgautofailover"
#define PG_AUTOCTL_MONITOR_DBOWNER "autoctl"

#define PG_AUTOCTL_MONITOR_USERNAME "autoctl_node"

/* Citus support */
#define CITUS_EXTENSION_NAME "citus"

/* Default external service provider to use to discover local IP address */
#define DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME "8.8.8.8"
#define DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT 53

/*
 * Error codes returned to the shell in case something goes wrong.
 */
#define EXIT_CODE_QUIT 0		/* it's ok, we were asked politely */
#define EXIT_CODE_BAD_ARGS 1
#define EXIT_CODE_BAD_CONFIG 2
#define EXIT_CODE_BAD_STATE 3
#define EXIT_CODE_PGSQL 4
#define EXIT_CODE_PGCTL 5
#define EXIT_CODE_MONITOR 6
#define EXIT_CODE_COORDINATOR 7
#define EXIT_CODE_KEEPER 8
#define EXIT_CODE_INTERNAL_ERROR 12


#endif /* DEFAULTS_H */
