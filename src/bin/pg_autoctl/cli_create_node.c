/*
 * src/bin/pg_autoctl/cli_create_node.c
 *     Implementation of the pg_autoctl create and pg_autoctl drop CLI for the
 *     pg_auto_failover nodes (monitor, coordinator, worker, postgres).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "fsm.h"
#include "ini_file.h"
#include "ipaddr.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "monitor_pg_init.h"
#include "pgctl.h"
#include "pghba.h"
#include "pidfile.h"
#include "primary_standby.h"
#include "service_keeper.h"
#include "service_keeper_init.h"
#include "service_monitor.h"
#include "service_monitor_init.h"
#include "string_utils.h"

/*
 * Global variables that we're going to use to "communicate" in between getopts
 * functions and their command implementation. We can't pass parameters around.
 */
MonitorConfig monitorOptions = { 0 };

static int cli_create_postgres_getopts(int argc, char **argv);
static void cli_create_postgres(int argc, char **argv);

static int cli_create_monitor_getopts(int argc, char **argv);
static void cli_create_monitor(int argc, char **argv);

static void check_hostname(const char *hostname);

CommandLine create_monitor_command =
	make_command(
		"monitor",
		"Initialize a pg_auto_failover monitor node",
		" [ --pgdata --pgport --pgctl --hostname ] ",
		"  --pgctl           path to pg_ctl\n"
		"  --pgdata          path to data directory\n"
		"  --pgport          PostgreSQL's port number\n"
		"  --hostname        hostname by which postgres is reachable\n"
		"  --auth            authentication method for connections from data nodes\n"
		"  --skip-pg-hba     skip editing pg_hba.conf rules\n"
		"  --run             create node then run pg_autoctl service\n"
		KEEPER_CLI_SSL_OPTIONS,
		cli_create_monitor_getopts,
		cli_create_monitor);

CommandLine create_postgres_command =
	make_command(
		"postgres",
		"Initialize a pg_auto_failover standalone postgres node",
		"",
		"  --pgctl           path to pg_ctl\n"
		"  --pgdata          path to data directory\n"
		"  --pghost          PostgreSQL's hostname\n"
		"  --pgport          PostgreSQL's port number\n"
		"  --listen          PostgreSQL's listen_addresses\n"
		"  --username        PostgreSQL's username\n"
		"  --dbname          PostgreSQL's database name\n"
		"  --name            pg_auto_failover node name\n"
		"  --hostname        hostname used to connect from the other nodes\n"
		"  --formation       pg_auto_failover formation\n"
		"  --monitor         pg_auto_failover Monitor Postgres URL\n"
		"  --auth            authentication method for connections from monitor\n"
		"  --skip-pg-hba     skip editing pg_hba.conf rules\n"
		"  --pg-hba-lan      edit pg_hba.conf rules for --dbname in detected LAN\n"
		KEEPER_CLI_SSL_OPTIONS
		"  --candidate-priority    priority of the node to be promoted to become primary\n"
		"  --replication-quorum    true if node participates in write quorum\n"
		"  --maximum-backup-rate   maximum transfer rate of data transferred from the server during initial sync\n",
		cli_create_postgres_getopts,
		cli_create_postgres);


/*
 * cli_create_config manages the whole set of configuration parameters that
 * pg_autoctl accepts and deals with either creating a configuration file if
 * necessary, or merges the command line arguments into the pre-existing
 * configuration file.
 */
bool
cli_create_config(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	/*
	 * We support two modes of operations here:
	 *   - configuration exists already, we need PGDATA
	 *   - configuration doesn't exist already, we need PGDATA, and more
	 */
	if (file_exists(config->pathnames.config))
	{
		Monitor *monitor = &(keeper->monitor);
		KeeperConfig options = { 0 };
		KeeperConfig oldConfig = { 0 };
		PostgresSetup optionsFullPgSetup = { 0 };

		if (!keeper_config_read_file(config,
									 missingPgdataIsOk,
									 pgIsNotRunningIsOk,
									 monitorDisabledIsOk))
		{
			log_fatal("Failed to read configuration file \"%s\"",
					  config->pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		oldConfig = *config;
		options = *config;

		/*
		 * Before merging command line options into the (maybe) pre-existing
		 * configuration file, we should also mix in the environment variables
		 * values in the command line options.
		 */
		if (!pg_setup_init(&optionsFullPgSetup,
						   &(options.pgSetup),
						   missingPgdataIsOk,
						   pgIsNotRunningIsOk))
		{
			exit(EXIT_CODE_BAD_ARGS);
		}

		options.pgSetup = optionsFullPgSetup;

		/*
		 * Now that we have loaded the configuration file, apply the command
		 * line options on top of it, giving them priority over the config.
		 */
		if (!keeper_config_merge_options(config, &options))
		{
			/* errors have been logged already */
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/*
		 * If we have registered to the monitor already, then we need to check
		 * if the user is providing new --nodename, --hostname, or --pgport
		 * arguments. After all, they may change their mind of have just
		 * realized that the --pgport they wanted to use is already in use.
		 */
		if (!config->monitorDisabled)
		{
			if (!monitor_init(monitor, config->monitor_pguri))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_ARGS);
			}

			if (file_exists(config->pathnames.state))
			{
				/*
				 * Handle the node metadata options: --name, --hostname,
				 * --pgport.
				 *
				 * When those options have been used, then the configuration
				 * file has been merged with the command line values, and we
				 * can update the metadata for this node on the monitor.
				 */
				if (!keeper_set_node_metadata(keeper, &oldConfig))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_MONITOR);
				}

				/*
				 * Now, at 1.3 to 1.4 upgrade, the monitor assigns a new name to
				 * pg_autoctl nodes, which did not use to have a name before. In
				 * that case, and then pg_autoctl run has been used without
				 * options, our name might be empty here. We then need to fetch
				 * it from the monitor.
				 */
				if (!keeper_update_nodename_from_monitor(keeper))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_CONFIG);
				}
			}
		}
	}
	else
	{
		/* set our KeeperConfig from the command line options now. */
		(void) keeper_config_init(config,
								  missingPgdataIsOk,
								  pgIsNotRunningIsOk);

		/* and write our brand new setup to file */
		if (!keeper_config_write_file(config))
		{
			log_fatal("Failed to write the pg_autoctl configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	return true;
}


/*
 * cli_pg_create calls keeper_pg_init where all the magic happens.
 */
void
cli_create_pg(Keeper *keeper)
{
	if (!keeper_pg_init(keeper))
	{
		/* errors have been logged */
		exit(EXIT_CODE_BAD_STATE);
	}
}


/*
 * cli_create_postgres_getopts parses command line options and set the global
 * variable keeperOptions from them, without doing any check.
 */
static int
cli_create_postgres_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };

	static struct option long_options[] = {
		{ "pgctl", required_argument, NULL, 'C' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "pghost", required_argument, NULL, 'H' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "listen", required_argument, NULL, 'l' },
		{ "username", required_argument, NULL, 'U' },
		{ "auth", required_argument, NULL, 'A' },
		{ "skip-pg-hba", no_argument, NULL, 'S' },
		{ "pg-hba-lan", no_argument, NULL, 'L' },
		{ "dbname", required_argument, NULL, 'd' },
		{ "name", required_argument, NULL, 'a' },
		{ "hostname", required_argument, NULL, 'n' },
		{ "formation", required_argument, NULL, 'f' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "disable-monitor", no_argument, NULL, 'M' },
		{ "node-id", required_argument, NULL, 'I' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ "candidate-priority", required_argument, NULL, 'P' },
		{ "replication-quorum", required_argument, NULL, 'r' },
		{ "maximum-backup-rate", required_argument, NULL, 'R' },
		{ "run", no_argument, NULL, 'x' },
		{ "no-ssl", no_argument, NULL, 'N' },
		{ "ssl-self-signed", no_argument, NULL, 's' },
		{ "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
		{ "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG },
		{ "ssl-crl-file", required_argument, &ssl_flag, SSL_CRL_FILE_FLAG },
		{ "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG },
		{ "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG },
		{ NULL, 0, NULL, 0 }
	};

	int optind =
		cli_create_node_getopts(argc, argv, long_options,
								"C:D:H:p:l:U:A:SLd:a:n:f:m:MI:RVvqhP:r:xsN",
								&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_create_postgres prepares a local PostgreSQL instance to be used as a
 * standalone Postgres instance, not in a Citus formation.
 */
static void
cli_create_postgres(int argc, char **argv)
{
	pid_t pid = 0;
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);

	keeper.config = keeperOptions;

	if (read_pidfile(config->pathnames.pid, &pid))
	{
		log_fatal("pg_autoctl is already running with pid %d", pid);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!file_exists(config->pathnames.config))
	{
		/* pg_autoctl create postgres: mark ourselves as a standalone node */
		config->pgSetup.pgKind = NODE_KIND_STANDALONE;
		strlcpy(config->nodeKind, "standalone", NAMEDATALEN);

		if (!check_or_discover_hostname(config))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (!cli_create_config(&keeper))
	{
		log_error("Failed to initialize our configuration, see above.");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	cli_create_pg(&keeper);
}


/*
 * cli_create_monitor_getopts parses the command line options necessary to
 * initialize a PostgreSQL instance as our monitor.
 */
static int
cli_create_monitor_getopts(int argc, char **argv)
{
	MonitorConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	SSLCommandLineOptions sslCommandLineOptions = SSL_CLI_UNKNOWN;

	static struct option long_options[] = {
		{ "pgctl", required_argument, NULL, 'C' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "hostname", required_argument, NULL, 'n' },
		{ "listen", required_argument, NULL, 'l' },
		{ "auth", required_argument, NULL, 'A' },
		{ "skip-pg-hba", no_argument, NULL, 'S' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ "run", no_argument, NULL, 'x' },
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

	while ((c = getopt_long(argc, argv, "C:D:p:n:l:A:SVvqhxNs",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'C':
			{
				strlcpy(options.pgSetup.pg_ctl, optarg, MAXPGPATH);
				log_trace("--pg_ctl %s", options.pgSetup.pg_ctl);
				break;
			}

			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &options.pgSetup.pgport))
				{
					log_fatal("--pgport argument is a valid port number: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--pgport %d", options.pgSetup.pgport);
				break;
			}

			case 'l':
			{
				strlcpy(options.pgSetup.listen_addresses, optarg, MAXPGPATH);
				log_trace("--listen %s", options.pgSetup.listen_addresses);
				break;
			}

			case 'n':
			{
				strlcpy(options.hostname, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--hostname %s", options.hostname);
				break;
			}

			case 'A':
			{
				if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.authMethod))
				{
					errors++;
					log_error("Please use either --auth or --skip-pg-hba");
				}

				strlcpy(options.pgSetup.authMethod, optarg, NAMEDATALEN);
				log_trace("--auth %s", options.pgSetup.authMethod);
				break;
			}

			case 'S':
			{
				if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.authMethod))
				{
					errors++;
					log_error("Please use either --auth or --skip-pg-hba");
				}

				/* force default authentication method then */
				strlcpy(options.pgSetup.authMethod,
						DEFAULT_AUTH_METHOD,
						NAMEDATALEN);

				options.pgSetup.hbaLevel = HBA_EDIT_SKIP;

				log_trace("--skip-pg-hba");
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

			case 'x':
			{
				/* { "run", no_argument, NULL, 'x' }, */
				createAndRun = true;
				log_trace("--run");
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
			 * { "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG }
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

	/*
	 * We're not using pg_setup_init() here: we are following a very different
	 * set of rules. We just want to check:
	 *
	 *   - PGDATA is set and the directory does not exist
	 *   - PGPORT is either set or defaults to 5432
	 *
	 * Also we use the first pg_ctl binary found in the PATH, we're not picky
	 * here, we don't have to manage the whole life-time of that PostgreSQL
	 * instance.
	 */
	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	/*
	 * We support two modes of operations here:
	 *   - configuration exists already, we need PGDATA
	 *   - configuration doesn't exist already, we need PGDATA, and more
	 */
	if (!monitor_config_set_pathnames_from_pgdata(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * We require the user to specify an authentication mechanism, or to use
	 * --skip-pg-hba. Our documentation tutorial will use --auth trust, and we
	 * should make it obvious that this is not the right choice for production.
	 */
	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.authMethod))
	{
		log_fatal("Please use either --auth trust|md5|... or --skip-pg-hba");
		log_info("pg_auto_failover can be set to edit Postgres HBA rules "
				 "automatically when needed. For quick testing '--auth trust' "
				 "makes it easy to get started, "
				 "consider another authentication mechanism for production.");
		exit(EXIT_CODE_BAD_ARGS);
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
				  "--server-key, and --server-cert (or use --no-ssl if you "
				  "are very sure that you do not want encrypted traffic)");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!pgsetup_validate_ssl_settings(&(options.pgSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pg_ctl))
	{
		set_first_pgctl(&(options.pgSetup));
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.listen_addresses))
	{
		strlcpy(options.pgSetup.listen_addresses,
				POSTGRES_DEFAULT_LISTEN_ADDRESSES, MAXPGPATH);
	}

	/* publish our option parsing in the global variable */
	monitorOptions = options;

	return optind;
}


/*
 * cli_create_monitor_config takes care of the monitor configuration, either
 * creating it from scratch or merging the pg_autoctl create monitor command
 * line arguments and options with the pre-existing configuration file (for
 * when people change their mind or fix an error in the previous command).
 */
static bool
cli_create_monitor_config(Monitor *monitor)
{
	MonitorConfig *config = &(monitor->config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;

	if (file_exists(config->pathnames.config))
	{
		MonitorConfig options = monitor->config;

		if (!monitor_config_read_file(config,
									  missingPgdataIsOk, pgIsNotRunningIsOk))
		{
			log_fatal("Failed to read configuration file \"%s\"",
					  config->pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		/*
		 * Now that we have loaded the configuration file, apply the command
		 * line options on top of it, giving them priority over the config.
		 */
		if (!monitor_config_merge_options(config, &options))
		{
			/* errors have been logged already */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	else
	{
		/* Take care of the --hostname */
		if (IS_EMPTY_STRING_BUFFER(config->hostname))
		{
			if (!ipaddrGetLocalHostname(config->hostname,
										sizeof(config->hostname)))
			{
				char monitorHostname[_POSIX_HOST_NAME_MAX] = { 0 };

				strlcpy(monitorHostname,
						DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
						_POSIX_HOST_NAME_MAX);

				if (!discover_hostname((char *) &(config->hostname),
									   _POSIX_HOST_NAME_MAX,
									   DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
									   DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT))
				{
					log_fatal("Failed to auto-detect the hostname "
							  "of this machine, please provide one "
							  "via --hostname");
					exit(EXIT_CODE_BAD_ARGS);
				}
			}
		}
		else
		{
			/*
			 * When provided with a --hostname option, we run some checks on
			 * the user provided value based on Postgres usage for the hostname
			 * in its HBA setup. Both forward and reverse DNS needs to return
			 * meaningful values for the connections to be granted when using a
			 * hostname.
			 *
			 * That said network setup is something complex and we don't
			 * pretend we are able to avoid any and all false negatives in our
			 * checks, so we only WARN when finding something that might be
			 * fishy, and proceed with the setup of the local node anyway.
			 */
			(void) check_hostname(config->hostname);
		}

		/* set our MonitorConfig from the command line options now. */
		(void) monitor_config_init(config,
								   missingPgdataIsOk,
								   pgIsNotRunningIsOk);

		/* and write our brand new setup to file */
		if (!monitor_config_write_file(config))
		{
			log_fatal("Failed to write the monitor's configuration file, "
					  "see above");
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}

	return true;
}


/*
 * Initialize the PostgreSQL instance that we're using for the Monitor:
 *
 *  - pg_ctl initdb
 *  - add postgresql-citus.conf to postgresql.conf
 *  - pg_ctl start
 *  - create user autoctl with createdb login;
 *  - create database pg_auto_failover with owner autoctl;
 *  - create extension pgautofailover;
 *
 * When this function is called (from monitor_config_init at the CLI level), we
 * know that PGDATA has been initdb already, and that's about it.
 *
 */
static void
cli_create_monitor(int argc, char **argv)
{
	pid_t pid = 0;
	Monitor monitor = { 0 };
	MonitorConfig *config = &(monitor.config);

	monitor.config = monitorOptions;

	if (read_pidfile(config->pathnames.pid, &pid))
	{
		log_fatal("pg_autoctl is already running with pid %d", pid);
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!cli_create_monitor_config(&monitor))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* Initialize our local connection to the monitor */
	if (!monitor_local_init(&monitor))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	/* Ok, now we know we have a configuration file, and it's been loaded. */
	if (!monitor_pg_init(&monitor))
	{
		/* errors have been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!service_monitor_init(&monitor))
	{
		/* errors have been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * check_or_discover_hostname checks given --hostname or attempt to discover a
 * suitable default value for the current node when it's not been provided on
 * the command line.
 */
bool
check_or_discover_hostname(KeeperConfig *config)
{
	/* take care of the hostname */
	if (IS_EMPTY_STRING_BUFFER(config->hostname))
	{
		char monitorHostname[_POSIX_HOST_NAME_MAX];
		int monitorPort = 0;

		/*
		 * When --disable-monitor, use the defaults for ipAddr discovery, same
		 * as when creating the monitor node itself.
		 */
		if (config->monitorDisabled)
		{
			strlcpy(monitorHostname,
					DEFAULT_INTERFACE_LOOKUP_SERVICE_NAME,
					_POSIX_HOST_NAME_MAX);

			monitorPort = DEFAULT_INTERFACE_LOOKUP_SERVICE_PORT;
		}
		else if (!hostname_from_uri(config->monitor_pguri,
									monitorHostname, _POSIX_HOST_NAME_MAX,
									&monitorPort))
		{
			log_fatal("Failed to determine monitor hostname when parsing "
					  "Postgres URI \"%s\"", config->monitor_pguri);
			return false;
		}

		if (!discover_hostname((char *) &(config->hostname),
							   _POSIX_HOST_NAME_MAX,
							   monitorHostname,
							   monitorPort))
		{
			log_fatal("Failed to auto-detect the hostname of this machine, "
					  "please provide one via --hostname");
			return false;
		}
	}
	else
	{
		/*
		 * When provided with a --hostname option, we run some checks on the
		 * user provided value based on Postgres usage for the hostname in its
		 * HBA setup. Both forward and reverse DNS needs to return meaningful
		 * values for the connections to be granted when using a hostname.
		 *
		 * That said network setup is something complex and we don't pretend we
		 * are able to avoid any and all false negatives in our checks, so we
		 * only WARN when finding something that might be fishy, and proceed
		 * with the setup of the local node anyway.
		 */
		(void) check_hostname(config->hostname);
	}
	return true;
}


/*
 * discover_hostname discovers a suitable --hostname default value in three
 * steps:
 *
 * 1. First find the local LAN IP address by connecting a socket() to either an
 *    internet service (8.8.8.8:53) or to the monitor's hostname and port, and
 *    then inspecting which local address has been used.
 *
 * 2. Use the local IP address obtained in the first step and do a reverse DNS
 *    lookup for it. The answer is our candidate default --hostname.
 *
 * 3. Do a DNS lookup for the candidate default --hostname. If we get back a IP
 *    address that matches one of the local network interfaces, we keep the
 *    candidate, the DNS lookup that Postgres does at connection time is
 *    expected to then work.
 *
 * All this dansing around DNS lookups is necessary in order to mimic Postgres
 * HBA matching of hostname rules against client IP addresses: the hostname in
 * the HBA rule is resolved and compared to the client IP address. We want the
 * --hostname we use to resolve to an IP address that exists on the local
 * Postgres server.
 *
 * Worst case here is that we fail to discover a --hostname and then ask the
 * user to provide one for us.
 *
 * monitorHostname and monitorPort are used to open a socket to that address,
 * in order to find the right outbound interface. When creating a monitor node,
 * of course, we don't have the monitorHostname yet: we are trying to discover
 * it... in that case we use PG_AUTOCTL_DEFAULT_SERVICE_NAME and PORT, which
 * are the Google DNS service: 8.8.8.8:53, expected to be reachable.
 */
bool
discover_hostname(char *hostname, int size,
				  const char *monitorHostname, int monitorPort)
{
	/*
	 * Try and find a default --hostname. The --hostname is mandatory, so
	 * when not provided for by the user, then failure to discover a
	 * suitable hostname is a fatal error.
	 */
	char ipAddr[BUFSIZE];
	char localIpAddr[BUFSIZE];
	char hostnameCandidate[_POSIX_HOST_NAME_MAX];

	ConnectionRetryPolicy retryPolicy = { 0 };

	/* retry connecting to the monitor when it's not available */
	(void) pgsql_set_monitor_interactive_retry_policy(&retryPolicy);

	while (!pgsql_retry_policy_expired(&retryPolicy))
	{
		bool mayRetry = false;

		/* fetch our local address among the network interfaces */
		if (fetchLocalIPAddress(ipAddr, BUFSIZE, monitorHostname, monitorPort,
								LOG_DEBUG, &mayRetry))
		{
			/* success: break out of the retry loop */
			break;
		}

		if (!mayRetry)
		{
			log_fatal("Failed to find a local IP address, "
					  "please provide --hostname.");
			return false;
		}

		int sleepTimeMs =
			pgsql_compute_connection_retry_sleep_time(&retryPolicy);

		log_warn("Failed to connect to \"%s\" on port %d "
				 "to discover this machine hostname, "
				 "retrying in %d ms.",
				 monitorHostname, monitorPort, sleepTimeMs);

		/* we have milliseconds, pg_usleep() wants microseconds */
		(void) pg_usleep(sleepTimeMs * 1000);
	}

	/* from there on we can take the ipAddr as the default --hostname */
	strlcpy(hostname, ipAddr, size);
	log_debug("discover_hostname: local ip %s", ipAddr);

	/* do a reverse DNS lookup from our local LAN ip address */
	if (!findHostnameFromLocalIpAddress(ipAddr,
										hostnameCandidate,
										_POSIX_HOST_NAME_MAX))
	{
		/* errors have already been logged */
		log_info("Using local IP address \"%s\" as the --hostname.", ipAddr);
		return true;
	}
	log_debug("discover_hostname: host from ip %s", hostnameCandidate);

	/* do a DNS lookup of the hostname we got from the IP address */
	if (!findHostnameLocalAddress(hostnameCandidate, localIpAddr, BUFSIZE))
	{
		/* errors have already been logged */
		log_info("Using local IP address \"%s\" as the --hostname.", ipAddr);
		return true;
	}
	log_debug("discover_hostname: ip from host %s", localIpAddr);

	/*
	 * ok ipAddr resolves to an hostname that resolved back to a local address,
	 * we should be able to use the hostname in pg_hba.conf
	 */
	strlcpy(hostname, hostnameCandidate, size);
	log_info("Using --hostname \"%s\", which resolves to IP address \"%s\"",
			 hostname, localIpAddr);

	return true;
}


/*
 * check_hostname runs some DNS check against the provided --hostname in order
 * to warn the user in case we might later fail to use it in the Postgres HBA
 * setup.
 *
 * The main trouble we guard against is from HBA authentication. Postgres HBA
 * check_hostname() does a DNS lookup of the hostname found in the pg_hba.conf
 * file and then compares the IP addresses obtained to the client IP address,
 * and refuses the connection where there's no match.
 */
static void
check_hostname(const char *hostname)
{
	char localIpAddress[INET_ADDRSTRLEN];
	IPType ipType = ip_address_type(hostname);

	if (ipType == IPTYPE_NONE)
	{
		if (!findHostnameLocalAddress(hostname,
									  localIpAddress, INET_ADDRSTRLEN))
		{
			log_warn(
				"Failed to resolve hostname \"%s\" to a local IP address, "
				"automated pg_hba.conf setup might fail.", hostname);
		}
	}
	else
	{
		char cidr[BUFSIZE] = { 0 };
		char ipaddr[BUFSIZE] = { 0 };

		if (!fetchLocalCIDR(hostname, cidr, BUFSIZE))
		{
			log_warn("Failed to find adress \"%s\" in local network "
					 "interfaces, automated pg_hba.conf setup might fail.",
					 hostname);
		}

		bool useHostname = false;

		/* use pghba_check_hostname for log diagnostics */
		(void) pghba_check_hostname(hostname, ipaddr, BUFSIZE, &useHostname);
	}
}
