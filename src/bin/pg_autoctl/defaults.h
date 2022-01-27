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
#define PG_AUTOCTL_VERSION "1.6.4"

/* version of the extension that we requite to talk to on the monitor */
#define PG_AUTOCTL_EXTENSION_VERSION "1.6"

/* environment variable to use to make DEBUG facilities available */
#define PG_AUTOCTL_DEBUG "PG_AUTOCTL_DEBUG"
#define PG_AUTOCTL_EXTENSION_VERSION_VAR "PG_AUTOCTL_EXTENSION_VERSION"

/* environment variable for containing the id of the logging semaphore */
#define PG_AUTOCTL_LOG_SEMAPHORE "PG_AUTOCTL_LOG_SEMAPHORE"

/* environment variable for --monitor, when used instead of --pgdata */
#define PG_AUTOCTL_MONITOR "PG_AUTOCTL_MONITOR"

/* environment variable for --candidate-priority and --replication-quorum */
#define PG_AUTOCTL_NODE_NAME "PG_AUTOCTL_NODE_NAME"
#define PG_AUTOCTL_CANDIDATE_PRIORITY "PG_AUTOCTL_CANDIDATE_PRIORITY"
#define PG_AUTOCTL_REPLICATION_QUORUM "PG_AUTOCTL_REPLICATION_QUORUM"

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
#define POSTGRES_CONNECT_TIMEOUT "2"
#define MAXIMUM_BACKUP_RATE "100M"
#define MAXIMUM_BACKUP_RATE_LEN 32


/*
 * Microsoft approved cipher string.
 * This cipher string implicitely enables only TLSv1.2+, because these ciphers
 * were all added in TLSv1.2. This can be confirmed by running:
 * openssl -v <below strings concatenated>
 */
#define DEFAULT_SSL_CIPHERS "ECDHE-ECDSA-AES128-GCM-SHA256:" \
							"ECDHE-ECDSA-AES256-GCM-SHA384:" \
							"ECDHE-RSA-AES128-GCM-SHA256:" \
							"ECDHE-RSA-AES256-GCM-SHA384:" \
							"ECDHE-ECDSA-AES128-SHA256:" \
							"ECDHE-ECDSA-AES256-SHA384:" \
							"ECDHE-RSA-AES128-SHA256:" \
							"ECDHE-RSA-AES256-SHA384"


/* retry PQping for a maximum of 15 mins, up to 2 secs between attemps */
#define POSTGRES_PING_RETRY_TIMEOUT 900               /* seconds */
#define POSTGRES_PING_RETRY_CAP_SLEEP_TIME (2 * 1000) /* milliseconds */
#define POSTGRES_PING_RETRY_BASE_SLEEP_TIME 5         /* milliseconds */

#define PG_AUTOCTL_MONITOR_DISABLED "PG_AUTOCTL_DISABLED"

#define NETWORK_PARTITION_TIMEOUT 20
#define PREPARE_PROMOTION_CATCHUP_TIMEOUT 30
#define PREPARE_PROMOTION_WALRECEIVER_TIMEOUT 5

#define PG_AUTOCTL_KEEPER_SLEEP_TIME 1      /* seconds */
#define PG_AUTOCTL_KEEPER_RETRY_TIME_MS 350 /* milliseconds */
#define PG_AUTOCTL_MONITOR_SLEEP_TIME 10 /* seconds */
#define PG_AUTOCTL_MONITOR_RETRY_TIME 1  /* seconds */

#define PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT 60

#define COORDINATOR_IS_READY_TIMEOUT 300

#define POSTGRESQL_FAILS_TO_START_TIMEOUT 20
#define POSTGRESQL_FAILS_TO_START_RETRIES 3

#define DEFAULT_CITUS_ROLE "primary"
#define DEFAULT_CITUS_CLUSTER_NAME "default"

#define FAILOVER_FORMATION_NUMBER_SYNC_STANDBYS 1
#define FAILOVER_NODE_CANDIDATE_PRIORITY 50
#define FAILOVER_NODE_REPLICATION_QUORUM true

/* internal default for allocating strings  */
#define BUFSIZE 1024

/*
 * 50kB seems enough to store the PATH environment variable if you have more,
 * simply set PATH to something smaller.
 * The limit on linux for environment variables is 128kB:
 * https://unix.stackexchange.com/questions/336934
 */
#define MAXPATHSIZE 50000


/* buffersize that is needed for results of ctime_r */
#define MAXCTIMESIZE 26

#define AWAIT_PROMOTION_SLEEP_TIME_MS 1000

#define KEEPER_CONFIGURATION_FILENAME "pg_autoctl.cfg"
#define KEEPER_STATE_FILENAME "pg_autoctl.state"
#define KEEPER_PID_FILENAME "pg_autoctl.pid"
#define KEEPER_INIT_STATE_FILENAME "pg_autoctl.init"
#define KEEPER_POSTGRES_STATE_FILENAME "pg_autoctl.pg"
#define KEEPER_NODES_FILENAME "nodes.json"

#define KEEPER_SYSTEMD_SERVICE "pgautofailover"
#define KEEPER_SYSTEMD_FILENAME "pgautofailover.service"

/* pg_auto_failover monitor related constants */
#define PG_AUTOCTL_HEALTH_USERNAME "pgautofailover_monitor"
#define PG_AUTOCTL_HEALTH_PASSWORD "pgautofailover_monitor"
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
#define EXIT_CODE_QUIT 0        /* it's ok, we were asked politely */
#define EXIT_CODE_BAD_ARGS 1
#define EXIT_CODE_BAD_CONFIG 2
#define EXIT_CODE_BAD_STATE 3
#define EXIT_CODE_PGSQL 4
#define EXIT_CODE_PGCTL 5
#define EXIT_CODE_MONITOR 6
#define EXIT_CODE_COORDINATOR 7
#define EXIT_CODE_KEEPER 8
#define EXIT_CODE_RELOAD 9
#define EXIT_CODE_INTERNAL_ERROR 12
#define EXIT_CODE_EXTENSION_MISSING 13
#define EXIT_CODE_DROPPED 121   /* node was dropped, stop everything and quit */
#define EXIT_CODE_FATAL 122     /* error is fatal, no retry, quit now */

/*
 * This opens file write only and creates if it doesn't exist.
 */
#define FOPEN_FLAGS_W O_WRONLY | O_TRUNC | O_CREAT

/*
 * This opens the file in append mode and creates it if it doesn't exist.
 */
#define FOPEN_FLAGS_A O_APPEND | O_RDWR | O_CREAT


/* when malloc fails, what do we tell our users */
#define ALLOCATION_FAILED_ERROR "Failed to allocate memory: %m"

#endif /* DEFAULTS_H */
