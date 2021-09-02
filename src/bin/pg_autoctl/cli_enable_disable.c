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

static bool allowFailover = false;
static bool optForce = false;

static int cli_secondary_getopts(int argc, char **argv);
static void cli_enable_secondary(int argc, char **argv);
static void cli_disable_secondary(int argc, char **argv);

static int cli_maintenance_getopts(int argc, char **argv);
static void cli_enable_maintenance(int argc, char **argv);
static void cli_disable_maintenance(int argc, char **argv);

static int cli_ssl_getopts(int argc, char **argv);
static void cli_enable_ssl(int argc, char **argv);
static void cli_disable_ssl(int argc, char **argv);

static int cli_enable_monitor_getopts(int argc, char **argv);
static int cli_disable_monitor_getopts(int argc, char **argv);
static void cli_enable_monitor(int argc, char **argv);
static void cli_disable_monitor(int argc, char **argv);

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
				 " [ --pgdata --allow-failover ]",
				 CLI_PGDATA_OPTION,
				 cli_maintenance_getopts,
				 cli_enable_maintenance);

static CommandLine disable_maintenance_command =
	make_command("maintenance",
				 "Disable Postgres maintenance mode on this node",
				 " [ --pgdata ]",
				 CLI_PGDATA_OPTION,
				 cli_maintenance_getopts,
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


static CommandLine enable_monitor_command =
	make_command("monitor",
				 "Enable a monitor for this node to be orchestrated from",
				 " [ --pgdata --allow-failover ] "
				 "postgres://autoctl_node@new.monitor.add.ress/pg_auto_failover",
				 "  --pgdata      path to data directory\n",
				 cli_enable_monitor_getopts,
				 cli_enable_monitor);

static CommandLine disable_monitor_command =
	make_command("monitor",
				 "Disable the monitor for this node",
				 " [ --pgdata --force ] ",
				 "  --pgdata      path to data directory\n"
				 "  --force       force unregistering from the monitor\n",
				 cli_disable_monitor_getopts,
				 cli_disable_monitor);

static CommandLine *enable_subcommands[] = {
	&enable_secondary_command,
	&enable_maintenance_command,
	&enable_ssl_command,
	&enable_monitor_command,
	NULL
};

static CommandLine *disable_subcommands[] = {
	&disable_secondary_command,
	&disable_maintenance_command,
	&disable_ssl_command,
	&disable_monitor_command,
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
		{ "allow-failover", no_argument, NULL, 'A' },
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

			case 'A':
			{
				allowFailover = true;
				log_trace("--allow-failover");
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

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

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
 * cli_maintenance_getopts parses command line options for the pg_autoctl
 * enable|disable maintenance feature. We accept the --allow-failover option
 * that is unique to this command and so we have our own version of the getopt
 * parsing.
 */
static int
cli_maintenance_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "allow-failover", no_argument, NULL, 'A' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:f:AVvqh",
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

			case 'A':
			{
				allowFailover = true;
				log_trace("--allow-failover");
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

	/* now that we have the command line parameters, prepare the options */
	(void) prepare_keeper_options(&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
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

	ConnectionRetryPolicy retryPolicy = { 0 };

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
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (keeper.state.current_role == PRIMARY_STATE && !allowFailover)
	{
		log_warn("Enabling maintenance on a primary causes a failover");
		log_fatal("Please use --allow-failover to allow the command proceed");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * If we're already in MAINTENANCE the monitor returns true but we don't
	 * want to listen to changes, we don't expect any
	 */
	if (keeper.state.current_role != MAINTENANCE_STATE)
	{
		if (!pgsql_listen(&(keeper.monitor.notificationClient), channels))
		{
			log_error("Failed to listen to state changes from the monitor");
			exit(EXIT_CODE_MONITOR);
		}
	}

	/*
	 * Set a retry policy for cases when we have a transient error on the
	 * monitor.
	 */
	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		int64_t nodeId = keeper.state.current_node_id;
		bool mayRetry = false;

		if (monitor_start_maintenance(&(keeper.monitor), nodeId, &mayRetry))
		{
			/* start_maintenance was successful, break out of the retry loop */
			break;
		}

		if (!mayRetry)
		{
			log_fatal("Failed to enable maintenance of node %" PRId64
					  " on the monitor, see above for details",
					  nodeId);
			exit(EXIT_CODE_MONITOR);
		}

		int sleepTimeMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_warn("Failed to enable maintenance of node %" PRId64
				 " on the monitor, retrying in %d ms.",
				 nodeId, sleepTimeMs);

		/* we have milliseconds, pg_usleep() wants microseconds */
		(void) pg_usleep(sleepTimeMs * 1000);
	}

	if (keeper.state.current_role == MAINTENANCE_STATE)
	{
		log_info("This node is already in the \"maintenance\" state.");
		exit(EXIT_CODE_QUIT);
	}

	NodeState targetStates[] = { MAINTENANCE_STATE };
	if (!monitor_wait_until_node_reported_state(
			&(keeper.monitor),
			keeper.config.formation,
			keeper.config.groupId,
			keeper.state.current_node_id,
			keeper.config.pgSetup.pgKind,
			targetStates,
			lengthof(targetStates)))
	{
		log_error("Failed to wait until the node reached the maintenance state");
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

	ConnectionRetryPolicy retryPolicy = { 0 };

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
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	if (!pgsql_listen(&(keeper.monitor.notificationClient), channels))
	{
		log_error("Failed to listen to state changes from the monitor");
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * Set a retry policy for cases when we have a transient error on the
	 * monitor.
	 */
	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		int64_t nodeId = keeper.state.current_node_id;
		bool mayRetry = false;

		if (monitor_stop_maintenance(&(keeper.monitor), nodeId, &mayRetry))
		{
			/* stop_maintenance was successful, break out of the retry loop */
			break;
		}

		if (!mayRetry)
		{
			log_fatal("Failed to disable maintenance of node %" PRId64
					  " on the monitor, see above for details",
					  nodeId);
			exit(EXIT_CODE_MONITOR);
		}

		int sleepTimeMs = pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_warn("Failed to disable maintenance of node %" PRId64
				 " on the monitor, retrying in %d ms.",
				 nodeId, sleepTimeMs);

		/* we have milliseconds, pg_usleep() wants microseconds */
		(void) pg_usleep(sleepTimeMs * 1000);
	}

	NodeState targetStates[] = { SECONDARY_STATE, PRIMARY_STATE };

	if (!monitor_wait_until_node_reported_state(
			&(keeper.monitor),
			keeper.config.formation,
			keeper.config.groupId,
			keeper.state.current_node_id,
			keeper.config.pgSetup.pgKind,
			targetStates,
			lengthof(targetStates)))
	{
		log_error("Failed to wait until a node reached the secondary or primary state");
		exit(EXIT_CODE_MONITOR);
	}
}


/*
 * cli_ssl_getopts parses the command line options necessary to initialize a
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
	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

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
 */
static bool
update_monitor_connection_string(KeeperConfig *config)
{
	Monitor monitor = { 0 };

	URIParams params = { 0 };
	KeyVal sslParams = {
		3, { "sslmode", "sslrootcert", "sslcrl" }, { 0 }
	};

	bool checkForCompleteURI = true;
	char newPgURI[MAXCONNINFO] = { 0 };

	/* initialize SSL Params values */
	strlcpy(sslParams.values[0],
			pgsetup_sslmode_to_string(config->pgSetup.ssl.sslMode),
			MAXCONNINFO);

	strlcpy(sslParams.values[1], config->pgSetup.ssl.caFile, MAXCONNINFO);
	strlcpy(sslParams.values[2], config->pgSetup.ssl.crlFile, MAXCONNINFO);

	if (!parse_pguri_info_key_vals(config->monitor_pguri,
								   &sslParams,
								   &params,
								   checkForCompleteURI))
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

	char scrubbedConnectionString[MAXCONNINFO] = { 0 };
	if (parse_and_scrub_connection_string(newPgURI, scrubbedConnectionString))
	{
		log_info("Trying to connect to monitor using connection string \"%s\"",
				 scrubbedConnectionString);
	}
	else
	{
		log_error(
			"Trying to connect to monitor using unparseable connection string \"%s\"",
			newPgURI);
		return false;
	}


	/*
	 * Try to connect using the new connection string and don't update it if it
	 * does not actually allow connecting.
	 */
	if (!pgsql_execute_with_params(&(monitor.pgsql), "SELECT 1",
								   0, NULL, NULL, NULL, NULL))
	{
		return false;
	}

	/* we have a new monitor URI with our new SSL parameters */
	strlcpy(config->monitor_pguri, newPgURI, MAXCONNINFO);

	log_info("Updating the monitor URI to \"%s\"", scrubbedConnectionString);

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


/*
 * cli_enable_monitor_getopts parses the command line options for the
 * command `pg_autoctl enable monitor`.
 */
static int
cli_enable_monitor_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "allow-failover", no_argument, NULL, 'A' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* set default values for our options, when we have some */
	options.groupId = -1;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	options.pgSetup.settings.candidatePriority =
		FAILOVER_NODE_CANDIDATE_PRIORITY;
	options.pgSetup.settings.replicationQuorum =
		FAILOVER_NODE_REPLICATION_QUORUM;

	optind = 0;

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "D:m:AVvqh",
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

			case 'A':
			{
				allowFailover = true;
				log_trace("--allow-failover");
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
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_enable_monitor enables a monitor (again?) on a pg_autoctl node where it
 * currently is setup without a monitor.
 */
static void
cli_enable_monitor(int argc, char **argv)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperConfig *config = &(keeper.config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!config->monitorDisabled)
	{
		log_fatal("Failed to enable monitor \"%s\": "
				  "there is already a monitor enabled",
				  config->monitor_pguri);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * Parse monitor Postgres URI expected as the first (only) argument.
	 */
	if (argc != 1)
	{
		log_fatal("Failed to parse new monitor URI as an argument.");
		commandline_print_usage(&enable_monitor_command, stderr);

		exit(EXIT_CODE_BAD_ARGS);
	}

	strlcpy(config->monitor_pguri, argv[0], MAXCONNINFO);

	if (!validate_connection_string(config->monitor_pguri))
	{
		log_fatal("Failed to parse the new monitor connection string, "
				  "see above for details.");
		exit(EXIT_CODE_BAD_ARGS);
	}

	config->monitorDisabled = false;

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(monitor, config->monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * Now register to the new monitor from this "client-side" process, and
	 * then signal the background pg_autoctl service for this node (if any) to
	 * reload its configuration so that it starts calling node_active() to the
	 * new monitor.
	 */
	if (!keeper_register_again(&keeper))
	{
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * Now that we have registered again, reload the background process (if any
	 * is running) so that it connects to the monitor for the node_active
	 * protocol. When we reload the background process, we need the
	 * configuration file to have been updated first on-disk:
	 */
	if (!keeper_config_write_file(config))
	{
		log_fatal("Failed to write pg_autoctl configuration file \"%s\", "
				  "see above for details",
				  keeper.config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* time to reload the running pg_autoctl service, when it's running */
	if (file_exists(keeper.config.pathnames.pid))
	{
		bool reloadedService =
			cli_pg_autoctl_reload(keeper.config.pathnames.pid);

		if (!reloadedService)
		{
			log_fatal("Failed to reload the pg_autoctl service");
		}
	}
}


/*
 * cli_disable_monitor_getopts parses the command line options for the
 * command `pg_autoctl disable monitor`.
 */
static int
cli_disable_monitor_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "force", no_argument, NULL, 'F' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* set default values for our options, when we have some */
	options.groupId = -1;
	options.network_partition_timeout = -1;
	options.prepare_promotion_catchup = -1;
	options.prepare_promotion_walreceiver = -1;
	options.postgresql_restart_failure_timeout = -1;
	options.postgresql_restart_failure_max_retries = -1;

	/* do not set a default formation, it should be found in the config file */

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:FVvqh",
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

			case 'F':
			{
				optForce = true;
				log_trace("--force");
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
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	keeperOptions = options;

	return optind;
}


/*
 * cli_disable_monitor disables the monitor on a running pg_autoctl node. This
 * is useful when the monitor has been lost and a maintenance operation has to
 * register the node to a new monitor without stopping Postgres.
 */
static void
cli_disable_monitor(int argc, char **argv)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperConfig *config = &(keeper.config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

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
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(monitor, keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	/*
	 * Unless --force has been used, we only disable the monitor when the
	 * current node has not been registered. When --force is used, we remove
	 * our registration from the monitor first.
	 */
	NodeAddressArray nodesArray = { 0 };
	int nodeIndex = 0;

	/*
	 * There might be some race conditions here, but it's all to be
	 * user-friendly so in the worst case we're going to be less friendly that
	 * we could have.
	 */
	if (!monitor_get_nodes(monitor,
						   config->formation,
						   config->groupId,
						   &nodesArray))
	{
		if (optForce)
		{
			/* ignore the error, just don't wait in that case */
			log_warn("Failed to get_nodes() on the monitor");
			log_info("Failed to contact the monitor, disabling it as requested");
		}
		else
		{
			log_warn("Failed to get_nodes() on the monitor");
			log_fatal("Failed to contact the monitor, use --force to continue");
			exit(EXIT_CODE_MONITOR);
		}
	}

	for (nodeIndex = 0; nodeIndex < nodesArray.count; nodeIndex++)
	{
		if (nodesArray.nodes[nodeIndex].nodeId == keeper.state.current_node_id)
		{
			/* we found our node, exit */
			break;
		}
	}

	/* did we find the local node on the monitor? */
	if (nodeIndex < nodesArray.count)
	{
		if (optForce)
		{
			/* --force, and we found the node */
			log_info("Removing node %" PRId64 " \"%s\" (%s:%d) from monitor",
					 nodesArray.nodes[nodeIndex].nodeId,
					 nodesArray.nodes[nodeIndex].name,
					 nodesArray.nodes[nodeIndex].host,
					 nodesArray.nodes[nodeIndex].port);

			int64_t nodeId = -1;
			int groupId = -1;

			if (!monitor_remove_by_hostname(
					monitor,
					nodesArray.nodes[nodeIndex].host,
					nodesArray.nodes[nodeIndex].port,
					optForce,
					&nodeId,
					&groupId))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_MONITOR);
			}
		}
		else
		{
			/* node was found on the monitor, but --force not provided */
			log_info("Found node %" PRId64 " \"%s\" (%s:%d) on the monitor",
					 nodesArray.nodes[nodeIndex].nodeId,
					 nodesArray.nodes[nodeIndex].name,
					 nodesArray.nodes[nodeIndex].host,
					 nodesArray.nodes[nodeIndex].port);
			log_fatal("Use --force to remove the node from the monitor");
			exit(EXIT_CODE_BAD_STATE);
		}
	}

	/*
	 * Now either we didn't find the node on the monitor, or we just removed it
	 * from there. In either case, we can proceed with disabling the monitor
	 * from the node setup, and removing the local state file.
	 */
	strlcpy(config->monitor_pguri,
			PG_AUTOCTL_MONITOR_DISABLED,
			sizeof(config->monitor_pguri));

	config->monitorDisabled = true;

	if (!keeper_config_write_file(config))
	{
		log_fatal("Failed to write pg_autoctl configuration file \"%s\", "
				  "see above for details",
				  keeper.config.pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* time to reload the running pg_autoctl service, when it's running */
	if (file_exists(keeper.config.pathnames.pid))
	{
		bool reloadedService =
			cli_pg_autoctl_reload(keeper.config.pathnames.pid);

		if (!reloadedService)
		{
			log_fatal("Failed to reload the pg_autoctl service");
		}
	}
}
