/*
 * src/bin/pg_autoctl/keeper_config.c
 *     Keeper configuration functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "defaults.h"
#include "ini_file.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "parsing.h"
#include "pgctl.h"

#define OPTION_AUTOCTL_ROLE(config) \
	make_strbuf_option_default("pg_autoctl", "role", NULL, true, NAMEDATALEN, \
							   config->role, KEEPER_ROLE)

#define OPTION_AUTOCTL_MONITOR(config) \
	make_strbuf_option("pg_autoctl", "monitor", "monitor", false, MAXCONNINFO, \
					   config->monitor_pguri)

#define OPTION_AUTOCTL_FORMATION(config) \
	make_strbuf_option_default("pg_autoctl", "formation", "formation", \
							   true, NAMEDATALEN, \
							   config->formation, FORMATION_DEFAULT)

#define OPTION_AUTOCTL_GROUPID(config) \
	make_int_option("pg_autoctl", "group", "group", false, &(config->groupId))

#define OPTION_AUTOCTL_NAME(config) \
	make_strbuf_option_default("pg_autoctl", "name", "name", \
							   false, _POSIX_HOST_NAME_MAX, \
							   config->name, "")

/*
 * --hostname used to be --nodename, and we need to support transition from the
 * old to the new name. For that, we read the pg_autoctl.nodename config
 * setting and change it on the fly to hostname instead.
 *
 * As a result HOSTNAME is marked not required and NODENAME is marked compat.
 */
#define OPTION_AUTOCTL_HOSTNAME(config) \
	make_strbuf_option("pg_autoctl", "hostname", "hostname", \
					   false, _POSIX_HOST_NAME_MAX, config->hostname)

#define OPTION_AUTOCTL_NODENAME(config) \
	make_strbuf_compat_option("pg_autoctl", "nodename", \
							  _POSIX_HOST_NAME_MAX, config->hostname)

#define OPTION_AUTOCTL_NODEKIND(config) \
	make_strbuf_option("pg_autoctl", "nodekind", NULL, false, NAMEDATALEN, \
					   config->nodeKind)

#define OPTION_POSTGRESQL_PGDATA(config) \
	make_strbuf_option("postgresql", "pgdata", "pgdata", true, MAXPGPATH, \
					   config->pgSetup.pgdata)

#define OPTION_POSTGRESQL_PG_CTL(config) \
	make_strbuf_option("postgresql", "pg_ctl", "pgctl", false, MAXPGPATH, \
					   config->pgSetup.pg_ctl)

#define OPTION_POSTGRESQL_USERNAME(config) \
	make_strbuf_option("postgresql", "username", "username", \
					   false, NAMEDATALEN, \
					   config->pgSetup.username)

#define OPTION_POSTGRESQL_DBNAME(config) \
	make_strbuf_option("postgresql", "dbname", "dbname", false, NAMEDATALEN, \
					   config->pgSetup.dbname)

#define OPTION_POSTGRESQL_HOST(config) \
	make_strbuf_option("postgresql", "host", "pghost", \
					   false, _POSIX_HOST_NAME_MAX, \
					   config->pgSetup.pghost)

#define OPTION_POSTGRESQL_PORT(config) \
	make_int_option("postgresql", "port", "pgport", \
					true, &(config->pgSetup.pgport))

#define OPTION_POSTGRESQL_PROXY_PORT(config) \
	make_int_option("postgresql", "proxyport", "proxyport", \
					false, &(config->pgSetup.proxyport))

#define OPTION_POSTGRESQL_LISTEN_ADDRESSES(config) \
	make_strbuf_option("postgresql", "listen_addresses", "listen", \
					   false, MAXPGPATH, config->pgSetup.listen_addresses)

#define OPTION_POSTGRESQL_AUTH_METHOD(config) \
	make_strbuf_option("postgresql", "auth_method", "auth", \
					   false, MAXPGPATH, config->pgSetup.authMethod)

#define OPTION_POSTGRESQL_HBA_LEVEL(config) \
	make_strbuf_option("postgresql", "hba_level", NULL, \
					   false, MAXPGPATH, config->pgSetup.hbaLevelStr)

#define OPTION_SSL_ACTIVE(config) \
	make_int_option_default("ssl", "active", NULL, \
							false, &(config->pgSetup.ssl.active), 0)

#define OPTION_SSL_MODE(config) \
	make_strbuf_option("ssl", "sslmode", "ssl-mode", \
					   false, SSL_MODE_STRLEN, config->pgSetup.ssl.sslModeStr)

#define OPTION_SSL_CA_FILE(config) \
	make_strbuf_option("ssl", "ca_file", "ssl-ca-file", \
					   false, MAXPGPATH, config->pgSetup.ssl.caFile)

#define OPTION_SSL_CRL_FILE(config) \
	make_strbuf_option("ssl", "crl_file", "ssl-crl-file", \
					   false, MAXPGPATH, config->pgSetup.ssl.crlFile)

#define OPTION_SSL_SERVER_CERT(config) \
	make_strbuf_option("ssl", "cert_file", "server-cert", \
					   false, MAXPGPATH, config->pgSetup.ssl.serverCert)

#define OPTION_SSL_SERVER_KEY(config) \
	make_strbuf_option("ssl", "key_file", "server-key", \
					   false, MAXPGPATH, config->pgSetup.ssl.serverKey)

#define OPTION_REPLICATION_PASSWORD(config) \
	make_strbuf_option_default("replication", "password", NULL, \
							   false, MAXCONNINFO, \
							   config->replication_password, \
							   REPLICATION_PASSWORD_DEFAULT)

#define OPTION_REPLICATION_MAXIMUM_BACKUP_RATE(config) \
	make_strbuf_option_default("replication", "maximum_backup_rate", NULL, \
							   false, MAXIMUM_BACKUP_RATE_LEN, \
							   config->maximum_backup_rate, \
							   MAXIMUM_BACKUP_RATE)

#define OPTION_REPLICATION_BACKUP_DIR(config) \
	make_strbuf_option("replication", "backup_directory", NULL, \
					   false, MAXPGPATH, config->backupDirectory)

#define OPTION_TIMEOUT_NETWORK_PARTITION(config) \
	make_int_option_default("timeout", "network_partition_timeout", \
							NULL, false, \
							&(config->network_partition_timeout), \
							NETWORK_PARTITION_TIMEOUT)

#define OPTION_TIMEOUT_PREPARE_PROMOTION_CATCHUP(config) \
	make_int_option_default("timeout", "prepare_promotion_catchup", \
							NULL, \
							false, \
							&(config->prepare_promotion_catchup), \
							PREPARE_PROMOTION_CATCHUP_TIMEOUT)

#define OPTION_TIMEOUT_PREPARE_PROMOTION_WALRECEIVER(config) \
	make_int_option_default("timeout", "prepare_promotion_walreceiver", \
							NULL, \
							false, \
							&(config->prepare_promotion_walreceiver), \
							PREPARE_PROMOTION_WALRECEIVER_TIMEOUT)

#define OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_TIMEOUT(config) \
	make_int_option_default("timeout", "postgresql_restart_failure_timeout", \
							NULL, \
							false, \
							&(config->postgresql_restart_failure_timeout), \
							POSTGRESQL_FAILS_TO_START_TIMEOUT)

#define OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_MAX_RETRIES(config) \
	make_int_option_default("timeout", "postgresql_restart_failure_max_retries", \
							NULL, \
							false, \
							&(config->postgresql_restart_failure_max_retries), \
							POSTGRESQL_FAILS_TO_START_RETRIES)

#define OPTION_TIMEOUT_LISTEN_NOTIFICATIONS(config) \
	make_int_option_default("timeout", "listen_notifications_timeout", \
							NULL, false, \
							&(config->listen_notifications_timeout), \
							PG_AUTOCTL_LISTEN_NOTIFICATIONS_TIMEOUT)

#define OPTION_CITUS_ROLE(config) \
	make_strbuf_option_default("citus", "role", NULL, false, NAMEDATALEN, \
							   config->citusRoleStr, DEFAULT_CITUS_ROLE)

#define OPTION_CITUS_CLUSTER_NAME(config) \
	make_strbuf_option("citus", "cluster_name", "citus-cluster", \
					   false, NAMEDATALEN, config->pgSetup.citusClusterName)


#define SET_INI_OPTIONS_ARRAY(config) \
	{ \
		OPTION_AUTOCTL_ROLE(config), \
		OPTION_AUTOCTL_MONITOR(config), \
		OPTION_AUTOCTL_FORMATION(config), \
		OPTION_AUTOCTL_GROUPID(config), \
		OPTION_AUTOCTL_NAME(config), \
		OPTION_AUTOCTL_HOSTNAME(config), \
		OPTION_AUTOCTL_NODENAME(config), \
		OPTION_AUTOCTL_NODEKIND(config), \
		OPTION_POSTGRESQL_PGDATA(config), \
		OPTION_POSTGRESQL_PG_CTL(config), \
		OPTION_POSTGRESQL_USERNAME(config), \
		OPTION_POSTGRESQL_DBNAME(config), \
		OPTION_POSTGRESQL_HOST(config), \
		OPTION_POSTGRESQL_PORT(config), \
		OPTION_POSTGRESQL_PROXY_PORT(config), \
		OPTION_POSTGRESQL_LISTEN_ADDRESSES(config), \
		OPTION_POSTGRESQL_AUTH_METHOD(config), \
		OPTION_POSTGRESQL_HBA_LEVEL(config), \
		OPTION_SSL_ACTIVE(config), \
		OPTION_SSL_MODE(config), \
		OPTION_SSL_CA_FILE(config), \
		OPTION_SSL_CRL_FILE(config), \
		OPTION_SSL_SERVER_CERT(config), \
		OPTION_SSL_SERVER_KEY(config), \
		OPTION_REPLICATION_MAXIMUM_BACKUP_RATE(config), \
		OPTION_REPLICATION_BACKUP_DIR(config), \
		OPTION_REPLICATION_PASSWORD(config), \
		OPTION_TIMEOUT_NETWORK_PARTITION(config), \
		OPTION_TIMEOUT_PREPARE_PROMOTION_CATCHUP(config), \
		OPTION_TIMEOUT_PREPARE_PROMOTION_WALRECEIVER(config), \
		OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_TIMEOUT(config), \
		OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_MAX_RETRIES(config), \
		OPTION_TIMEOUT_LISTEN_NOTIFICATIONS(config), \
 \
		OPTION_CITUS_ROLE(config), \
		OPTION_CITUS_CLUSTER_NAME(config), \
		INI_OPTION_LAST \
	}

static bool keeper_config_init_nodekind(KeeperConfig *config);
static bool keeper_config_init_hbalevel(KeeperConfig *config);
static bool keeper_config_set_backup_directory(KeeperConfig *config,
											   int64_t nodeId);


/*
 * keeper_config_set_pathnames_from_pgdata sets the config pathnames from its
 * pgSetup.pgdata field, which must have already been set when calling this
 * function.
 */
bool
keeper_config_set_pathnames_from_pgdata(ConfigFilePaths *pathnames,
										const char *pgdata)
{
	if (IS_EMPTY_STRING_BUFFER(pgdata))
	{
		/* developer error */
		log_error("BUG: keeper_config_set_pathnames_from_pgdata: empty pgdata");
		return false;
	}

	if (!SetConfigFilePath(pathnames, pgdata))
	{
		log_fatal("Failed to set configuration filename from PGDATA \"%s\","
				  " see above for details.", pgdata);
		return false;
	}

	if (!SetStateFilePath(pathnames, pgdata))
	{
		log_fatal("Failed to set state filename from PGDATA \"%s\","
				  " see above for details.", pgdata);
		return false;
	}

	if (!SetNodesFilePath(pathnames, pgdata))
	{
		log_fatal("Failed to set pid filename from PGDATA \"%s\","
				  " see above for details.", pgdata);
		return false;
	}

	if (!SetPidFilePath(pathnames, pgdata))
	{
		log_fatal("Failed to set pid filename from PGDATA \"%s\","
				  " see above for details.", pgdata);
		return false;
	}

	return true;
}


/*
 * keeper_config_init initializes a KeeperConfig with the default values.
 */
void
keeper_config_init(KeeperConfig *config,
				   bool missingPgdataIsOk, bool pgIsNotRunningIsOk)
{
	PostgresSetup pgSetup = { 0 };
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	log_trace("keeper_config_init");

	if (!ini_validate_options(keeperOptions))
	{
		log_error("Please review your setup options per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_config_init_nodekind(config))
	{
		/* errors have already been logged. */
		log_error("Please review your setup options per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_config_init_hbalevel(config))
	{
		log_error("Failed to initialize postgresql.hba_level");
		log_error("Please review your setup options per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!pg_setup_init(&pgSetup,
					   &(config->pgSetup),
					   missingPgdataIsOk,
					   pgIsNotRunningIsOk))
	{
		log_error("Please fix your PostgreSQL setup per above messages");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * Keep the whole set of values discovered in pg_setup_init from the
	 * configuration file
	 */
	config->pgSetup = pgSetup;

	/*
	 * Compute the backupDirectory from pgdata, or check the one given in the
	 * configuration file already.
	 */
	if (!keeper_config_set_backup_directory(config, -1))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* set our configuration and state file pathnames */
	if (!SetConfigFilePath(&(config->pathnames), config->pgSetup.pgdata))
	{
		log_error("Failed to initialize Keeper's config, see above");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!SetStateFilePath(&(config->pathnames), config->pgSetup.pgdata))
	{
		log_error("Failed to initialize Keeper's config, see above");
		exit(EXIT_CODE_BAD_CONFIG);
	}
}


/*
 * keeper_config_read_file overrides values in given KeeperConfig with whatever
 * values are read from given configuration filename.
 */
bool
keeper_config_read_file(KeeperConfig *config,
						bool missingPgdataIsOk,
						bool pgIsNotRunningIsOk,
						bool monitorDisabledIsOk)
{
	if (!keeper_config_read_file_skip_pgsetup(config, monitorDisabledIsOk))
	{
		/* errors have already been logged. */
		return false;
	}

	return keeper_config_pgsetup_init(config,
									  missingPgdataIsOk,
									  pgIsNotRunningIsOk);
}


/*
 * keeper_config_read_file_skip_pgsetup overrides values in given KeeperConfig
 * with whatever values are read from given configuration filename.
 */
bool
keeper_config_read_file_skip_pgsetup(KeeperConfig *config,
									 bool monitorDisabledIsOk)
{
	const char *filename = config->pathnames.config;
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	log_debug("Reading configuration from %s", filename);

	if (!read_ini_file(filename, keeperOptions))
	{
		log_error("Failed to parse configuration file \"%s\"", filename);
		return false;
	}

	/*
	 * We have changed the --nodename option to being named --hostname, and
	 * same in the configuration file: pg_autoctl.nodename is now
	 * pg_autoctl.hostname.
	 *
	 * We can read either names from the configuration file and will then write
	 * the current option name (pg_autoctl.hostname), but we can't have either
	 * one be required anymore.
	 *
	 * Implement the "require" property here by making sure one of those names
	 * have been used to populate the monitor config structure.
	 */
	if (IS_EMPTY_STRING_BUFFER(config->hostname))
	{
		log_error("Failed to read either pg_autoctl.hostname or its older "
				  "name pg_autoctl.nodename from the \"%s\" configuration file",
				  filename);
		return false;
	}

	/* take care of the special value for disabled monitor setup */
	if (PG_AUTOCTL_MONITOR_IS_DISABLED(config))
	{
		config->monitorDisabled = true;

		if (!monitorDisabledIsOk)
		{
			log_error("Monitor is disabled in the configuration");
			return false;
		}
	}

	/*
	 * Turn the configuration string for hbaLevel into our enum value.
	 */
	if (!keeper_config_init_hbalevel(config))
	{
		log_error("Failed to initialize postgresql.hba_level");
		return false;
	}

	/* set the ENUM value for hbaLevel */
	config->pgSetup.hbaLevel =
		pgsetup_parse_hba_level(config->pgSetup.hbaLevelStr);

	/*
	 * Required for grandfathering old clusters that don't have sslmode
	 * explicitely set
	 */
	if (IS_EMPTY_STRING_BUFFER(config->pgSetup.ssl.sslModeStr))
	{
		strlcpy(config->pgSetup.ssl.sslModeStr, "prefer", SSL_MODE_STRLEN);
	}

	/* set the ENUM value for sslMode */
	config->pgSetup.ssl.sslMode =
		pgsetup_parse_sslmode(config->pgSetup.ssl.sslModeStr);

	/* now when that is provided, read the Citus Role and convert to enum */
	if (IS_EMPTY_STRING_BUFFER(config->citusRoleStr))
	{
		config->citusRole = CITUS_ROLE_PRIMARY;
	}
	else
	{
		if (strcmp(config->citusRoleStr, "primary") == 0)
		{
			config->citusRole = CITUS_ROLE_PRIMARY;
		}
		else if (strcmp(config->citusRoleStr, "secondary") == 0)
		{
			config->citusRole = CITUS_ROLE_SECONDARY;
		}
		else
		{
			log_error("Failed to parse citus.role \"%s\": expected either "
					  "\"primary\" or \"secondary\"", config->citusRoleStr);
			return false;
		}
	}

	if (!keeper_config_init_nodekind(config))
	{
		/* errors have already been logged. */
		return false;
	}

	return true;
}


/*
 * keeper_config_pgsetup_init overrides values in given KeeperConfig with
 * whatever values are read from given configuration filename.
 */
bool
keeper_config_pgsetup_init(KeeperConfig *config,
						   bool missingPgdataIsOk,
						   bool pgIsNotRunningIsOk)
{
	PostgresSetup pgSetup = { 0 };

	log_trace("keeper_config_pgsetup_init");

	if (!pg_setup_init(&pgSetup,
					   &config->pgSetup,
					   missingPgdataIsOk,
					   pgIsNotRunningIsOk))
	{
		return false;
	}

	/*
	 * Keep the whole set of values discovered in pg_setup_init from the
	 * configuration file
	 */
	config->pgSetup = pgSetup;

	return true;
}


/*
 * keeper_config_write_file writes the current values in given KeeperConfig to
 * filename.
 */
bool
keeper_config_write_file(KeeperConfig *config)
{
	const char *filePath = config->pathnames.config;

	log_trace("keeper_config_write_file \"%s\"", filePath);

	FILE *fileStream = fopen_with_umask(filePath, "w", FOPEN_FLAGS_W, 0644);
	if (fileStream == NULL)
	{
		/* errors have already been logged */
		return false;
	}

	bool success = keeper_config_write(fileStream, config);

	if (fclose(fileStream) == EOF)
	{
		log_error("Failed to write file \"%s\"", filePath);
		return false;
	}

	return success;
}


/*
 * keeper_config_write write the current config to given STREAM.
 */
bool
keeper_config_write(FILE *stream, KeeperConfig *config)
{
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return write_ini_to_stream(stream, keeperOptions);
}


/*
 * keeper_config_to_json populates given jsRoot object with the INI
 * configuration sections as JSON objects, and the options as keys to those
 * objects.
 */
bool
keeper_config_to_json(KeeperConfig *config, JSON_Value *js)
{
	JSON_Object *jsRoot = json_value_get_object(js);
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return ini_to_json(jsRoot, keeperOptions);
}


/*
 * keeper_config_log_settings outputs a DEBUG line per each config parameter in
 * the given KeeperConfig.
 */
void
keeper_config_log_settings(KeeperConfig config)
{
	log_debug("pg_autoctl.monitor: %s", config.monitor_pguri);
	log_debug("pg_autoctl.formation: %s", config.formation);

	log_debug("postgresql.hostname: %s", config.hostname);
	log_debug("postgresql.nodekind: %s", config.nodeKind);
	log_debug("postgresql.pgdata: %s", config.pgSetup.pgdata);
	log_debug("postgresql.pg_ctl: %s", config.pgSetup.pg_ctl);
	log_debug("postgresql.version: %s", config.pgSetup.pg_version);
	log_debug("postgresql.username: %s", config.pgSetup.username);
	log_debug("postgresql.dbname: %s", config.pgSetup.dbname);
	log_debug("postgresql.host: %s", config.pgSetup.pghost);
	log_debug("postgresql.port: %d", config.pgSetup.pgport);

	log_debug("replication.replication_password: %s",
			  config.replication_password);
	log_debug("replication.maximum_backup_rate: %s",
			  config.maximum_backup_rate);
}


/*
 * keeper_config_get_setting returns the current value of the given option
 * "path" (thats a section.option string). The value is returned in the
 * pre-allocated value buffer of size size.
 */
bool
keeper_config_get_setting(KeeperConfig *config,
						  const char *path,
						  char *value, size_t size)
{
	const char *filename = config->pathnames.config;
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	return ini_get_setting(filename, keeperOptions, path, value, size);
}


/*
 * keeper_config_set_setting sets the setting identified by "path"
 * (section.option) to the given value. The value is passed in as a string,
 * which is going to be parsed if necessary.
 */
bool
keeper_config_set_setting(KeeperConfig *config,
						  const char *path,
						  char *value)
{
	const char *filename = config->pathnames.config;
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);

	log_trace("keeper_config_set_setting: %s = %s", path, value);

	if (ini_set_setting(filename, keeperOptions, path, value))
	{
		PostgresSetup pgSetup = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		/*
		 * Before merging given options, validate them as much as we can.
		 * The ini level functions validate the syntax (strings, integers,
		 * etc), not that the values themselves then make sense.
		 */
		if (pg_setup_init(&pgSetup,
						  &config->pgSetup,
						  missing_pgdata_is_ok,
						  pg_is_not_running_is_ok))
		{
			config->pgSetup = pgSetup;
			return true;
		}
	}

	return false;
}


/*
 * keeper_config_merge_options merges any option setup in options into config.
 * Its main use is to override configuration file settings with command line
 * options.
 */
bool
keeper_config_merge_options(KeeperConfig *config, KeeperConfig *options)
{
	IniOption keeperConfigOptions[] = SET_INI_OPTIONS_ARRAY(config);
	IniOption keeperOptionsOptions[] = SET_INI_OPTIONS_ARRAY(options);

	log_trace("keeper_config_merge_options");

	if (ini_merge(keeperConfigOptions, keeperOptionsOptions))
	{
		PostgresSetup pgSetup = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		/*
		 * Before merging given options, validate them as much as we can. The
		 * ini level functions validate the syntax (strings, integers, etc),
		 * not that the values themselves then make sense.
		 */
		if (!pg_setup_init(&pgSetup,
						   &config->pgSetup,
						   missing_pgdata_is_ok,
						   pg_is_not_running_is_ok))
		{
			return false;
		}

		/*
		 * Keep the whole set of values discovered in pg_setup_init from the
		 * configuration file
		 */
		config->pgSetup = pgSetup;

		return keeper_config_write_file(config);
	}
	return false;
}


/*
 * keeper_config_update updates the configuration of the keeper once we are
 * registered and know our nodeId and group: then we can also set our
 * replication slot name and our backup directory using the nodeId.
 */
bool
keeper_config_update(KeeperConfig *config, int64_t nodeId, int groupId)
{
	config->groupId = groupId;

	(void) postgres_sprintf_replicationSlotName(
		nodeId,
		config->replication_slot_name,
		sizeof(config->replication_slot_name));

	/*
	 * Compute the backupDirectory from pgdata, or check the one given in the
	 * configuration file already.
	 */
	if (!keeper_config_set_backup_directory(config, nodeId))
	{
		/* errors have already been logged */
		return false;
	}

	log_debug("keeper_config_update: backup directory = %s",
			  config->backupDirectory);

	return keeper_config_write_file(config);
}


/*
 * keeper_config_init_nodekind initializes the config->nodeKind and
 * config->pgSetup.pgKind values from the configuration file or command line
 * options.
 *
 * We didn't implement the PgInstanceKind datatype in our INI primitives, so we
 * need to now to check the configuration values and then transform
 * config->nodeKind into config->pgSetup.pgKind.
 */
static bool
keeper_config_init_nodekind(KeeperConfig *config)
{
	if (IS_EMPTY_STRING_BUFFER(config->nodeKind))
	{
		/*
		 * If the configuration file lacks the pg_autoctl.nodekind key, it
		 * means we're going to use the default: "standalone".
		 */
		strlcpy(config->nodeKind, "standalone", NAMEDATALEN);
		config->pgSetup.pgKind = NODE_KIND_STANDALONE;
	}
	else
	{
		config->pgSetup.pgKind = nodeKindFromString(config->nodeKind);

		/*
		 * Now, NODE_KIND_UNKNOWN signals we failed to recognize selected node
		 * kind, which is an error.
		 */
		if (config->pgSetup.pgKind == NODE_KIND_UNKNOWN)
		{
			/* we already logged about it */
			return false;
		}
	}
	return true;
}


/*
 * keeper_config_init_hbalevel initializes the config->pgSetup.hbaLevel and
 * hbaLevelStr when no command line option switch has been used that places a
 * value (see --auth, --skip-pg-hba, and --pg-hba-lan).
 */
static bool
keeper_config_init_hbalevel(KeeperConfig *config)
{
	/*
	 * Turn the configuration string for hbaLevel into our enum value.
	 */
	if (IS_EMPTY_STRING_BUFFER(config->pgSetup.hbaLevelStr))
	{
		strlcpy(config->pgSetup.hbaLevelStr, "minimal", NAMEDATALEN);
	}

	/* set the ENUM value for hbaLevel */
	config->pgSetup.hbaLevel =
		pgsetup_parse_hba_level(config->pgSetup.hbaLevelStr);

	return true;
}


/*
 * keeper_config_set_backup_directory sets the pg_basebackup target directory
 * to ${PGDATA}/../backup/${hostname} by default. Adding the local hostname
 * makes it possible to run several instances of Postgres and pg_autoctl on the
 * same host, which is nice for development and testing scenarios.
 *
 * That said, when testing and maybe in other situations, it is custom to have
 * all the nodes sit on the same machine, and all be "localhost". To avoid any
 * double-usage of the backup directory, as soon as we have a nodeId we use
 * ${PGDATA/../backup/node_${nodeId} instead.
 */
static bool
keeper_config_set_backup_directory(KeeperConfig *config, int64_t nodeId)
{
	char *pgdata = config->pgSetup.pgdata;
	char subdirs[MAXPGPATH] = { 0 };
	char backupDirectory[MAXPGPATH] = { 0 };
	char absoluteBackupDirectory[PATH_MAX];

	/* build the default hostname based backup directory path */
	sformat(subdirs, MAXPGPATH, "backup/%s", config->hostname);
	path_in_same_directory(pgdata, subdirs, backupDirectory);

	/*
	 * If the user didn't provide a backupDirectory and we're not registered
	 * yet, just use the default value with the hostname. Don't even check it
	 * now.
	 */
	if (IS_EMPTY_STRING_BUFFER(config->backupDirectory) && nodeId <= 0)
	{
		strlcpy(config->backupDirectory, backupDirectory, MAXPGPATH);
		return true;
	}

	/* if we didn't have a backup directory yet, set one */
	if (IS_EMPTY_STRING_BUFFER(config->backupDirectory) ||
		strcmp(backupDirectory, config->backupDirectory) == 0)
	{
		/* we might be able to use the nodeId, better than the hostname */
		if (nodeId > 0)
		{
			sformat(subdirs, MAXPGPATH, "backup/node_%" PRId64, nodeId);
			path_in_same_directory(pgdata, subdirs, backupDirectory);
		}

		strlcpy(config->backupDirectory, backupDirectory, MAXPGPATH);
	}

	/*
	 * The best way to make sure we are allowed to create the backup directory
	 * is to just go ahead and create it now.
	 */
	log_debug("mkdir -p \"%s\"", config->backupDirectory);
	if (!ensure_empty_dir(config->backupDirectory, 0700))
	{
		log_fatal("Failed to create the backup directory \"%s\", "
				  "see above for details", config->backupDirectory);
		return false;
	}

	/* Now get the realpath() of the directory we just created */
	if (!realpath(config->backupDirectory, absoluteBackupDirectory))
	{
		/* non-fatal error, just keep the computed or given directory path */
		log_warn("Failed to get the realpath of backup directory \"%s\": %m",
				 config->backupDirectory);
		return true;
	}

	if (strcmp(config->backupDirectory, absoluteBackupDirectory) != 0)
	{
		strlcpy(config->backupDirectory, absoluteBackupDirectory, MAXPGPATH);
	}

	return true;
}


/*
 * keeper_config_update_with_absolute_pgdata verifies that the pgdata path is
 * an absolute one If not, the config->pgSetup is updated and we rewrite the
 * config file
 */
bool
keeper_config_update_with_absolute_pgdata(KeeperConfig *config)
{
	PostgresSetup pgSetup = config->pgSetup;

	if (pg_setup_set_absolute_pgdata(&pgSetup))
	{
		strlcpy(config->pgSetup.pgdata, pgSetup.pgdata, MAXPGPATH);
		if (!keeper_config_write_file(config))
		{
			/* errors have already been logged */
			return false;
		}
	}
	return true;
}
