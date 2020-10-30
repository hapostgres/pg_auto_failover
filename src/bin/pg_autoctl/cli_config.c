/*
 * src/bin/pg_autoctl/cli_config.c
 *     Implementation of pg_autoctl config CLI sub-commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "ini_file.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pidfile.h"


static void cli_config_check(int argc, char **argv);
static void cli_config_check_pgsetup(PostgresSetup *pgSetup);
static void cli_config_check_connections(PostgresSetup *pgSetup,
										 const char *monitor_pguri);

static void cli_config_get(int argc, char **argv);
static void cli_keeper_config_get(int argc, char **argv);
static void cli_monitor_config_get(int argc, char **argv);

static void cli_config_set(int argc, char **argv);
static void cli_keeper_config_set(int argc, char **argv);
static bool cli_keeper_config_validate_and_commit(KeeperConfig *config,
												  const char *key,
												  char *value);
static bool cli_keeper_config_validate_and_commit_replication_primary(Keeper *keeper);
static bool cli_keeper_config_validate_replication_secondary(Keeper *keeper,
															 NodeAddress *primary);
static void cli_monitor_config_set(int argc, char **argv);

static CommandLine config_check =
	make_command("check",
				 "Check pg_autoctl configuration",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_config_check);

static CommandLine config_get =
	make_command("get",
				 "Get the value of a given pg_autoctl configuration variable",
				 CLI_PGDATA_USAGE "[ section.option ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_config_get);

static CommandLine config_set =
	make_command("set",
				 "Set the value of a given pg_autoctl configuration variable",
				 CLI_PGDATA_USAGE "section.option [ value ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_config_set);

static CommandLine *config[] = {
	&config_check,
	&config_get,
	&config_set,
	NULL
};

CommandLine config_commands =
	make_command_set("config",
					 "Manages the pg_autoctl configuration", NULL, NULL,
					 NULL, config);


/*
 * cli_config_check reads a configuration file and debug its content as
 * DEBUG messages.
 */
static void
cli_config_check(int argc, char **argv)
{
	const bool missingPgdataIsOk = true;
	const bool pgIsNotRunningIsOk = true;
	const bool monitorDisabledIsOk = true;

	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
												 config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			bool missingPgDataIsOk = true;
			MonitorConfig mconfig = { 0 };

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &config.pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!pg_controldata(&(mconfig.pgSetup), missingPgDataIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_PGCTL);
			}

			(void) cli_config_check_pgsetup(&(mconfig.pgSetup));
			(void) cli_config_check_connections(&(mconfig.pgSetup), NULL);

			if (outputJSON)
			{
				JSON_Value *js = json_value_init_object();
				JSON_Value *jsPostgres = json_value_init_object();
				JSON_Value *jsMConfig = json_value_init_object();

				JSON_Object *root = json_value_get_object(js);

				/* prepare both JSON objects */
				if (!pg_setup_as_json(&(mconfig.pgSetup), jsPostgres))
				{
					/* can't happen */
					exit(EXIT_CODE_INTERNAL_ERROR);
				}

				if (!monitor_config_to_json(&mconfig, jsMConfig))
				{
					log_fatal("Failed to serialize monitor configuration to JSON");
					exit(EXIT_CODE_BAD_CONFIG);
				}

				/* concatenate JSON objects into a container object */
				json_object_set_value(root, "postgres", jsPostgres);
				json_object_set_value(root, "config", jsMConfig);

				(void) cli_pprint_json(js);
			}
			else
			{
				fprintf_pg_setup(stdout, &(mconfig.pgSetup));
			}

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool missingPgDataIsOk = true;

			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (!pg_controldata(&(config.pgSetup), missingPgDataIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_PGCTL);
			}

			(void) cli_config_check_pgsetup(&(config.pgSetup));
			(void) cli_config_check_connections(
				&(config.pgSetup),
				config.monitorDisabled ? NULL : config.monitor_pguri);

			if (outputJSON)
			{
				JSON_Value *js = json_value_init_object();
				JSON_Value *jsPostgres = json_value_init_object();
				JSON_Value *jsKConfig = json_value_init_object();

				JSON_Object *root = json_value_get_object(js);

				/* prepare both JSON objects */
				if (!pg_setup_as_json(&(config.pgSetup), jsPostgres))
				{
					/* can't happen */
					exit(EXIT_CODE_INTERNAL_ERROR);
				}

				if (!keeper_config_to_json(&config, jsKConfig))
				{
					log_fatal("Failed to serialize monitor configuration to JSON");
					exit(EXIT_CODE_BAD_CONFIG);
				}

				/* concatenate JSON objects into a container object */
				json_object_set_value(root, "postgres", jsPostgres);
				json_object_set_value(root, "config", jsKConfig);

				(void) cli_pprint_json(js);
			}
			else
			{
				fprintf_pg_setup(stdout, &(config.pgSetup));
			}

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	keeper_config_destroy(&config);
}


/*
 * cli_keeper_config_check checks a keeper configuration file.
 */
static void
cli_config_check_pgsetup(PostgresSetup *pgSetup)
{
	int errors = 0;

	/*
	 * Now check for errors. Rather than using the generic missing_pgdata_is_ok
	 * and pg_not_running_is_ok facility, we do our own error checking here.
	 * One reason is that this command line doesn't provide support for
	 * --pgport and other options, on purpose. Another reason is that we want
	 * to check for everything rather than fail fast.
	 */
	char globalControlPath[MAXPGPATH] = { 0 };

	/* globalControlFilePath = $PGDATA/global/pg_control */
	join_path_components(globalControlPath, pgSetup->pgdata, "global/pg_control");

	if (!file_exists(globalControlPath))
	{
		errors++;
		log_error("postgresql.pgdata does not belong to a PostgreSQL cluster: "
				  "\"%s\"", pgSetup->pgdata);
	}

	/* when PostgreSQL is running, pg_setup_init() has connected to it. */
	if (pgSetup->pidFile.pid == 0)
	{
		errors++;
		log_error("PostgreSQL is not running");
	}

	/* TODO: check formation, group, hostname on the monitor */

	if (errors > 0)
	{
		exit(EXIT_CODE_BAD_CONFIG);
	}

	log_info("Postgres setup for PGDATA \"%s\" is ok, "
			 "running with PID %d and port %d",
			 pgSetup->pgdata, pgSetup->pidFile.port, pgSetup->pidFile.pid);
}


/*
 * cli_config_check_connections checks that the following three connections are
 * possible:
 *
 *  1. connection to the local Postgres server
 *  2. connection to the Postgres monitor
 *  3. streaming replication connection string
 */
static void
cli_config_check_connections(PostgresSetup *pgSetup,
							 const char *monitor_pguri)
{
	PGSQL pgsql = { 0 };
	char connInfo[MAXCONNINFO] = { 0 };

	bool settings_are_ok = false;

	Monitor monitor = { 0 };
	MonitorExtensionVersion version = { 0 };

	pg_setup_get_local_connection_string(pgSetup, connInfo);
	pgsql_init(&pgsql, connInfo, PGSQL_CONN_LOCAL);

	if (!pgsql_is_in_recovery(&pgsql, &pgSetup->is_in_recovery))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGSQL);
	}

	log_info("Connection to local Postgres ok, using \"%s\"", connInfo);

	/*
	 * Do not check settings on the monitor node itself. On the monitor, we
	 * don't have a monitor_pguri in the config.
	 */
	if (monitor_pguri == NULL)
	{
		return;
	}

	/*
	 * Check that the Postgres settings for pg_auto_failover are active in the
	 * running Postgres instance.
	 */
	if (!pgsql_check_postgresql_settings(&pgsql, false, &settings_are_ok))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_PGSQL);
	}

	if (settings_are_ok)
	{
		log_info("Postgres configuration settings required "
				 "for pg_auto_failover are ok");
	}
	else
	{
		log_warn("Failed to check required settings for pg_auto_failover, "
				 "please review your Postgres configuration");
	}

	if (pg_setup_standby_slot_supported(pgSetup, LOG_WARN))
	{
		int major = pgSetup->control.pg_control_version / 100;
		int minor = pgSetup->control.pg_control_version % 100;

		log_info("Postgres version %d.%d allows using replication slots "
				 "on the standby nodes", major, minor);
	}

	/*
	 * Now, on Postgres nodes, check that the monitor uri is valid and that we
	 * can connect to the monitor just fine. This requires having setup the
	 * Postgres HBA rules correctly, which is up to the user when using
	 * --skip-pg-hba.
	 */
	if (!monitor_init(&monitor, (char *) monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_get_extension_version(&monitor, &version))
	{
		log_fatal("Failed to check version compatibility with the monitor "
				  "extension \"%s\", see above for details",
				  PG_AUTOCTL_MONITOR_EXTENSION_NAME);
		exit(EXIT_CODE_MONITOR);
	}

	/* disconnect from the monitor now */
	pgsql_finish(&(monitor.pgsql));

	log_info("Connection to monitor ok, using \"%s\"", monitor_pguri);

	if (strcmp(version.installedVersion, PG_AUTOCTL_EXTENSION_VERSION) == 0)
	{
		log_info("Monitor is running version \"%s\", as expected",
				 version.installedVersion);
	}
	else
	{
		log_info("Monitor is running version \"%s\" "
				 "instead of expected version \"%s\"",
				 version.installedVersion, PG_AUTOCTL_EXTENSION_VERSION);
		log_warn("Please connect to the monitor node and restart pg_autoctl.");
	}

	/* TODO: check streaming replication connections */
}


/*
 * cli_keeper_config_get retrieves the value of a given configuration value,
 * supporting either a Keeper or a Monitor configuration file.
 */
static void
cli_config_get(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
												 config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			(void) cli_monitor_config_get(argc, argv);
			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			(void) cli_keeper_config_get(argc, argv);
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * keeper_cli_config_get returns the value of a given section.option, or prints
 * out the whole file to stdout when no argument has been given.
 */
static void
cli_keeper_config_get(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	switch (argc)
	{
		case 0:
		{
			/* no argument, write the config out */
			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				exit(EXIT_CODE_PGCTL);
			}
			else
			{
				if (outputJSON)
				{
					JSON_Value *js = json_value_init_object();

					if (!keeper_config_to_json(&config, js))
					{
						log_fatal("Failed to serialize configuration to JSON");
						exit(EXIT_CODE_BAD_CONFIG);
					}

					(void) cli_pprint_json(js);
				}
				else
				{
					keeper_config_write(stdout, &config);
					fformat(stdout, "\n");
				}
			}

			keeper_config_destroy(&config);
			break;
		}

		case 1:
		{
			/* single argument, find the option and display its value */
			char *path = argv[0];
			char value[BUFSIZE];

			if (keeper_config_get_setting(&config,
										  path,
										  value,
										  BUFSIZE))
			{
				fformat(stdout, "%s\n", value);
			}
			else
			{
				log_error("Failed to lookup option %s", path);
				exit(EXIT_CODE_BAD_ARGS);
			}

			keeper_config_destroy(&config);
			break;
		}

		default:
		{
			/* we only support 0 or 1 argument */
			commandline_help(stderr);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}


/*
 * keeper_cli_config_get returns the value of a given section.option, or prints
 * out the whole file to stdout when no argument has been given.
 */
static void
cli_monitor_config_get(int argc, char **argv)
{
	MonitorConfig mconfig = { 0 };
	KeeperConfig kconfig = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	if (!monitor_config_init_from_pgsetup(&mconfig,
										  &kconfig.pgSetup,
										  missing_pgdata_is_ok,
										  pg_is_not_running_is_ok))
	{
		exit(EXIT_CODE_PGCTL);
	}

	switch (argc)
	{
		case 0:
		{
			if (outputJSON)
			{
				JSON_Value *js = json_value_init_object();

				if (!monitor_config_to_json(&mconfig, js))
				{
					log_fatal("Failed to serialize configuration to JSON");
					exit(EXIT_CODE_BAD_CONFIG);
				}

				(void) cli_pprint_json(js);
			}
			else
			{
				monitor_config_write(stdout, &mconfig);
				fformat(stdout, "\n");
			}

			keeper_config_destroy(&kconfig);
			break;
		}

		case 1:
		{
			/* single argument, find the option and display its value */
			char *path = argv[0];
			char value[BUFSIZE];

			if (monitor_config_get_setting(&mconfig,
										   path,
										   value,
										   BUFSIZE))
			{
				fformat(stdout, "%s\n", value);
			}
			else
			{
				log_error("Failed to lookup option %s", path);
				exit(EXIT_CODE_BAD_ARGS);
			}

			keeper_config_destroy(&kconfig);
			break;
		}

		default:
		{
			/* we only support 0 or 1 argument */
			commandline_help(stderr);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}


/*
 * cli_keeper_config_get retrieves the value of a given configuration value,
 * supporting either a Keeper or a Monitor configuration file.
 */
static void
cli_config_set(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&config.pathnames,
												 config.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			(void) cli_monitor_config_set(argc, argv);
			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			(void) cli_keeper_config_set(argc, argv);
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}

	if (!cli_pg_autoctl_reload(config.pathnames.pid))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_keeper_config_validate_and_commit_replication_primary checks that ALTER
 * USER PASSWORD succeeds on a primary with the current replication
 * configuration, then writes the configuration to disk. If the configuration
 * write fails, the ALTER USER will be rolled back. If the primary is not
 * running, validation is skipped (and this function returns true).
 */
static bool
cli_keeper_config_validate_and_commit_replication_primary(Keeper *keeper)
{
	KeeperConfig *config = &keeper->config;
	PGSQL *pgsql = &keeper->postgres.sqlClient;
	bool success = false;


	/* Skip validation if the primary isn't running. */
	if (!pg_is_running(config->pgSetup.pg_ctl, config->pgSetup.pgdata))
	{
		log_warn("Failed to connect to the primary, skipping password "
				 "validation");

		if (!keeper_config_write_file(config))
		{
			log_error("Failed to write pg_autoctl configuration file \"%s\"",
					  config->pathnames.config);
			return false;
		}

		return true;
	}

	/*
	 * Validate the replication password in a transaction block by setting
	 * the password using ALTER USER PASSWORD. The transaction block will
	 * encapsulate the subsequent change to the config file as well and we
	 * ROLLBACK/COMMIT depending on whether both the ALTER and the config
	 * file update are successful.
	 */
	if (!pgsql_execute(pgsql, "BEGIN"))
	{
		log_error("Failed to open a SQL transaction to update the "
				  "replication password");
		goto finish;
	}

	if (!pgsql_set_password(pgsql,
							PG_AUTOCTL_REPLICA_USERNAME,
							config->replication_password))
	{
		log_error("Failed to set replication password, see above for details");
		goto finish;
	}

	/* write the new configuration settings to file */
	if (!keeper_config_write_file(config))
	{
		log_error("Failed to write pg_autoctl configuration file \"%s\"",
				  config->pathnames.config);
		goto finish;
	}

	if (!pgsql_execute(pgsql, "COMMIT"))
	{
		log_error("Failed to COMMIT replication password update transaction "
				  "on the primary, see above for details.");
		goto finish;
	}

	success = true;

finish:
	pgsql_finish(pgsql); /* will implicitly ROLLBACK in failure cases */
	return success;
}


/*
 * cli_keeper_config_validate_replication_secondary checks that the current
 * replication configuration can be used to connect to the primary. If the
 * primary cannot be contacted, validation is skipped (and this function
 * returns true).
 */
static bool
cli_keeper_config_validate_replication_secondary(Keeper *keeper, NodeAddress *primary)
{
	KeeperConfig *config = &keeper->config;
	bool primaryUnreachable;

	(void) postgres_sprintf_replicationSlotName(
		primary->nodeId,
		config->replication_slot_name,
		sizeof(config->replication_slot_name));
	if (!standby_init_replication_source(&keeper->postgres,
										 primary,
										 PG_AUTOCTL_REPLICA_USERNAME,
										 config->replication_password,
										 config->replication_slot_name,
										 config->maximum_backup_rate,
										 config->backupDirectory,
										 NULL, /* no targetLSN */
										 config->pgSetup.ssl,
										 keeper->state.current_node_id))
	{
		/* can't happen at the moment */
		return false;
	}

	if (!pgctl_identify_system(&keeper->postgres.replicationSource, &primaryUnreachable))
	{
		if (primaryUnreachable)
		{
			log_warn("Primary could not be contacted, skipping password "
					 "validation");
			return true;
		}

		log_error("Could not connect to primary with the new replication "
				  "password. See above for details.");
		return false;
	}

	return true;
}


/*
 * cli_keeper_config_validate_and_commit runs any required validation and writes
 * the updated configuration to disk.
 */
static bool
cli_keeper_config_validate_and_commit(KeeperConfig *config, const char *key, char *value)
{
	Keeper keeper = { 0 };
	NodeAddress primary = { 0 };

	if (!keeper_init(&keeper, config))
	{
		/* already logged */
		return false;
	}

	/* get the primary info from the monitor */
	if (!monitor_get_primary(&keeper.monitor,
							 config->formation,
							 keeper.state.current_group,
							 &primary))
	{
		log_warn("Failed to contact monitor to get primary conninfo, skipping "
				 "replication.password validation");
		goto write;
	}

	/*
	 * We can check if we are validating against a primary by comparing our
	 * local nodeId with the nodeId from the primary info obtained from the
	 * monitor.
	 */
	if (keeper.state.current_node_id == primary.nodeId)
	{
		/*
		 * The primary case handles the configuration write itself. Return
		 * directly.
		 */
		return cli_keeper_config_validate_and_commit_replication_primary(&keeper);
	}

	if (!cli_keeper_config_validate_replication_secondary(&keeper, &primary))
	{
		/* already logged */
		return false;
	}

write:
	if (!keeper_config_write_file(config))
	{
		log_error("Failed to write pg_autoctl configuration file \"%s\"",
				  config->pathnames.config);
		return false;
	}

	return true;
}


/*
 * cli_keeper_config_set sets the given option path to the given value.
 */
static void
cli_keeper_config_set(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;

	if (argc != 2)
	{
		log_error("Two arguments are expected, found %d", argc);
		exit(EXIT_CODE_BAD_ARGS);
	}
	else
	{
		/* we print out the value that we parsed, as a double-check */
		char value[BUFSIZE];

		if (!keeper_config_set_setting(&config,
									   argv[0],
									   argv[1]))
		{
			/* we already logged about it */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* first write the new configuration settings to file */
		if (keeper_config_setting_requires_validation(argv[0]))
		{
			if (!cli_keeper_config_validate_and_commit(&config,
													   argv[0],
													   argv[1]))
			{
				/* we already logged about it */
				exit(EXIT_CODE_BAD_CONFIG);
			}
		}
		else if (!keeper_config_write_file(&config))
		{
			log_fatal("Failed to write pg_autoctl configuration file \"%s\", "
					  "see above for details",
					  config.pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* now read the value from just written file */
		if (keeper_config_get_setting(&config,
									  argv[0],
									  value,
									  BUFSIZE))
		{
			fformat(stdout, "%s\n", value);
		}
		else
		{
			log_error("Failed to lookup option %s", argv[0]);
			exit(EXIT_CODE_BAD_ARGS);
		}

		keeper_config_destroy(&config);
	}
}


/*
 * cli_monitor_config_set sets the given option path to the given value.
 */
static void
cli_monitor_config_set(int argc, char **argv)
{
	KeeperConfig kconfig = keeperOptions;

	if (argc != 2)
	{
		log_error("Two arguments are expected, found %d", argc);
		exit(EXIT_CODE_BAD_ARGS);
	}
	else
	{
		/* we print out the value that we parsed, as a double-check */
		char value[BUFSIZE];
		MonitorConfig mconfig = { 0 };
		bool missing_pgdata_is_ok = true;
		bool pg_is_not_running_is_ok = true;

		if (!monitor_config_init_from_pgsetup(&mconfig,
											  &kconfig.pgSetup,
											  missing_pgdata_is_ok,
											  pg_is_not_running_is_ok))
		{
			exit(EXIT_CODE_PGCTL);
		}

		/* first write the new configuration settings to file */
		if (!monitor_config_set_setting(&mconfig, argv[0], argv[1]))
		{
			/* we already logged about it */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		if (!monitor_config_write_file(&mconfig))
		{
			log_fatal("Failed to write the monitor's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/* now read the value from just written file */
		if (monitor_config_get_setting(&mconfig,
									   argv[0],
									   value,
									   BUFSIZE))
		{
			fformat(stdout, "%s\n", value);
		}
		else
		{
			log_error("Failed to lookup option %s", argv[0]);
			exit(EXIT_CODE_BAD_ARGS);
		}

		keeper_config_destroy(&kconfig);
	}
}
