/*
 * src/bin/pg_autoctl/pbouncer_config.h
 *     Keeper integration with pgbouncer configuration file
 *
 * Copyright (c) XXX: FIll in As requested
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGBOUNCER_CONFIG_H
#define PGBOUNCER_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"

typedef struct PgbouncerConfig
{
	ConfigFilePaths pathnames;

	/* UNIT */
	char Description[BUFSIZE];

	/* User Supplied options */
	char userSuppliedConfig[MAXPGPATH];

	/* Absolute Path of pgbouncer binary */
	char pgbouncerProg[MAXPGPATH];

	/* PostgreSQL setup */
	PostgresSetup pgSetup;
	NodeAddress primary;

	/* Monitor uri to connect to */
	char monitor_pguri[MAXCONNINFO];

	/* Formation and group we belong to */
	char formation[NAMEDATALEN];
	int groupId;

	/* Private member */
	void *data;
} PgbouncerConfig;

bool pgbouncer_config_init(PgbouncerConfig *config, const char *pgdata);
bool pgbouncer_config_destroy(PgbouncerConfig *config);
bool pgbouncer_config_read_template(PgbouncerConfig *config);
bool pgbouncer_config_read_user_supplied_ini(PgbouncerConfig *config);
bool pgbouncer_config_write_runtime(PgbouncerConfig *config);
bool pgbouncer_config_write_template(PgbouncerConfig *config);

#endif /* PGBOUNCER_CONFIG_H */
