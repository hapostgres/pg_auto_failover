/*
 * src/bin/pg_autoctl/debian.h
 *
 *   Debian specific code to support registering a pg_autoctl node from a
 *   Postgres cluster created with pg_createcluster. We need to move the
 *   configuration files back to PGDATA.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef DEBIAN_H
#define DEBIAN_H

#include "keeper_config.h"
#include "pgsetup.h"

/*
 * We know how to find configuration files in either PGDATA as per Postgres
 * core, or in the debian cluster configuration directory as per debian
 * postgres-common packaging, implemented in pg_createcluster.
 */
typedef enum
{
	PG_CONFIG_TYPE_UNKNOWN = 0,
	PG_CONFIG_TYPE_POSTGRES,
	PG_CONFIG_TYPE_DEBIAN
} PostgresConfigurationKind;

/*
 * debian's pg_createcluster moves the 3 configuration files to a place in /etc:
 *
 *  - postgresql.conf
 *  - pg_ident.conf
 *  - pg_hba.conf
 *
 * On top of that debian also manages a "start.conf" file to decide if their
 * systemd integration should manage a given cluster.
 */
typedef struct pg_config_files
{
	PostgresConfigurationKind kind;
	char conf[MAXPGPATH];
	char ident[MAXPGPATH];
	char hba[MAXPGPATH];
} PostgresConfigFiles;


/*
 * debian handles paths for data_directory and configuration directory that
 * depend on two components: Postgres version string ("11", "12", etc) and
 * debian cluster name (defaults to "main").
 */
typedef struct debian_pathnames
{
	char versionName[PG_VERSION_STRING_MAX];
	char clusterName[MAXPGPATH];

	char dataDirectory[MAXPGPATH];
	char confDirectory[MAXPGPATH];
} DebianPathnames;


bool keeper_ensure_pg_configuration_files_in_pgdata(PostgresSetup *pgSetup);


#endif /* DEBIAN_H */
