/*
 * src/bin/pg_autoctl/monitor_config.h
 *     Monitor configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef MONITOR_CONFIG_H
#define MONITOR_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"
#include "pgctl.h"
#include "parson.h"
#include "pgsql.h"

typedef struct MonitorConfig
{
	/* in-memory configuration related variables */
	ConfigFilePaths pathnames;

	/* pg_autoctl setup */
	char hostname[_POSIX_HOST_NAME_MAX];

	/* PostgreSQL setup */
	char role[NAMEDATALEN];

	/* PostgreSQL setup */
	PostgresSetup pgSetup;
} MonitorConfig;


bool monitor_config_set_pathnames_from_pgdata(MonitorConfig *config);
void monitor_config_init(MonitorConfig *config,
						 bool missing_pgdata_is_ok,
						 bool pg_is_not_running_is_ok);
bool monitor_config_init_from_pgsetup(MonitorConfig *mconfig,
									  PostgresSetup *pgSetup,
									  bool missingPgdataIsOk,
									  bool pgIsNotRunningIsOk);
bool monitor_config_read_file(MonitorConfig *config,
							  bool missing_pgdata_is_ok,
							  bool pg_not_running_is_ok);
bool monitor_config_write_file(MonitorConfig *config);
bool monitor_config_write(FILE *stream, MonitorConfig *config);
bool monitor_config_to_json(MonitorConfig *config, JSON_Value *js);
void monitor_config_log_settings(MonitorConfig config);
bool monitor_config_merge_options(MonitorConfig *config,
								  MonitorConfig *options);
bool monitor_config_get_postgres_uri(MonitorConfig *config, char *connectionString,
									 size_t size);

bool monitor_config_get_setting(MonitorConfig *config,
								const char *path, char *value, size_t size);
bool monitor_config_set_setting(MonitorConfig *config,
								const char *path, char *value);

bool monitor_config_update_with_absolute_pgdata(MonitorConfig *config);

bool monitor_config_accept_new(MonitorConfig *config, MonitorConfig *newConfig);

#endif  /*  MONITOR_CONFIG_H */
