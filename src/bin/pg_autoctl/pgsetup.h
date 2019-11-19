/*
 * src/bin/pg_autoctl/pgsetup.h
 *   Discovers a PostgreSQL setup by calling pg_controldata and reading
 *   postmaster.pid file, getting clues from the process environment and from
 *   user given hints (options).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGSETUP_H
#define PGSETUP_H

#include <limits.h>

#include "postgres_fe.h"

#include "parson.h"

/*
 * To be able to check if a minor upgrade should be scheduled, and to check for
 * system WAL compatiblity, we use some parts of the pg_controldata output.
 *
 * See postgresql/src/include/catalog/pg_control.h for definitions of the
 * following fields of the ControlFileData struct.
 */
typedef struct pg_control_data
{
	uint32_t pg_control_version;        /* PG_CONTROL_VERSION */
	uint32_t catalog_version_no;        /* see catversion.h */
	uint64_t system_identifier;
} PostgresControlData;

/*
 * We don't need the full information set form the pidfile, it onyl allows us
 * to guess/retrieve the PostgreSQL port number from the PGDATA without having
 * to ask the user to provide the information.
 */
typedef struct pg_pidfile
{
	long pid;
	unsigned short port;
} PostgresPIDFile;

/*
 * From pidfile.h we also extract the Postmaster status, one of the following
 * values:
 */
typedef enum
{
	POSTMASTER_STATUS_UNKNOWN = 0,
	POSTMASTER_STATUS_STARTING,
	POSTMASTER_STATUS_STOPPING,
	POSTMASTER_STATUS_READY,
	POSTMASTER_STATUS_STANDBY
} PostmasterStatus;

/*
 * pg_auto_failover knows how to manage three kinds of PostgreSQL servers:
 *
 *  - Standalone PostgreSQL instances
 *  - Citus Coordinator PostgreSQL instances
 *  - Citus Worker PostgreSQL instances
 *
 * Each of them may then take on the role of a primary or a standby depending
 * on circumstances. Citus coordinator and worker instances need to load the
 * citus extension in shared_preload_libraries, which the keeper ensures.
 *
 * At failover time, when dealing with a Citus worker instance, the keeper
 * fetches its coordinator nodename and port from the monitor and blocks writes
 * using the citus master_update_node() function call in a prepared
 * transaction.
 */

typedef enum PgInstanceKind
{
	NODE_KIND_UNKNOWN = 0,
	NODE_KIND_STANDALONE,
	NODE_KIND_CITUS_COORDINATOR,
	NODE_KIND_CITUS_WORKER
} PgInstanceKind;


#define IS_CITUS_INSTANCE_KIND(x)				\
	(x == NODE_KIND_CITUS_COORDINATOR			\
	 || x == NODE_KIND_CITUS_WORKER)


#define PG_VERSION_STRING_MAX 12

/*
 * In the PostgresSetup structure, we use pghost either as socket directory
 * name or as a hostname. We could use MAXPGPATH rather than
 * _POSIX_HOST_NAME_MAX chars in that name, but then again the the hostname is
 * part of a connection string that must be held in MAXCONNINFO.
 *
 * If you want to change pghost[_POSIX_HOST_NAME_MAX], keep that in mind!
 */
typedef struct pg_setup
{
	char pgdata[MAXPGPATH];                 /* PGDATA */
	char pg_ctl[MAXPGPATH];                 /* absolute path to pg_ctl */
	char pg_version[PG_VERSION_STRING_MAX]; /* pg_ctl --version */
	char username[NAMEDATALEN];             /* username, defaults to USER */
	char dbname[NAMEDATALEN];               /* dbname, defaults to PGDATABASE */
	char pghost[_POSIX_HOST_NAME_MAX];      /* local PGHOST to connect to */
	int pgport;                             /* PGPORT */
	char listen_addresses[MAXPGPATH];       /* listen_addresses */
	int proxyport;                          /* Proxy port */
	char authMethod[NAMEDATALEN];           /* auth method, defaults to trust */
	PostmasterStatus pm_status;				/* Postmaster status */
	bool is_in_recovery;                    /* select pg_is_in_recovery() */
	PostgresControlData control;            /* pg_controldata pgdata */
	PostgresPIDFile pidFile;                /* postmaster.pid information */
	PgInstanceKind pgKind;					/* standalone/coordinator/worker */
} PostgresSetup;

#define IS_EMPTY_STRING_BUFFER(strbuf) (strbuf[0] == '\0')

bool pg_setup_init(PostgresSetup *pgSetup,
				   PostgresSetup *options,
				   bool missing_pgdata_is_ok,
				   bool pg_is_not_running_is_ok);

bool read_pg_pidfile(PostgresSetup *pgSetup, bool pg_is_not_running_is_ok);

void fprintf_pg_setup(FILE *stream, PostgresSetup *pgSetup);
bool pg_setup_as_json(PostgresSetup *pgSetup, JSON_Object *jsobj);

bool pg_setup_get_local_connection_string(PostgresSetup *pgSetup,
										  char *connectionString);
bool pg_setup_pgdata_exists(PostgresSetup *pgSetup);
bool pg_setup_is_running(PostgresSetup *pgSetup);
bool pg_setup_is_primary(PostgresSetup *pgSetup);
bool pg_setup_is_ready(PostgresSetup *pgSetup, bool pg_is_not_running_is_ok);
char *pg_setup_get_username(PostgresSetup *pgSetup);
char *pg_setup_get_auth_method(PostgresSetup *pgSetup);
bool pg_setup_set_absolute_pgdata(PostgresSetup *pgSetup);

PgInstanceKind nodeKindFromString(const char *nodeKind);
char *nodeKindToString(PgInstanceKind kind);

#endif /* PGSETUP_H */
