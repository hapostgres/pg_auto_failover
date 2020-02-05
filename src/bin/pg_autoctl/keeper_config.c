/*
 * src/bin/pg_autoctl/keeper_config.c
 *     Keeper configuration functions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

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

#define OPTION_AUTOCTL_NODENAME(config) \
	make_strbuf_option("pg_autoctl", "nodename", "nodename", \
					   true, _POSIX_HOST_NAME_MAX, \
					   config->nodename)

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

#define OPTION_POSTGRESQL_PROXY_PORT(config)				\
	make_int_option("postgresql", "proxyport", "proxyport", \
					false, &(config->pgSetup.proxyport))

#define OPTION_POSTGRESQL_LISTEN_ADDRESSES(config) \
	make_strbuf_option("postgresql", "listen_addresses", "listen", \
					   false, MAXPGPATH, config->pgSetup.listen_addresses)

#define OPTION_REPLICATION_SLOT_NAME(config) \
	make_string_option_default("replication", "slot", NULL, false, \
							   &config->replication_slot_name, \
							   REPLICATION_SLOT_NAME_DEFAULT)

#define OPTION_REPLICATION_PASSWORD(config) \
	make_string_option_default("replication", "password", NULL, \
							   false, &config->replication_password, \
							   REPLICATION_PASSWORD_DEFAULT)

#define OPTION_REPLICATION_MAXIMUM_BACKUP_RATE(config) \
	make_string_option_default("replication", "maximum_backup_rate", NULL, \
							   false, &config->maximum_backup_rate, \
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

#define SET_INI_OPTIONS_ARRAY(config) \
	{ \
		OPTION_AUTOCTL_ROLE(config), \
		OPTION_AUTOCTL_MONITOR(config), \
		OPTION_AUTOCTL_FORMATION(config), \
		OPTION_AUTOCTL_GROUPID(config), \
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
		OPTION_REPLICATION_PASSWORD(config), \
		OPTION_REPLICATION_SLOT_NAME(config), \
		OPTION_REPLICATION_MAXIMUM_BACKUP_RATE(config), \
		OPTION_REPLICATION_BACKUP_DIR(config), \
		OPTION_TIMEOUT_NETWORK_PARTITION(config), \
		OPTION_TIMEOUT_PREPARE_PROMOTION_CATCHUP(config), \
		OPTION_TIMEOUT_PREPARE_PROMOTION_WALRECEIVER(config), \
		OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_TIMEOUT(config), \
		OPTION_TIMEOUT_POSTGRESQL_RESTART_FAILURE_MAX_RETRIES(config), \
		INI_OPTION_LAST \
	}

static bool keeper_config_init_nodekind(KeeperConfig *config);
static bool keeper_config_set_backup_directory(KeeperConfig *config, int nodeId);


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

	if (!SetPidFilePath(pathnames, pgdata))
	{
		log_fatal("Failed to set pid filename from PGDATA \"%s\","
				  " see above for details.", pgdata);
		return false;
	}
	return true;
}


/*
 * keeper_config_init initialises a KeeperConfig with the default values.
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
	memcpy(&(config->pgSetup), &pgSetup, sizeof(PostgresSetup));

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

	if (!keeper_config_init_nodekind(config))
	{
		/* errors have already been logged. */
		return false;
	}

	return true;
}


/*
 * keeper_config_read_file_skip_pgsetup overrides values in given KeeperConfig
 * with whatever values are read from given configuration filename.
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
	memcpy(&(config->pgSetup), &pgSetup, sizeof(PostgresSetup));

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
	bool success = false;
	FILE *fileStream = NULL;

	log_trace("keeper_config_write_file \"%s\"", filePath);

	fileStream = fopen(filePath, "w");
	if (fileStream == NULL)
	{
		log_error("Failed to open file \"%s\": %s", filePath, strerror(errno));
		return false;
	}

	success = keeper_config_write(fileStream, config);

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

	log_debug("postgresql.nodename: %s", config.nodename);
	log_debug("postgresql.nodekind: %s", config.nodeKind);
	log_debug("postgresql.pgdata: %s", config.pgSetup.pgdata);
	log_debug("postgresql.pg_ctl: %s", config.pgSetup.pg_ctl);
	log_debug("postgresql.version: %s", config.pgSetup.pg_version);
	log_debug("postgresql.username: %s", config.pgSetup.username);
	log_debug("postgresql.dbname: %s", config.pgSetup.dbname);
	log_debug("postgresql.host: %s", config.pgSetup.pghost);
	log_debug("postgresql.port: %d", config.pgSetup.pgport);

	log_debug("replication.replication_slot_name: %s",
			  config.replication_slot_name);
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

	log_trace("keeper_config_set_setting");

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
			memcpy(&(config->pgSetup), &pgSetup, sizeof(PostgresSetup));
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
		memcpy(&(config->pgSetup), &pgSetup, sizeof(PostgresSetup));

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
keeper_config_update(KeeperConfig *config, int nodeId, int groupId)
{
	IniOption keeperOptions[] = SET_INI_OPTIONS_ARRAY(config);
	char buffer[BUFSIZE] = { 0 };
	char *replicationSlotName = NULL;
	IntString groupIdString = intToString(groupId);

	snprintf(buffer, BUFSIZE, "%s_%d", REPLICATION_SLOT_NAME_DEFAULT, nodeId);
	replicationSlotName = strdup(buffer);

	config->groupId = groupId;
	config->replication_slot_name = replicationSlotName;

	/*
	 * Compute the backupDirectory from pgdata, or check the one given in the
	 * configuration file already.
	 */
	if (!keeper_config_set_backup_directory(config, nodeId))
	{
		/* errors have already been logged */
		return false;
	}

	log_warn("keeper_config_update: backup directory = %s",
			 config->backupDirectory);

	return keeper_config_write_file(config);
}


/*
 * keeper_config_destroy frees memory that may be dynamically allocated.
 */
void
keeper_config_destroy(KeeperConfig *config)
{
	if (config->replication_slot_name != NULL)
	{
		free(config->replication_slot_name);
	}

	if (config->maximum_backup_rate != NULL)
	{
		free(config->maximum_backup_rate);
	}

	if (config->replication_password != NULL)
	{
		free(config->replication_password);
	}
}


/*
 * keeper_config_accept_new returns true when we can accept to RELOAD our
 * current config into the new one that's been editing.
 */
#define strneq(x, y) \
	((x != NULL) && (y != NULL) && ( strcmp(x, y) != 0))

bool
keeper_config_accept_new(KeeperConfig *config, KeeperConfig *newConfig)
{
	/* some elements are not supposed to change on a reload */
	if (strneq(newConfig->pgSetup.pgdata, config->pgSetup.pgdata))
	{
		log_error("Attempt to change postgresql.pgdata from \"%s\" to \"%s\"",
				  config->pgSetup.pgdata, newConfig->pgSetup.pgdata);
		return false;
	}

	if (strneq(newConfig->replication_slot_name, config->replication_slot_name))
	{
		log_error("Attempt to change replication.slot from \"%s\" to \"%s\"",
				  config->replication_slot_name,
				  newConfig->replication_slot_name);
		return false;
	}

	/*
	 * Changing the monitor URI. Well it might just be about using a new IP
	 * address, e.g. switching to IPv6, or maybe the monitor has moved to
	 * another hostname.
	 *
	 * We don't check if we are still registered on the new monitor, only that
	 * we can connect. The node_active calls are going to fail it we then
	 * aren't registered anymore.
	 */
	if (strneq(newConfig->monitor_pguri, config->monitor_pguri))
	{
		Monitor monitor = { 0 };

		if (!monitor_init(&monitor, newConfig->monitor_pguri))
		{
			log_fatal("Failed to contact the monitor because its URL is invalid, "
					  "see above for details");
			return false;
		}

		strlcpy(config->monitor_pguri, newConfig->monitor_pguri, MAXCONNINFO);
	}

	/*
	 * We don't support changing formation, group, or nodename mid-flight: we
	 * might have to register again to the monitor to make that work, and in
	 * that case an admin should certainly be doing some offline steps, maybe
	 * even having to `pg_autoctl create` all over again.
	 */
	if (strneq(newConfig->formation, config->formation))
	{
		log_warn("pg_autoctl doesn't know how to change formation at run-time, "
				 "continuing with formation \"%s\".",
				 config->formation);
	}

	/*
	 * Changing the nodename seems ok, our registration is checked against
	 * formation/groupId/nodeId anyway. The nodename is used so that other
	 * nodes in the network may contact us. Again, it might be a change of
	 * public IP address, e.g. switching to IPv6.
	 */
	if (strneq(newConfig->nodename, config->nodename))
	{
		log_info("Reloading configuration: nodename is now \"%s\"; "
				 "used to be \"%s\"",
				 newConfig->nodename, config->nodename);
		strlcpy(config->nodename, newConfig->nodename, _POSIX_HOST_NAME_MAX);
	}

	/*
	 * Changing the replication password? Sure.
	 */
	if (strneq(newConfig->replication_password, config->replication_password))
	{
		log_info("Reloading configuration: replication password has changed");

		/* note: strneq checks args are not NULL, it's safe to proceed */
		free(config->replication_password);
		config->replication_password = strdup(newConfig->replication_password);
	}

	/*
	 * Changing replication.maximum_backup_rate.
	 */
	if (strneq(newConfig->maximum_backup_rate, config->maximum_backup_rate))
	{
		log_info("Reloading configuration: "
				 "replication.maximum_backup_rate is now \"%s\"; "
				 "used to be \"%s\"" ,
				 newConfig->maximum_backup_rate, config->maximum_backup_rate);

		/* note: strneq checks args are not NULL, it's safe to proceed */
		free(config->maximum_backup_rate);
		config->maximum_backup_rate = strdup(newConfig->maximum_backup_rate);
	}

	/*
	 * And now the timeouts. Of course we support changing them at run-time.
	 */
	if (newConfig->network_partition_timeout
		!= config->network_partition_timeout)
	{
		log_info("Reloading configuration: timeout.network_partition_timeout "
				 "is now %d; used to be %d",
				 newConfig->network_partition_timeout,
				 config->network_partition_timeout);

		config->network_partition_timeout =
			newConfig->network_partition_timeout;
	}

	if (newConfig->prepare_promotion_catchup
		!= config->prepare_promotion_catchup)
	{
		log_info("Reloading configuration: timeout.prepare_promotion_catchup "
				 "is now %d; used to be %d",
				 newConfig->prepare_promotion_catchup,
				 config->prepare_promotion_catchup);

		config->prepare_promotion_catchup =
			newConfig->prepare_promotion_catchup;
	}

	if (newConfig->prepare_promotion_walreceiver
		!= config->prepare_promotion_walreceiver)
	{
		log_info(
			"Reloading configuration: timeout.prepare_promotion_walreceiver "
			"is now %d; used to be %d",
			newConfig->prepare_promotion_walreceiver,
			config->prepare_promotion_walreceiver);

		config->prepare_promotion_walreceiver =
			newConfig->prepare_promotion_walreceiver;
	}

	if (newConfig->postgresql_restart_failure_timeout
		!= config->postgresql_restart_failure_timeout)
	{
		log_info(
			"Reloading configuration: timeout.postgresql_restart_failure_timeout "
			"is now %d; used to be %d",
			newConfig->postgresql_restart_failure_timeout,
			config->postgresql_restart_failure_timeout);

		config->postgresql_restart_failure_timeout =
			newConfig->postgresql_restart_failure_timeout;
	}

	if (newConfig->postgresql_restart_failure_max_retries
		!= config->postgresql_restart_failure_max_retries)
	{
		log_info(
			"Reloading configuration: retries.postgresql_restart_failure_max_retries "
			"is now %d; used to be %d",
			newConfig->postgresql_restart_failure_max_retries,
			config->postgresql_restart_failure_max_retries);

		config->postgresql_restart_failure_max_retries =
			newConfig->postgresql_restart_failure_max_retries;
	}

	return true;
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
 * keeper_config_set_backup_directory sets the pg_basebackup target directory
 * to ${PGDATA}/../backup/${nodename} by default. Adding the local nodename
 * makes it possible to run several instances of Postgres and pg_autoctl on the
 * same host, which is nice for development and testing scenarios.
 *
 * That said, when testing and maybe in other situations, it is custom to have
 * all the nodes sit on the same machine, and all be "localhost". To avoid any
 * double-usage of the backup directory, as soon as we have a nodeId we use
 * ${PGDATA/../backup/node_${nodeId} instead.
 */
static bool
keeper_config_set_backup_directory(KeeperConfig *config, int nodeId)
{
	char *pgdata = config->pgSetup.pgdata;
	char subdirs[MAXPGPATH] = { 0 };
	char backupDirectory[MAXPGPATH] = { 0 };
	char absoluteBackupDirectory[PATH_MAX];

	/* build the default nodename based backup directory path */
	snprintf(subdirs, MAXPGPATH, "backup/%s", config->nodename);
	path_in_same_directory(pgdata, subdirs, backupDirectory);

	/*
	 * If the user didn't provide a backupDirectory and we're not registered
	 * yet, just use the default value with the nodename. Don't even check it
	 * now.
	 */
	if (IS_EMPTY_STRING_BUFFER(config->backupDirectory) && nodeId <= 0)
	{
		strlcpy(config->backupDirectory, backupDirectory, MAXPGPATH);
		return true;
	}

	/* if we didn't have a backup directory yet, set one */
	if (IS_EMPTY_STRING_BUFFER(config->backupDirectory)
		|| strcmp(backupDirectory, config->backupDirectory) == 0)
	{
		/* we might be able to use the nodeId, better than the nodename */
		if (nodeId > 0)
		{
			snprintf(subdirs, MAXPGPATH, "backup/node_%d", nodeId);
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
		log_warn("Failed to get the realpath of backup directory \"%s\": %s",
				 config->backupDirectory, strerror(errno));
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
