/*
 * cli_enable_disable.c
 *     Implementation of pg_autoctl enable and disable CLI sub-commands.
 *     Current features that can be enabled and their scope are:
 *      - secondary (scope: formation)
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "fsm.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "parsing.h"
#include "pgsetup.h"


static int cli_secondary_getopts(int argc, char **argv);
static void cli_enable_secondary(int argc, char **argv);
static void cli_disable_secondary(int argc, char **argv);

static void cli_enable_maintenance(int argc, char **argv);
static void cli_disable_maintenance(int argc, char **argv);

static int cli_ssl_getopts(int argc, char **argv);
static void cli_enable_ssl(int argc, char **argv);
static void cli_disable_ssl(int argc, char **argv);

static bool update_ssl_configuration(LocalPostgresServer *postgres,
									 const char *hostname);

static bool update_monitor_connection_string(KeeperConfig *config);


static CommandLine enable_secondary_command =
	make_command("secondary",
				 "Enable secondary nodes on a formation",
				 " [ --pgdata --formation ] ",
				 "  --pgdata      path to data directory\n" \
				 "  --formation   Formation to enable secondary on\n",
				 cli_secondary_getopts,
				 cli_enable_secondary);

static CommandLine disable_secondary_command =
	make_command("secondary",
				 "Disable secondary nodes on a formation",
				 " [ --pgdata --formation ] ",
				 "  --pgdata      path to data directory\n" \
				 "  --formation   Formation to disable secondary on\n",
				 cli_secondary_getopts,
				 cli_disable_secondary);

static CommandLine enable_maintenance_command =
	make_command("maintenance",
				 "Enable Postgres maintenance mode on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_enable_maintenance);

static CommandLine disable_maintenance_command =
	make_command("maintenance",
				 "Disable Postgres maintenance mode on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_disable_maintenance);

static CommandLine enable_ssl_command =
	make_command("ssl",
				 "Enable SSL configuration on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION
				 KEEPER_CLI_SSL_OPTIONS,
				 cli_ssl_getopts,
				 cli_enable_ssl);

static CommandLine disable_ssl_command =
	make_command("ssl",
				 "Disable SSL configuration on this node",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_disable_ssl);

static CommandLine *enable_subcommands[] = {
	&enable_secondary_command,
	&enable_maintenance_command,
	&enable_ssl_command,
	NULL
};

static CommandLine *disable_subcommands[] = {
	&disable_secondary_command,
	&disable_maintenance_command,
	&disable_ssl_command,
	NULL
};


CommandLine enable_commands =
	make_command_set("enable",
					 "Enable a feature on a formation", NULL, NULL,
					 NULL, enable_subcommands);

CommandLine disable_commands =
	make_command_set("disable",
					 "Disable a feature on a formation", NULL, NULL,
					 NULL, disable_subcommands);


/*
 * cli_secondary_getopts parses command line options for the secondary feature,
 * both during enable and disable. Little verification is performed however the
 * function will error when no --pgdata or --formation are provided, existance
 * of either are not verified.
 */
static int
cli_secondary_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "formation", required_argument, NULL, 'f' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		get_env_pgdata_or_exit(options.pgSetup.pgdata);
	}

	if (IS_EMPTY_STRING_BUFFER(options.formation))
	{
		log_error("Option --formation is mandatory");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_enable_secondary enables secondaries on the specified formation.
 */
static void
cli_enable_secondary(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_enable_secondary_for_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Enabled secondaries for formation \"%s\", make sure to add "
			 "worker nodes to the formation to have secondaries ready "
			 "for failover.",
			 config.formation);
}


/*
 * cli_disable_secondary disables secondaries on the specified formation.
 */
static void
cli_disable_secondary(int argc, char **argv)
{
	KeeperConfig config = keeperOptions;
	Monitor monitor = { 0 };

	if (!monitor_init_from_pgsetup(&monitor, &config.pgSetup))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * disabling secondaries on a formation happens on the monitor. When the
	 * formation is still operating with secondaries an error will be logged
	 * and the function will return with a false value. As we will exit the
	 * successful info message below is only printed if secondaries on the
	 * formation have been disabled successfully.
	 */
	if (!monitor_disable_secondary_for_formation(&monitor, config.formation))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	log_info("Disabled secondaries for formation \"%s\".", config.formation);
}


/*
 * cli_enable_maintenance calls the pgautofailover.start_maintenance() function
 * on the monitor for the local node.
 */
static void
cli_enable_maintenance(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	char *channels[] = { "state", NULL };

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!pgsql_listen(&(keeper.monitor.pgsql), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_start_maintenance(&(keeper.monitor),
								   keeper.config.formation,
								   keeper.config.nodename))
	{
		log_fatal("Failed to start maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_wait_until_node_reported_state(
			&(keeper.monitor),
			keeper.state.current_node_id,
			MAINTENANCE_STATE))
	{
		log_error("Failed to wait until the new setting has been applied");
		exit(EXIT_CODE_MONITOR);
	}
}


/*
 * cli_disable_maintenance calls pgautofailver.stop_maintenance(name, port) on
 * the monitor.
 */
static void
cli_disable_maintenance(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	char *channels[] = { "state", NULL };

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!pgsql_listen(&(keeper.monitor.pgsql), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_stop_maintenance(&(keeper.monitor),
								  keeper.config.formation,
								  keeper.config.nodename))
	{
		log_fatal("Failed to stop maintenance from the monitor, "
				  "see above for details");
		exit(EXIT_CODE_MONITOR);
	}

	if (!monitor_wait_until_node_reported_state(
			&(keeper.monitor),
			keeper.state.current_node_id,
			CATCHINGUP_STATE))
	{
		log_error("Failed to wait until the new setting has been applied");
		exit(EXIT_CODE_MONITOR);
	}
}


/*
 * cli_ssl_getopts parses the command line options necessary to initialise a
 * PostgreSQL instance as our monitor.
 */
static int
cli_ssl_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	SSLCommandLineOptions sslCommandLineOptions = SSL_CLI_UNKNOWN;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ "no-ssl", no_argument, NULL, 'N' },
		{ "ssl-self-signed", no_argument, NULL, 's' },
		{ "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
		{ "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG },
		{ "ssl-crl-file", required_argument, &ssl_flag, SSL_CRL_FILE_FLAG },
		{ "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG },
		{ "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG },
		{ NULL, 0, NULL, 0 }
	};

	/* hard-coded defaults */
	options.pgSetup.pgport = pgsetup_get_pgport();

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:VvqhNs",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				keeper_cli_print_version(argc, argv);
				break;
			}

			case 'v':
			{
				++verboseCount;
				switch (verboseCount)
				{
					case 1:
					{
						log_set_level(LOG_INFO);
						break;
					}

					case 2:
					{
						log_set_level(LOG_DEBUG);
						break;
					}

					default:
					{
						log_set_level(LOG_TRACE);
						break;
					}
				}
				break;
			}

			case 'q':
			{
				log_set_level(LOG_ERROR);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			case 's':
			{
				/* { "ssl-self-signed", no_argument, NULL, 's' }, */
				if (!cli_getopt_accept_ssl_options(SSL_CLI_SELF_SIGNED,
												   sslCommandLineOptions))
				{
					errors++;
					break;
				}
				sslCommandLineOptions = SSL_CLI_SELF_SIGNED;

				options.pgSetup.ssl.active = 1;
				options.pgSetup.ssl.createSelfSignedCert = true;
				log_trace("--ssl-self-signed");
				break;
			}

			case 'N':
			{
				/* { "no-ssl", no_argument, NULL, 'N' }, */
				if (!cli_getopt_accept_ssl_options(SSL_CLI_NO_SSL,
												   sslCommandLineOptions))
				{
					errors++;
					break;
				}
				sslCommandLineOptions = SSL_CLI_NO_SSL;

				options.pgSetup.ssl.active = 0;
				options.pgSetup.ssl.createSelfSignedCert = false;
				log_trace("--no-ssl");
				break;
			}

			/*
			 * { "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG }
			 * { "ssl-crl-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG }
			 * { "server-crt", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG }
			 * { "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG }
			 * { "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
			 */
			case 0:
			{
				if (ssl_flag != SSL_MODE_FLAG)
				{
					if (!cli_getopt_accept_ssl_options(SSL_CLI_USER_PROVIDED,
													   sslCommandLineOptions))
					{
						errors++;
						break;
					}

					sslCommandLineOptions = SSL_CLI_USER_PROVIDED;
					options.pgSetup.ssl.active = 1;
				}

				if (!cli_getopt_ssl_flags(ssl_flag, optarg, &(options.pgSetup)))
				{
					errors++;
				}
				break;
			}

			default:
			{
				/* getopt_long already wrote an error message */
				commandline_help(stderr);
				exit(EXIT_CODE_BAD_ARGS);
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* Initialize with given PGDATA */
	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		get_env_pgdata_or_exit(options.pgSetup.pgdata);
	}

	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * If any --ssl-* option is provided, either we have a root ca file and a
	 * server.key and a server.crt or none of them. Any other combo is a
	 * mistake.
	 */
	if (sslCommandLineOptions == SSL_CLI_UNKNOWN)
	{
		log_fatal("Explicit SSL choice is required: please use either "
				  "--ssl-self-signed or provide your certificates "
				  "using --ssl-ca-file, --ssl-crl-file, "
				  "--server-key, and --server-crt (or use --no-ssl if you "
				  "are very sure that you do not want encrypted traffic)");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!pgsetup_validate_ssl_settings(&(options.pgSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_enable_ssl enables SSL setup on this node.
 *
 *  - edit our Postgres configuration with the given SSL files and options
 *  - when run on a keeper, edit the monitor connection string to use SSL
 *  - edits our configuration at pg_autoctl.conf
 */
static void
cli_enable_ssl(int argc, char **argv)
{
	KeeperConfig options = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	switch (ProbeConfigurationFileRole(options.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			MonitorConfig mconfig = { 0 };
			PostgresSetup *pgSetup = &(mconfig.pgSetup);
			LocalPostgresServer postgres = { 0 };

			bool reloadedService = false;

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &options.pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			/* now override current on-file settings with CLI ssl options */
			pgSetup->ssl = options.pgSetup.ssl;

			local_postgres_init(&postgres, pgSetup);

			/* update the Postgres SSL setup and maybe create the certificate */
			if (!update_ssl_configuration(&postgres, mconfig.hostname))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			/* make sure that the new SSL files are part of the setup */
			mconfig.pgSetup.ssl = postgres.postgresSetup.ssl;

			/* update the monitor's configuration to use SSL */
			if (!monitor_config_write_file(&mconfig))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (file_exists(mconfig.pathnames.pid))
			{
				reloadedService = cli_pg_autoctl_reload(mconfig.pathnames.pid);

				if (!reloadedService)
				{
					log_warn("Failed to reload the pg_autoctl, consider "
							 "restarting it to implement the SSL changes");
				}
			}

			/* display a nice summary to our users */
			log_info("Successfully enabled new SSL configuration:");
			log_info("  SSL is now %s",
					 pgSetup->ssl.active ? "active" : "disabled");

			if (pgSetup->ssl.createSelfSignedCert)
			{
				log_info("  Self-Signed certificates have been created and "
						 "deployed in Postgres configuration settings "
						 "ssl_key_file and ssl_cert_file");
			}

			if (reloadedService)
			{
				log_info("  pg_autoctl service has been signaled to reload "
						 "its configuration");
			}
			else
			{
				log_warn("  pg_autoctl service is not running, changes "
						 "will only apply at next start of pg_autoctl");
			}

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			KeeperConfig kconfig = { 0 };
			PostgresSetup *pgSetup = &(kconfig.pgSetup);
			LocalPostgresServer postgres = { 0 };

			bool reloadedService = false;
			bool updatedMonitorString = true;

			kconfig.pgSetup = options.pgSetup;
			kconfig.pathnames = options.pathnames;

			if (!keeper_config_read_file(&kconfig,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				log_fatal("Failed to read configuration file \"%s\"",
						  kconfig.pathnames.config);
				exit(EXIT_CODE_BAD_CONFIG);
			}

			/* now override current on-file settings with CLI ssl options */
			pgSetup->ssl = options.pgSetup.ssl;

			local_postgres_init(&postgres, pgSetup);

			/* log about the need to edit the monitor connection string */
			if (!update_monitor_connection_string(&kconfig))
			{
				updatedMonitorString = false;
				log_error(
					"Failed to update the monitor URI, rerun this command "
					"again after resolving the issue to update it");
			}

			/* update the Postgres SSL setup and maybe create the certificate */
			if (!update_ssl_configuration(&postgres, kconfig.hostname))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			/* make sure that the new SSL files are part of the setup */
			kconfig.pgSetup.ssl = postgres.postgresSetup.ssl;

			/* and write our brand new setup to file */
			if (!keeper_config_write_file(&kconfig))
			{
				log_fatal("Failed to write the pg_autoctl configuration file, "
						  "see above");
				exit(EXIT_CODE_BAD_CONFIG);
			}

			if (file_exists(kconfig.pathnames.pid))
			{
				reloadedService = cli_pg_autoctl_reload(kconfig.pathnames.pid);

				if (!reloadedService)
				{
					log_error("Failed to reload the pg_autoctl, consider "
							  "restarting it to implement the SSL changes");
				}
			}

			/* display a nice summary to our users */
			log_info("Successfully enabled new SSL configuration:");
			log_info("  SSL is now %s",
					 pgSetup->ssl.active ? "active" : "disabled");

			if (pgSetup->ssl.createSelfSignedCert)
			{
				log_info("  Self-Signed certificates have been created and "
						 "deployed in Postgres configuration settings "
						 "ssl_key_file and ssl_cert_file");
			}

			if (updatedMonitorString)
			{
				log_info("  Postgres connection string to the monitor "
						 "has been changed to use sslmode \"%s\"",
						 pgsetup_sslmode_to_string(pgSetup->ssl.sslMode));
			}
			else
			{
				log_error("  Postgres connection string to the monitor "
						  "could not be updated, see above for details");
			}

			log_info("  Replication connection string primary_conninfo "
					 "is going to be updated in the main service loop "
					 "to use ssl mode \"%s\"",
					 pgsetup_sslmode_to_string(pgSetup->ssl.sslMode));

			if (reloadedService)
			{
				log_info("  pg_autoctl service has been signaled to reload "
						 "its configuration");
			}
			else
			{
				log_error("  pg_autoctl service is not running, changes "
						  "will only apply at next start of pg_autoctl");
			}

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  options.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * update_ssl_configuration updates the local SSL configuration.
 */
static bool
update_ssl_configuration(LocalPostgresServer *postgres, const char *hostname)
{
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	log_trace("update_ssl_configuration: ssl %s",
			  pgSetup->ssl.active ? "on" : "off");

	/*
	 * When --ssl-self-signed is used, create a certificate.
	 *
	 * In the caller function cli_enable_ssl() we then later write the
	 * pg_autoctl.conf file with the new SSL settings, including both the
	 * ssl.cert_file and the ssl.key_file values, and reload the pg_autoctl
	 * service if it's running.
	 *
	 * At reload time, the pg_autoctl service will edit our Postgres settings
	 * in postgresql-auto-failover.conf with the new values and reload
	 * Postgres.
	 */
	if (pgSetup->ssl.createSelfSignedCert &&
		(!file_exists(pgSetup->ssl.serverKey) ||
		 !file_exists(pgSetup->ssl.serverCert)))
	{
		if (!pg_create_self_signed_cert(pgSetup, hostname))
		{
			log_error("Failed to create SSL self-signed certificate, "
					  "see above for details");
			return false;
		}
	}

	/* HBA rules for hostssl are not edited */
	log_warn("HBA rules in \"%s/pg_hba.conf\" have NOT been edited: \"host\" "
			 " records match either SSL or non-SSL connection attempts.",
			 pgSetup->pgdata);

	return true;
}


/*
 * update_monitor_connection_string connects to the monitor to see if ssl is
 * active on the server. When that's the case, the function complains about
 * updating the monitor URI in the given KeeperConfig.
 *
 * TODO: it would be nice that we actually edit the URI that we have in the
 * file, but it appears to be too big an amount of work for the time being. We
 * could use PQconninfoParse and then add our own SSL settings, and then use
 * PQconnectdbParams to make sure we can use the new connection.
 */
static bool
update_monitor_connection_string(KeeperConfig *config)
{
	Monitor monitor = { 0 };

	URIParams params = { 0 };
	KeyVal sslParams = {
		3, { "sslmode", "sslrootcert", "sslcrl" }, { 0 }
	};

	char newPgURI[MAXCONNINFO] = { 0 };

	/* initialize SSL Params values */
	strlcpy(sslParams.values[0],
			pgsetup_sslmode_to_string(config->pgSetup.ssl.sslMode),
			MAXCONNINFO);

	strlcpy(sslParams.values[1], config->pgSetup.ssl.caFile, MAXCONNINFO);
	strlcpy(sslParams.values[2], config->pgSetup.ssl.crlFile, MAXCONNINFO);

	if (!parse_pguri_info_key_vals(config->monitor_pguri,
								   &sslParams,
								   &params))
	{
		log_warn(
			"The monitor SSL setup is ready and your current "
			"connection string is \"%s\", you might need to update it",
			config->monitor_pguri);

		log_info(
			"Use pg_autoctl config set pg_autoctl.monitor for updating "
			"your monitor connection string, then restart pg_autoctl ");
	}

	if (!buildPostgresURIfromPieces(&params, newPgURI))
	{
		log_error("Failed to produce the new monitor connection string");
		return false;
	}

	if (!monitor_init(&monitor, newPgURI))
	{
		/* errors have already been logged */
		return false;
	}


	log_info(
		"Trying to connect to monitor using connection string \"%s\"",
		newPgURI
		);

	/*
	 * Try to connect using the new connection string and don't update it if it
	 * does not actually allow connecting.
	 */
	monitor.pgsql.connectFailFast = true;
	if (!pgsql_execute_with_params(&monitor.pgsql, "SELECT 1", 0, NULL, NULL, NULL, NULL))
	{
		return false;
	}

	/* we have a new monitor URI with our new SSL parameters */
	strlcpy(config->monitor_pguri, newPgURI, MAXCONNINFO);

	log_info("Updating the monitor URI to \"%s\"", config->monitor_pguri);

	return true;
}


/*
 * cli_disable_ssl enables SSL setup on this node.
 *
 * The following two commands do the same thing:
 *
 *  - pg_autoctl enable ssl --no-ssl
 *  - pg_autoctl disable ssl
 */
static void
cli_disable_ssl(int argc, char **argv)
{
	/* prepare the global command line options keeperOptions as if --no-ssl */
	keeperOptions.pgSetup.ssl.active = 0;
	keeperOptions.pgSetup.ssl.createSelfSignedCert = false;

	/* this does some validation and user facing WARNing messages */
	if (!pgsetup_validate_ssl_settings(&(keeperOptions.pgSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	(void) cli_enable_ssl(argc, argv);
}
