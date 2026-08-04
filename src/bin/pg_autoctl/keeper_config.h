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
#include "defaults.h"
#include "pgctl.h"
#include "pgsql.h"
#include "string_utils.h"

/*
 * We support "primary" and "secondary" roles in Citus, when Citus support is
 * enabled.
 */
typedef enum
{
	CITUS_ROLE_UNKNOWN = 0,
	CITUS_ROLE_PRIMARY,
	CITUS_ROLE_SECONDARY
} CitusRole;


typedef struct KeeperConfig
{
	/* in-memory configuration related variables */
	ConfigFilePaths pathnames;

	/* who's in charge? pg_auto_failover monitor, or a control plane? */
	bool monitorDisabled;

	/* pg_autoctl setup */
	char role[NAMEDATALEN];
	char monitor_pguri[MAXCONNINFO];
	char formation[NAMEDATALEN];
	int groupId;
	char name[_POSIX_HOST_NAME_MAX];
	char hostname[_POSIX_HOST_NAME_MAX];
	char nodeKind[NAMEDATALEN];

	/*
	 * The archiver's own archiverid (distinct from keeper.state.
	 * current_node_id, which holds the ARCHIVING membership row's nodeid --
	 * see cli_create_archiver's own comment). Only meaningful when nodeKind
	 * is "archiver"; 0 otherwise. archiverIdStr is the ini-persisted form
	 * (ini_file.c's INI_INT_T only supports a plain int, too narrow for a
	 * bigserial id -- same string-plus-parsed-value pattern citusRoleStr/
	 * citusRole already use in this struct), archiverId is parsed from it
	 * once at config-read time.
	 */
	char archiverIdStr[INTSTRING_MAX_DIGITS];
	int64_t archiverId;

	/* PostgreSQL setup */
	PostgresSetup pgSetup;

	/* PostgreSQL replication / tooling setup */
	char replication_slot_name[MAXCONNINFO];
	char replication_password[MAXCONNINFO];
	char monitor_password[MAXCONNINFO];
	char maximum_backup_rate[MAXIMUM_BACKUP_RATE_LEN];
	char backupDirectory[MAXPGPATH];

	/* Citus specific options and settings */
	char citusRoleStr[NAMEDATALEN];
	CitusRole citusRole;

	/* pg_autoctl timeouts */
	int network_partition_timeout;
	int prepare_promotion_catchup;
	int prepare_promotion_walreceiver;
	int postgresql_restart_failure_timeout;
	int postgresql_restart_failure_max_retries;

	int citus_master_update_node_lock_cooldown;
	int citus_coordinator_wait_timeout;
	int citus_coordinator_wait_max_retries;
	int listen_notifications_timeout;

	/* allow data loss during a perform failover operation */
	bool allowDataLoss;
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
bool keeper_config_update(KeeperConfig *config, int64_t nodeId, int groupId);
bool keeper_config_update_with_absolute_pgdata(KeeperConfig *config);

#endif /* KEEPER_CONFIG_H */
