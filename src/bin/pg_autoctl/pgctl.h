/*
 * src/bin/pg_autoctl/pgctl.h
 *   API for controling PostgreSQL, using its binary tooling (pg_ctl,
 *   pg_controldata, pg_basebackup and such).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGCTL_H
#define PGCTL_H

#include <limits.h>
#include <stdbool.h>
#include <string.h>

#include "postgres_fe.h"
#include "utils/pidfile.h"

#include "defaults.h"
#include "file_utils.h"
#include "pgsetup.h"
#include "pgsql.h"

#define AUTOCTL_DEFAULTS_CONF_FILENAME "postgresql-auto-failover.conf"
#define AUTOCTL_STANDBY_CONF_FILENAME "postgresql-auto-failover-standby.conf"

#define PG_CTL_STATUS_NOT_RUNNING 3

bool pg_controldata(PostgresSetup *pgSetup, bool missing_ok);
bool set_pg_ctl_from_PG_CONFIG(PostgresSetup *pgSetup);
bool set_pg_ctl_from_pg_config(PostgresSetup *pgSetup);
bool config_find_pg_ctl(PostgresSetup *pgSetup);
bool find_extension_control_file(const char *pg_ctl, const char *extName);
bool pg_ctl_version(PostgresSetup *pgSetup);
bool set_pg_ctl_from_config_bindir(PostgresSetup *pgSetup, const char *pg_config);
bool find_pg_config_from_pg_ctl(const char *pg_ctl, char *pg_config, size_t size);

bool pg_add_auto_failover_default_settings(PostgresSetup *pgSetup,
										   const char *hostname,
										   const char *configFilePath,
										   GUC *settings);

bool pg_auto_failover_default_settings_file_exists(PostgresSetup *pgSetup);

bool pg_basebackup(const char *pgdata,
				   const char *pg_ctl,
				   ReplicationSource *replicationSource);
bool pg_rewind(const char *pgdata,
			   const char *pg_ctl,
			   ReplicationSource *replicationSource);

bool pg_ctl_initdb(const char *pg_ctl, const char *pgdata);
bool pg_ctl_postgres(const char *pg_ctl, const char *pgdata, int pgport,
					 char *listen_addresses, bool listen);
bool pg_log_startup(const char *pgdata, int logLevel);
bool pg_log_recovery_setup(const char *pgdata, int logLevel);
bool pg_ctl_stop(const char *pg_ctl, const char *pgdata);
int pg_ctl_status(const char *pg_ctl, const char *pgdata, bool log_output);
bool pg_ctl_promote(const char *pg_ctl, const char *pgdata);

bool pg_setup_standby_mode(uint32_t pg_control_version,
						   const char *pg_ctl,
						   const char *pgdata,
						   ReplicationSource *replicationSource);

bool pg_cleanup_standby_mode(uint32_t pg_control_version,
							 const char *pg_ctl,
							 const char *pgdata,
							 PGSQL *pgsql);

bool pgctl_identify_system(ReplicationSource *replicationSource);

bool pg_is_running(const char *pg_ctl, const char *pgdata);
bool pg_create_self_signed_cert(PostgresSetup *pgSetup, const char *hostname);

#endif /* PGCTL_H */
