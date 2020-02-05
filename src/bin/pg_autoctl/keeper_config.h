/*
 * src/bin/pg_autoctl/keeper_config.h
 *     Keeper configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_CONFIG_H
#define KEEPER_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"
#include "pgctl.h"
#include "pgsql.h"

typedef struct KeeperConfig
{
	/* in-memory configuration related variables */
	ConfigFilePaths pathnames;

	/* who's in charge? pg_auto_failover monitor, or HTTP API? */
	bool monitorDisabled;		/* default is 0, which means enabled */

	/* pg_autoctl setup */
	char role[NAMEDATALEN];
	char monitor_pguri[MAXCONNINFO];
	char formation[NAMEDATALEN];
	int groupId;
	char nodename[_POSIX_HOST_NAME_MAX];
	char nodeKind[NAMEDATALEN];

	/* PostgreSQL setup */
	PostgresSetup pgSetup;

	/* PostgreSQL replication / tooling setup */
	char *replication_slot_name;
	char *replication_password;
	char *maximum_backup_rate;
	char backupDirectory[MAXPGPATH];

	/* pg_autoctl timeouts */
	int network_partition_timeout;
	int prepare_promotion_catchup;
	int prepare_promotion_walreceiver;
	int postgresql_restart_failure_timeout;
	int postgresql_restart_failure_max_retries;
} KeeperConfig;

#define PG_AUTOCTL_MONITOR_IS_DISABLED(config) \
	(strcmp(config->monitor_pguri, PG_AUTOCTL_MONITOR_DISABLED) == 0)

bool keeper_config_set_pathnames_from_pgdata(ConfigFilePaths *pathnames,
											 const char *pgdata);

void keeper_config_init(KeeperConfig *config,
						bool missingPgdataIsOk,
						bool pgIsNotRunningIsOk);
bool keeper_config_read_file(KeeperConfig *config,
							 bool missingPgdataIsOk,
							 bool pgIsNotRunningIsOk,
							 bool monitorDisabledIsOk);
bool keeper_config_read_file_skip_pgsetup(KeeperConfig *config,
										  bool monitorDisabledIsOk);
bool keeper_config_pgsetup_init(KeeperConfig *config,
								bool missingPgdataIsOk,
								bool pgIsNotRunningIsOk);
bool keeper_config_write_file(KeeperConfig *config);
bool keeper_config_write(FILE *stream, KeeperConfig *config);
bool keeper_config_to_json(KeeperConfig *config, JSON_Value *js);
void keeper_config_log_settings(KeeperConfig config);

bool keeper_config_get_setting(KeeperConfig *config,
							   const char *path,
							   char *value, size_t size);

bool keeper_config_set_setting(KeeperConfig *config,
							   const char *path,
							   char *value);

bool keeper_config_merge_options(KeeperConfig *config, KeeperConfig *options);
bool keeper_config_update(KeeperConfig *config, int nodeId, int groupId);
void keeper_config_destroy(KeeperConfig *config);
bool keeper_config_accept_new(KeeperConfig *config, KeeperConfig *newConfig);
bool keeper_config_update_with_absolute_pgdata(KeeperConfig *config);

#endif /* KEEPER_CONFIG_H */
