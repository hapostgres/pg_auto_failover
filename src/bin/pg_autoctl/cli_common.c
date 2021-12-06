/*
 * src/bin/pg_autoctl/cli_common.c
 *     Implementation of a CLI which lets you run individual keeper routines
 *     directly
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>

#include "commandline.h"

#include "cli_common.h"
#include "cli_root.h"
#include "commandline.h"
#include "env_utils.h"
#include "ipaddr.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "parsing.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "pidfile.h"
#include "state.h"
#include "string_utils.h"

/* handle command line options for our setup. */
KeeperConfig keeperOptions;
bool createAndRun = false;
bool outputJSON = false;
bool openAppHBAonLAN = false;
int ssl_flag = 0;

/* stores --node-id, only used with --disable-monitor */
int monitorDisabledNodeId = -1;

/*
 * cli_common_keeper_getopts parses the CLI options for the pg_autoctl create
 * postgres command, and others such as pg_autoctl do discover. An example of a
 * long_options parameter would look like this:
 *
 *	static struct option long_options[] = {
 *		{ "pgctl", required_argument, NULL, 'C' },
 *		{ "pgdata", required_argument, NULL, 'D' },
 *		{ "pghost", required_argument, NULL, 'H' },
 *		{ "pgport", required_argument, NULL, 'p' },
 *		{ "listen", required_argument, NULL, 'l' },
 *		{ "proxyport", required_argument, NULL, 'y' },
 *		{ "username", required_argument, NULL, 'U' },
 *		{ "auth", required_argument, NULL, 'A' },
 *		{ "skip-pg-hba", no_argument, NULL, 'S' },
 *		{ "pg-hba-lan", no_argument, NULL, 'L' },
 *		{ "dbname", required_argument, NULL, 'd' },
 *		{ "name", required_argument, NULL, 'a' },
 *		{ "hostname", required_argument, NULL, 'n' },
 *		{ "formation", required_argument, NULL, 'f' },
 *		{ "group", required_argument, NULL, 'g' },
 *		{ "monitor", required_argument, NULL, 'm' },
 *		{ "node-id", required_argument, NULL, 'I' },
 *		{ "disable-monitor", no_argument, NULL, 'M' },
 *		{ "version", no_argument, NULL, 'V' },
 *		{ "verbose", no_argument, NULL, 'v' },
 *		{ "quiet", no_argument, NULL, 'q' },
 *		{ "help", no_argument, NULL, 'h' },
 *		{ "secondary", no_argument, NULL, 'z' }
 *      { "citus-cluster", required_argument, NULL, 'Z' },
 *		{ "candidate-priority", required_argument, NULL, 'P'},
 *		{ "replication-quorum", required_argument, NULL, 'r'},
 *		{ "maximum-backup-rate", required_argument, NULL, 'R' },
 *		{ "help", no_argument, NULL, 0 },
 *		{ "run", no_argument, NULL, 'x' },
 *      { "ssl-self-signed", no_argument, NULL, 's' },
 *      { "no-ssl", no_argument, NULL, 'N' },
 *      { "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG },
 *      { "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG },
 *      { "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG },
 *      { "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
 *		{ NULL, 0, NULL, 0 }
 *	};
 *
 */
int
cli_common_keeper_getopts(int argc, char **argv,
						  struct option *long_options,
						  const char *optstring,
						  KeeperConfig *options,
						  SSLCommandLineOptions *sslCommandLineOptions)
{
	KeeperConfig LocalOptionConfig = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	/* force some non-zero default values */
	LocalOptionConfig.monitorDisabled = false;
	LocalOptionConfig.groupId = -1;
	LocalOptionConfig.network_partition_timeout = -1;
	LocalOptionConfig.prepare_promotion_catchup = -1;
	LocalOptionConfig.prepare_promotion_walreceiver = -1;
	LocalOptionConfig.postgresql_restart_failure_timeout = -1;
	LocalOptionConfig.postgresql_restart_failure_max_retries = -1;
	LocalOptionConfig.pgSetup.settings.candidatePriority = -1;
	LocalOptionConfig.pgSetup.settings.replicationQuorum =
		FAILOVER_NODE_REPLICATION_QUORUM;

	/* default to a "primary" in citus node_role terms */
	LocalOptionConfig.citusRole = CITUS_ROLE_PRIMARY;

	/* add support for environment variable for some of the options */
	if (env_exists(PG_AUTOCTL_NODE_NAME))
	{
		if (!get_env_copy(PG_AUTOCTL_NODE_NAME,
						  LocalOptionConfig.name,
						  sizeof(LocalOptionConfig.name)))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (env_exists(PG_AUTOCTL_CANDIDATE_PRIORITY))
	{
		char prio[BUFSIZE] = { 0 };

		if (!get_env_copy(PG_AUTOCTL_CANDIDATE_PRIORITY, prio, sizeof(prio)))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}

		int candidatePriority = strtol(prio, NULL, 10);
		if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
		{
			log_fatal("PG_AUTOCTL_CANDIDATE_PRIORITY environment variable is not valid."
					  " Valid values are integers from 0 to 100. ");
			exit(EXIT_CODE_BAD_ARGS);
		}

		LocalOptionConfig.pgSetup.settings.candidatePriority = candidatePriority;
	}

	if (env_exists(PG_AUTOCTL_REPLICATION_QUORUM))
	{
		char quorum[BUFSIZE] = { 0 };

		if (!get_env_copy(PG_AUTOCTL_REPLICATION_QUORUM, quorum, sizeof(quorum)))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}

		bool replicationQuorum = false;
		if (!parse_bool(quorum, &replicationQuorum))
		{
			log_fatal("PG_AUTOCTL_REPLICATION_QUORUM environment variable is not valid."
					  " Valid values are \"true\" or \"false\".");
			exit(EXIT_CODE_BAD_ARGS);
		}

		LocalOptionConfig.pgSetup.settings.replicationQuorum = replicationQuorum;
	}

	optind = 0;

	while ((c = getopt_long(argc, argv,
							optstring, long_options, &option_index)) != -1)
	{
		/*
		 * The switch statement is ready for all the common letters of the
		 * different nodes that `pg_autoctl create` knows how to deal with. The
		 * parameter optstring restrict which letters we are going to actually
		 * parsed, and there's no command that has all of them.
		 */
		switch (c)
		{
			case 'C':
			{
				/* { "pgctl", required_argument, NULL, 'C' } */
				strlcpy(LocalOptionConfig.pgSetup.pg_ctl, optarg, MAXPGPATH);
				log_trace("--pg_ctl %s", LocalOptionConfig.pgSetup.pg_ctl);
				break;
			}

			case 'D':
			{
				/* { "pgdata", required_argument, NULL, 'D' } */
				strlcpy(LocalOptionConfig.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", LocalOptionConfig.pgSetup.pgdata);
				break;
			}

			case 'H':
			{
				/* { "pghost", required_argument, NULL, 'h' } */
				strlcpy(LocalOptionConfig.pgSetup.pghost, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--pghost %s", LocalOptionConfig.pgSetup.pghost);
				break;
			}

			case 'p':
			{
				/* { "pgport", required_argument, NULL, 'p' } */
				if (!stringToInt(optarg, &LocalOptionConfig.pgSetup.pgport))
				{
					log_error("Failed to parse --pgport number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--pgport %d", LocalOptionConfig.pgSetup.pgport);
				break;
			}

			case 'l':
			{
				/* { "listen", required_argument, NULL, 'l' } */
				strlcpy(LocalOptionConfig.pgSetup.listen_addresses, optarg, MAXPGPATH);
				log_trace("--listen %s", LocalOptionConfig.pgSetup.listen_addresses);
				break;
			}

			case 'y':
			{
				/* { "proxyport", required_argument, NULL,'y' } */
				if (!stringToInt(optarg, &LocalOptionConfig.pgSetup.proxyport))
				{
					log_error("Failed to parse --proxyport number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--proxy %d", LocalOptionConfig.pgSetup.proxyport);
				break;
			}

			case 'z':
			{
				/* { "secondary", no_argument, NULL, 'z' } */
				strlcpy(LocalOptionConfig.citusRoleStr, "secondary", NAMEDATALEN);
				LocalOptionConfig.citusRole = CITUS_ROLE_SECONDARY;
				log_trace("--secondary");
				break;
			}

			case 'Z':
			{
				/* { "citus-cluster", required_argument, NULL, 'Z' }, */
				strlcpy(LocalOptionConfig.pgSetup.citusClusterName,
						optarg,
						NAMEDATALEN);
				log_trace("--citus-cluster %s",
						  LocalOptionConfig.pgSetup.citusClusterName);
				break;
			}

			case 'U':
			{
				/* { "username", required_argument, NULL, 'U' } */
				strlcpy(LocalOptionConfig.pgSetup.username, optarg, NAMEDATALEN);
				log_trace("--username %s", LocalOptionConfig.pgSetup.username);
				break;
			}

			case 'A':
			{
				/* { "auth", required_argument, NULL, 'A' }, */
				if (LocalOptionConfig.pgSetup.hbaLevel == HBA_EDIT_SKIP)
				{
					errors++;
					log_error("Please use either --auth or --skip-pg-hba");
				}

				strlcpy(LocalOptionConfig.pgSetup.authMethod, optarg, NAMEDATALEN);
				log_trace("--auth %s", LocalOptionConfig.pgSetup.authMethod);

				if (LocalOptionConfig.pgSetup.hbaLevel == HBA_EDIT_UNKNOWN)
				{
					strlcpy(LocalOptionConfig.pgSetup.hbaLevelStr,
							pgsetup_hba_level_to_string(HBA_EDIT_MINIMAL),
							sizeof(LocalOptionConfig.pgSetup.hbaLevelStr));

					LocalOptionConfig.pgSetup.hbaLevel = HBA_EDIT_MINIMAL;
				}
				break;
			}

			case 'S':
			{
				/* { "skip-pg-hba", required_argument, NULL, 'S' }, */
				if (!IS_EMPTY_STRING_BUFFER(LocalOptionConfig.pgSetup.authMethod))
				{
					errors++;
					log_error("Please use either --auth or --skip-pg-hba");
				}

				/* force default authentication method then */
				strlcpy(LocalOptionConfig.pgSetup.authMethod,
						DEFAULT_AUTH_METHOD,
						NAMEDATALEN);

				strlcpy(LocalOptionConfig.pgSetup.hbaLevelStr,
						pgsetup_hba_level_to_string(HBA_EDIT_SKIP),
						sizeof(LocalOptionConfig.pgSetup.hbaLevelStr));

				LocalOptionConfig.pgSetup.hbaLevel = HBA_EDIT_SKIP;

				log_trace("--skip-pg-hba");
				break;
			}

			case 'L':
			{
				/* { "pg-hba-lan", required_argument, NULL, 'L' }, */
				if (LocalOptionConfig.pgSetup.hbaLevel == HBA_EDIT_SKIP)
				{
					errors++;
					log_error("Please use either --skip-pg-hba or --pg-hba-lan");
				}

				strlcpy(LocalOptionConfig.pgSetup.hbaLevelStr,
						pgsetup_hba_level_to_string(HBA_EDIT_LAN),
						sizeof(LocalOptionConfig.pgSetup.hbaLevelStr));

				LocalOptionConfig.pgSetup.hbaLevel = HBA_EDIT_LAN;

				log_trace("--pg-hba-lan");
				break;
			}

			case 'd':
			{
				/* { "dbname", required_argument, NULL, 'd' } */
				strlcpy(LocalOptionConfig.pgSetup.dbname, optarg, NAMEDATALEN);
				log_trace("--dbname %s", LocalOptionConfig.pgSetup.dbname);
				break;
			}

			case 'a':
			{
				/* { "name", required_argument, NULL, 'a' }, */
				strlcpy(LocalOptionConfig.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", LocalOptionConfig.name);
				break;
			}

			case 'n':
			{
				/* { "hostname", required_argument, NULL, 'n' } */
				strlcpy(LocalOptionConfig.hostname, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--hostname %s", LocalOptionConfig.hostname);
				break;
			}

			case 'f':
			{
				/* { "formation", required_argument, NULL, 'f' } */
				strlcpy(LocalOptionConfig.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", LocalOptionConfig.formation);
				break;
			}

			case 'g':
			{
				/* { "group", required_argument, NULL, 'g' } */
				if (!stringToInt(optarg, &LocalOptionConfig.groupId))
				{
					log_fatal("--group argument is not a valid group ID: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--group %d", LocalOptionConfig.groupId);
				break;
			}

			case 'm':
			{
				/* { "monitor", required_argument, NULL, 'm' } */
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(LocalOptionConfig.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", LocalOptionConfig.monitor_pguri);
				break;
			}

			case 'M':
			{
				/* { "disable-monitor", required_argument, NULL, 'M' }, */
				LocalOptionConfig.monitorDisabled = true;
				log_trace("--disable-monitor");
				break;
			}

			case 'I':
			{
				/* { "node-id", required_argument, NULL, 'I' }, */
				if (!stringToInt(optarg, &monitorDisabledNodeId))
				{
					log_fatal("--node-id argument is not a valid ID: \"%s\"",
							  optarg);
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--node-id %d", monitorDisabledNodeId);
				break;
			}

			case 'P':
			{
				/* { "candidate-priority", required_argument, NULL, 'P'} */
				int candidatePriority = strtol(optarg, NULL, 10);
				if (errno == EINVAL || candidatePriority < 0 || candidatePriority > 100)
				{
					log_fatal("--candidate-priority argument is not valid."
							  " Valid values are integers from 0 to 100. ");
					exit(EXIT_CODE_BAD_ARGS);
				}

				LocalOptionConfig.pgSetup.settings.candidatePriority = candidatePriority;
				log_trace("--candidate-priority %d", candidatePriority);
				break;
			}

			case 'r':
			{
				/* { "replication-quorum", required_argument, NULL, 'r'} */
				bool replicationQuorum = false;

				if (!parse_bool(optarg, &replicationQuorum))
				{
					log_fatal("--replication-quorum argument is not valid."
							  " Valid values are \"true\" or \"false\".");
					exit(EXIT_CODE_BAD_ARGS);
				}

				LocalOptionConfig.pgSetup.settings.replicationQuorum = replicationQuorum;
				log_trace("--replication-quorum %s", boolToString(replicationQuorum));
				break;
			}

			case 'R':
			{
				/* { "maximum-backup-rate", required_argument, NULL, 'R' } */
				strlcpy(LocalOptionConfig.maximum_backup_rate, optarg,
						MAXIMUM_BACKUP_RATE_LEN);
				log_trace("--maximum-backup-rate %s",
						  LocalOptionConfig.maximum_backup_rate);
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
												   *sslCommandLineOptions))
				{
					errors++;
					break;
				}
				*sslCommandLineOptions = SSL_CLI_SELF_SIGNED;

				LocalOptionConfig.pgSetup.ssl.active = 1;
				LocalOptionConfig.pgSetup.ssl.createSelfSignedCert = true;
				log_trace("--ssl-self-signed");
				break;
			}

			case 'N':
			{
				/* { "no-ssl", no_argument, NULL, 'N' }, */
				if (!cli_getopt_accept_ssl_options(SSL_CLI_NO_SSL,
												   *sslCommandLineOptions))
				{
					errors++;
					break;
				}
				*sslCommandLineOptions = SSL_CLI_NO_SSL;

				LocalOptionConfig.pgSetup.ssl.active = 0;
				LocalOptionConfig.pgSetup.ssl.createSelfSignedCert = false;
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
													   *sslCommandLineOptions))
					{
						errors++;
						break;
					}

					*sslCommandLineOptions = SSL_CLI_USER_PROVIDED;
					LocalOptionConfig.pgSetup.ssl.active = 1;
				}

				if (!cli_getopt_ssl_flags(ssl_flag,
										  optarg,
										  &(LocalOptionConfig.pgSetup)))
				{
					errors++;
				}
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

	/* check --disable-monitor and --node-id */
	if (LocalOptionConfig.monitorDisabled && monitorDisabledNodeId == -1)
	{
		log_fatal("When using --disable-monitor, also use --node-id");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!LocalOptionConfig.monitorDisabled && monitorDisabledNodeId != -1)
	{
		log_fatal("Option --node-id is only accepted with --disable-monitor");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* check --secondary and --candidate-priority */
	if (LocalOptionConfig.pgSetup.settings.candidatePriority == -1)
	{
		/* --candidate-priority has not been used */
		if (LocalOptionConfig.citusRole == CITUS_ROLE_SECONDARY)
		{
			/* a Citus secondary can't be a target for failover */
			LocalOptionConfig.pgSetup.settings.candidatePriority = 0;
		}
		else
		{
			/* here we install the default candidate priority */
			LocalOptionConfig.pgSetup.settings.candidatePriority =
				FAILOVER_NODE_CANDIDATE_PRIORITY;
		}
	}
	else if (LocalOptionConfig.pgSetup.settings.candidatePriority > 0 &&
			 LocalOptionConfig.citusRole == CITUS_ROLE_SECONDARY)
	{
		log_fatal("Citus does not support secondary roles that are "
				  "also a candidate for failover: please use --secondary "
				  "with --candidate-priority 0");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* a --secondary citus worker requires a cluster name */
	if (LocalOptionConfig.citusRole == CITUS_ROLE_SECONDARY)
	{
		if (IS_EMPTY_STRING_BUFFER(LocalOptionConfig.pgSetup.citusClusterName))
		{
			log_fatal("When using --secondary, also use --citus-cluster");
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* the default HBA editing level is MINIMAL, time to install it */
	if (LocalOptionConfig.pgSetup.hbaLevel == HBA_EDIT_UNKNOWN)
	{
		strlcpy(LocalOptionConfig.pgSetup.hbaLevelStr,
				pgsetup_hba_level_to_string(HBA_EDIT_MINIMAL),
				sizeof(LocalOptionConfig.pgSetup.hbaLevelStr));

		LocalOptionConfig.pgSetup.hbaLevel = HBA_EDIT_MINIMAL;
	}

	/*
	 * Now, all commands need PGDATA validation.
	 */
	cli_common_get_set_pgdata_or_exit(&(LocalOptionConfig.pgSetup));

	/*
	 * We have a PGDATA setting, prepare our configuration pathnames from it.
	 */
	if (!keeper_config_set_pathnames_from_pgdata(
			&(LocalOptionConfig.pathnames), LocalOptionConfig.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing now */
	*options = LocalOptionConfig;

	return optind;
}


/*
 * cli_create_node_getopts parses the CLI options for the pg_autoctl create
 * command. An example of a long_options parameter would look like this:
 *
 *	static struct option long_options[] = {
 *		{ "pgctl", required_argument, NULL, 'C' },
 *		{ "pgdata", required_argument, NULL, 'D' },
 *		{ "pghost", required_argument, NULL, 'H' },
 *		{ "pgport", required_argument, NULL, 'p' },
 *		{ "listen", required_argument, NULL, 'l' },
 *		{ "proxyport", required_argument, NULL, 'y' },
 *		{ "username", required_argument, NULL, 'U' },
 *		{ "auth", required_argument, NULL, 'A' },
 *		{ "skip-pg-hba", no_argument, NULL, 'S' },
 *		{ "pg-hba-lan", no_argument, NULL, 'L' },
 *		{ "dbname", required_argument, NULL, 'd' },
 *		{ "hostname", required_argument, NULL, 'n' },
 *		{ "formation", required_argument, NULL, 'f' },
 *		{ "group", required_argument, NULL, 'g' },
 *		{ "monitor", required_argument, NULL, 'm' },
 *		{ "disable-monitor", no_argument, NULL, 'M' },
 *		{ "version", no_argument, NULL, 'V' },
 *		{ "verbose", no_argument, NULL, 'v' },
 *		{ "quiet", no_argument, NULL, 'q' },
 *		{ "help", no_argument, NULL, 'h' },
 *		{ "secondary", no_argument, NULL, 'z' },
 *      { "citus-cluster", required_argument, NULL, 'Z' },
 *		{ "candidate-priority", required_argument, NULL, 'P'},
 *		{ "replication-quorum", required_argument, NULL, 'r'},
 *		{ "help", no_argument, NULL, 0 },
 *		{ "run", no_argument, NULL, 'x' },
 *      { "ssl-self-signed", no_argument, NULL, 's' },
 *      { "no-ssl", no_argument, NULL, 'N' },
 *      { "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG },
 *      { "server-crt", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG },
 *      { "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG },
 *      { "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
 *		{ NULL, 0, NULL, 0 }
 *	};
 *
 */
int
cli_create_node_getopts(int argc, char **argv,
						struct option *long_options,
						const char *optstring,
						KeeperConfig *options)
{
	SSLCommandLineOptions sslCommandLineOptions = SSL_CLI_UNKNOWN;

	optind = cli_common_keeper_getopts(argc, argv,
									   long_options, optstring,
									   options, &sslCommandLineOptions);

	/*
	 * We require the user to specify an authentication mechanism, or to use
	 * ---skip-pg-hba. Our documentation tutorial will use --auth trust, and we
	 * should make it obvious that this is not the right choice for production.
	 */
	if (IS_EMPTY_STRING_BUFFER(options->pgSetup.authMethod))
	{
		log_fatal("Please use either --auth trust|md5|... or --skip-pg-hba");
		log_info("pg_auto_failover can be set to edit Postgres HBA rules "
				 "automatically when needed. For quick testing '--auth trust' "
				 "makes it easy to get started, "
				 "consider another authentication mechanism for production.");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * If we have --ssl-self-signed, we don't want to have --ssl-ca-file and
	 * others in use anywhere. If we have --no-ssl, same thing. If we have the
	 * SSL files setup, we want to have neither --ssl-self-signed nor the other
	 * SSL files specified.
	 *
	 * We also need to either use the given sslMode or compute our default.
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

	if (!pgsetup_validate_ssl_settings(&(options->pgSetup)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * You can't both have a monitor a use --disable-monitor.
	 */
	if (!IS_EMPTY_STRING_BUFFER(options->monitor_pguri) &&
		options->monitorDisabled)
	{
		log_fatal("Use either --monitor or --disable-monitor, not both.");
		exit(EXIT_CODE_BAD_ARGS);
	}
	else if (!options->monitorDisabled)
	{
		if (IS_EMPTY_STRING_BUFFER(options->monitor_pguri) &&
			!(env_exists(PG_AUTOCTL_MONITOR) &&
			  get_env_copy(PG_AUTOCTL_MONITOR,
						   options->monitor_pguri,
						   sizeof(options->monitor_pguri))))
		{
			log_fatal("Failed to set the monitor URI: "
					  "use either --monitor postgresql://... "
					  "or --disable-monitor");
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
	else if (options->monitorDisabled)
	{
		/*
		 * We must be able to restore this setup from the configuration file,
		 * and for that we set the pg_autoctl.monitor URI in the file to the
		 * "magic" value PG_AUTOCTL_DISABLED.
		 */
		strlcpy(options->monitor_pguri,
				PG_AUTOCTL_MONITOR_DISABLED,
				MAXCONNINFO);
	}

	return optind;
}


/*
 * cli_getopt_accept_ssl_options compute if we can accept the newSSLoption
 * (such as --no-ssl or --ssl-ca-file) given the previous one we have already
 * accepted.
 */
bool
cli_getopt_accept_ssl_options(SSLCommandLineOptions newSSLOption,
							  SSLCommandLineOptions currentSSLOptions)
{
	if (currentSSLOptions == SSL_CLI_UNKNOWN)
	{
		/* first SSL option being parsed */
		return true;
	}

	if (currentSSLOptions != newSSLOption)
	{
		if (currentSSLOptions == SSL_CLI_USER_PROVIDED ||
			newSSLOption == SSL_CLI_USER_PROVIDED)
		{
			log_error(
				"Using either --no-ssl or --ssl-self-signed "
				"with user-provided SSL certificates "
				"is not supported");
			return false;
		}

		/*
		 * At this point we know that currentSSLOptions and newSSLOption are
		 * different and none of them are SSL_CLI_USER_PROVIDED.
		 */
		log_error("Using both --no-ssl and --ssl-self-signed "
				  "is not supported");
		return false;
	}

	return true;
}


/*
 * cli_getopt_ssl_flags parses the SSL related options from the command line.
 *
 * { "ssl-ca-file", required_argument, &ssl_flag, SSL_CA_FILE_FLAG }
 * { "ssl-crl-file", required_argument, &ssl_flag, SSL_CRL_FILE_FLAG }
 * { "server-cert", required_argument, &ssl_flag, SSL_SERVER_CRT_FLAG }
 * { "server-key", required_argument, &ssl_flag, SSL_SERVER_KEY_FLAG }
 * { "ssl-mode", required_argument, &ssl_flag, SSL_MODE_FLAG },
 *
 * As those options are not using any short option (one-char) variant, they all
 * fall in the case 0, and we can process them thanks to the global variable
 * ssl_flag, an int.
 */
bool
cli_getopt_ssl_flags(int ssl_flag, char *optarg, PostgresSetup *pgSetup)
{
	switch (ssl_flag)
	{
		case SSL_CA_FILE_FLAG:
		{
			strlcpy(pgSetup->ssl.caFile, optarg, MAXPGPATH);
			log_trace("--ssl-ca-file %s", pgSetup->ssl.caFile);
			break;
		}

		case SSL_CRL_FILE_FLAG:
		{
			strlcpy(pgSetup->ssl.crlFile, optarg, MAXPGPATH);
			log_trace("--ssl-crl-file %s", pgSetup->ssl.crlFile);
			break;
		}

		case SSL_SERVER_CRT_FLAG:
		{
			strlcpy(pgSetup->ssl.serverCert, optarg, MAXPGPATH);
			log_trace("--server-cert %s", pgSetup->ssl.serverCert);
			break;
		}

		case SSL_SERVER_KEY_FLAG:
		{
			strlcpy(pgSetup->ssl.serverKey, optarg, MAXPGPATH);
			log_trace("--server-key %s", pgSetup->ssl.serverKey);
			break;
		}

		case SSL_MODE_FLAG:
		{
			strlcpy(pgSetup->ssl.sslModeStr, optarg, SSL_MODE_STRLEN);
			pgSetup->ssl.sslMode = pgsetup_parse_sslmode(optarg);

			log_trace("--ssl-mode %s",
					  pgsetup_sslmode_to_string(pgSetup->ssl.sslMode));

			if (pgSetup->ssl.sslMode == SSL_MODE_UNKNOWN)
			{
				log_fatal("Failed to parse ssl mode \"%s\"", optarg);
				return false;
			}
			break;
		}

		default:
		{
			log_fatal("BUG: unknown ssl flag value: %d", ssl_flag);
			return false;
		}
	}
	return true;
}


/*
 * cli_common_get_set_pgdata_or_exit gets pgdata from either --pgdata or PGDATA
 * in the environment, and when we have a value for it, then we set it in the
 * environment.
 */
void
cli_common_get_set_pgdata_or_exit(PostgresSetup *pgSetup)
{
	/* if --pgdata is not given, fetch PGDATA from the environment or exit */
	if (IS_EMPTY_STRING_BUFFER(pgSetup->pgdata))
	{
		get_env_pgdata_or_exit(pgSetup->pgdata);
	}
	else
	{
		/* from now on want PGDATA set in the environment */
		setenv("PGDATA", pgSetup->pgdata, 1);
	}
}


/*
 * keeper_cli_getopt_pgdata gets the PGDATA options or environment variable,
 * either of those must be set for all of pg_autoctl's commands. This parameter
 * allows to know which PostgreSQL instance we are the keeper of, and also
 * allows to determine where is our configuration file.
 */
int
cli_getopt_pgdata(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
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

	while ((c = getopt_long(argc, argv, "D:JVvqh",
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

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				printVersion = true;
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

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/* now that we have the command line parameters, prepare the options */
	(void) prepare_keeper_options(&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * prepare_keeper_options finishes the preparation of the keeperOptions that
 * hosts the command line options.
 */
void
prepare_keeper_options(KeeperConfig *options)
{
	cli_common_get_set_pgdata_or_exit(&(options->pgSetup));

	log_debug("Managing PostgreSQL installation at \"%s\"",
			  options->pgSetup.pgdata);

	if (!keeper_config_set_pathnames_from_pgdata(&options->pathnames,
												 options->pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/*
	 * The function keeper_cli_getopt_pgdata is only used by commands needing a
	 * configuration file to already exists:
	 *
	 * - `pg_autoctl do ...` are coded is a way that they don't need a
	 *    configuration file, instead using their own command line options
	 *    parser, so that test files specify the options on the command line,
	 *    making it easier to maintain,
	 *
	 * - `pg_autoctl config|create|run` are using this function
	 *   keeper_cli_getopt_pgdata and expect the configuration file to exists.
	 *
	 * A typo in PGDATA might be responsible for a failure that is hard to
	 * understand later, because of the way to derive the configuration
	 * filename from the PGDATA value. So we're going to go a little out of our
	 * way and be helpful to the user.
	 */
	if (!file_exists(options->pathnames.config))
	{
		log_fatal("Expected configuration file does not exist: \"%s\"",
				  options->pathnames.config);

		if (!directory_exists(options->pgSetup.pgdata))
		{
			log_warn("HINT: Check your PGDATA setting: \"%s\"",
					 options->pgSetup.pgdata);
		}

		exit(EXIT_CODE_BAD_ARGS);
	}
}


/*
 * set_first_pgctl sets the first pg_ctl found in PATH to given KeeperConfig.
 */
void
set_first_pgctl(PostgresSetup *pgSetup)
{
	/* first, use PG_CONFIG when it exists in the environment */
	if (set_pg_ctl_from_PG_CONFIG(pgSetup))
	{
		return;
	}

	/* then, use PATH and fetch the first entry there for the monitor */
	if (search_path_first("pg_ctl", pgSetup->pg_ctl, LOG_WARN))
	{
		if (!pg_ctl_version(pgSetup))
		{
			/* errors have been logged in pg_ctl_version */
			exit(EXIT_CODE_PGCTL);
		}

		return;
	}

	/* then, use PATH and fetch pg_config --bindir from there */
	if (set_pg_ctl_from_pg_config(pgSetup))
	{
		return;
	}

	/* at this point we don't have any other ways to find a pg_ctl */
	exit(EXIT_CODE_PGCTL);
}


/*
 * monitor_init_from_pgsetup might be called either from a monitor or
 * a keeper node.
 *
 * First, see if we are on a keeper node with a configuration file for given
 * PGDATA. If that's the case, then we'll use the pg_autoctl.monitor_pguri
 * setting from there to contact the monitor.
 *
 * Then, if we failed to get the monitor's uri from a keeper's configuration
 * file, probe the given PGDATA to see if there's a running PostgreSQL instance
 * there, and if that's the case consider it's a monitor, and build its
 * connection string from discovered PostgreSQL parameters.
 */
bool
monitor_init_from_pgsetup(Monitor *monitor, PostgresSetup *pgSetup)
{
	ConfigFilePaths pathnames = { 0 };

	if (!keeper_config_set_pathnames_from_pgdata(&pathnames, pgSetup->pgdata))
	{
		/* errors have already been logged */
		return false;
	}

	switch (ProbeConfigurationFileRole(pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			bool missingPgdataIsOk = false;
			bool pgIsNotRunningIsOk = false;
			char connInfo[MAXCONNINFO];
			MonitorConfig mconfig = { 0 };

			if (!monitor_config_init_from_pgsetup(&mconfig, pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			pg_setup_get_local_connection_string(&mconfig.pgSetup, connInfo);
			monitor_init(monitor, connInfo);

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			KeeperConfig config = { 0 };
			Keeper keeper;
			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;
			bool monitorDisabledIsOk = false;

			log_trace("monitor_init_from_pgsetup: keeper");

			/*
			 * the dereference of pgSetup is safe as it only contains literals,
			 * no pointers. keeper_config_read_file expects pgSetup to be set.
			 */
			config.pgSetup = *pgSetup;
			config.pathnames = pathnames;

			/*
			 * All we need here is a pg_autoctl.monitor URI to connect to. We
			 * don't need that the local PostgreSQL instance has been created
			 * already.
			 */
			if (!keeper_config_read_file(&config,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			if (config.monitorDisabled)
			{
				log_error("This node has disabled monitor, "
						  "pg_autoctl get and set commands are not available.");
				return false;
			}

			if (!monitor_init(&(keeper.monitor), config.monitor_pguri))
			{
				return false;
			}

			*monitor = keeper.monitor;
			*pgSetup = config.pgSetup;
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"", pathnames.config);
			return false;
		}
	}

	return true;
}


/*
 * exit_unless_role_is_keeper exits when the configured role for the local node
 * is not a pg_autoctl keeper, meaning either we fail to parse the
 * configuration file (maybe it doesn't exists), or we parse it correctly and
 * pg_autoctl.role is "monitor".
 */
void
exit_unless_role_is_keeper(KeeperConfig *kconfig)
{
	if (!keeper_config_set_pathnames_from_pgdata(&kconfig->pathnames,
												 kconfig->pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (ProbeConfigurationFileRole(kconfig->pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			log_fatal("The command `%s` does not apply to a monitor node.",
					  current_command->breadcrumb);
			exit(EXIT_CODE_BAD_CONFIG);
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			/* pg_autoctl.role is as expected, we may continue */
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  kconfig->pathnames.config);
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
}


/*
 * Provide help.
 */
void
keeper_cli_help(int argc, char **argv)
{
	CommandLine command = root;

	if (env_exists(PG_AUTOCTL_DEBUG))
	{
		command = root_with_debug;
	}

	(void) commandline_print_command_tree(&command, stdout);
}


/*
 * cli_print_version_getopts parses the CLI options for the pg_autoctl version
 * command, which are the usual suspects.
 */
int
cli_print_version_getopts(int argc, char **argv)
{
	int c, option_index = 0;

	static struct option long_options[] = {
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
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

	while ((c = getopt_long(argc, argv, "JVvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
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
				/*
				 * Ignore errors, ignore most of the things, just print the
				 * version and exit(0)
				 */
				break;
			}
		}
	}
	return optind;
}


/*
 * keeper_cli_print_version prints the pg_autoctl version and exits with
 * successful exit code of zero.
 */
void
keeper_cli_print_version(int argc, char **argv)
{
	const char *version = PG_AUTOCTL_VERSION;

	if (outputJSON)
	{
		JSON_Value *js = json_value_init_object();
		JSON_Object *root = json_value_get_object(js);

		json_object_set_string(root, "pg_autoctl", version);
		json_object_set_string(root,
							   "pgautofailover", PG_AUTOCTL_EXTENSION_VERSION);
		json_object_set_string(root, "pg_major", PG_MAJORVERSION);
		json_object_set_string(root, "pg_version", PG_VERSION);
		json_object_set_string(root, "pg_version_str", PG_VERSION_STR);
		json_object_set_number(root, "pg_version_num", (double) PG_VERSION_NUM);

		(void) cli_pprint_json(js);
	}
	else
	{
		fformat(stdout, "pg_autoctl version %s\n", version);
		fformat(stdout,
				"pg_autoctl extension version %s\n",
				PG_AUTOCTL_EXTENSION_VERSION);
		fformat(stdout, "compiled with %s\n", PG_VERSION_STR);
		fformat(stdout, "compatible with Postgres 10, 11, 12, 13, and 14\n");
	}

	exit(0);
}


/*
 * cli_pprint_json pretty prints the given JSON value to stdout and frees the
 * JSON related memory.
 */
void
cli_pprint_json(JSON_Value *js)
{
	/* output our nice JSON object, pretty printed please */
	char *serialized_string = json_serialize_to_string_pretty(js);
	fformat(stdout, "%s\n", serialized_string);

	/* free intermediate memory */
	json_free_serialized_string(serialized_string);
	json_value_free(js);
}


/*
 * logLevelToString returns the string to use to enable the same logLevel in a
 * sub-process.
 *
 * enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };
 */
char *
logLevelToString(int logLevel)
{
	switch (logLevel)
	{
		case LOG_TRACE:
		{
			return "-vvv";
		}

		case LOG_DEBUG:
		{
			return "-vv";
		}

		case LOG_WARN:
		case LOG_INFO:
		{
			return "-v";
		}

		case LOG_ERROR:
		case LOG_FATAL:
		{
			return "-q";
		}
	}

	return "";
}


/*
 * cli_common_pgsetup_init prepares a pgSetup instance from either a keeper or
 * a monitor configuration file.
 */
bool
cli_common_pgsetup_init(ConfigFilePaths *pathnames, PostgresSetup *pgSetup)
{
	KeeperConfig kconfig = keeperOptions;

	if (!keeper_config_set_pathnames_from_pgdata(&(kconfig.pathnames),
												 kconfig.pgSetup.pgdata))
	{
		/* errors have already been logged */
		return false;
	}

	/* copy the pathnames over to the caller */
	*pathnames = kconfig.pathnames;

	switch (ProbeConfigurationFileRole(kconfig.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			MonitorConfig mconfig = { 0 };

			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;

			if (!monitor_config_init_from_pgsetup(&mconfig,
												  &kconfig.pgSetup,
												  missingPgdataIsOk,
												  pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			/* copy the pgSetup from the config to the Local Postgres instance */
			*pgSetup = mconfig.pgSetup;

			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool missingPgdataIsOk = true;
			bool pgIsNotRunningIsOk = true;
			bool monitorDisabledIsOk = true;

			if (!keeper_config_read_file(&kconfig,
										 missingPgdataIsOk,
										 pgIsNotRunningIsOk,
										 monitorDisabledIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			/* copy the pgSetup from the config to the Local Postgres instance */
			*pgSetup = kconfig.pgSetup;

			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  kconfig.pathnames.config);
			return false;
		}
	}

	return true;
}


/*
 * cli_common_ensure_formation reads the formation name from the configuration
 * file where it's not been given on the command line. When the local node is a
 * monitor, the target formation should be found on the command line with the
 * option --formation, otherwise we default to FORMATION_DEFAULT.
 */
bool
cli_common_ensure_formation(KeeperConfig *options)
{
	/* if --formation has been used, we're good */
	if (!IS_EMPTY_STRING_BUFFER(options->formation))
	{
		return true;
	}

	/*
	 * When --monitor has been used rather than --pgdata, we are operating at a
	 * distance and we don't expect a configuration file to exist.
	 */
	if (IS_EMPTY_STRING_BUFFER(options->pgSetup.pgdata))
	{
		strlcpy(options->formation,
				FORMATION_DEFAULT,
				sizeof(options->formation));

		return true;
	}

	switch (ProbeConfigurationFileRole(options->pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			/* on a monitor node, default to using the "default" formation */
			strlcpy(options->formation,
					FORMATION_DEFAULT,
					sizeof(options->formation));
			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			KeeperConfig config = { 0 };
			bool monitorDisabledIsOk = true;

			/* copy the pathnames to our temporary config struct */
			config.pathnames = options->pathnames;

			if (!keeper_config_read_file_skip_pgsetup(&config,
													  monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}

			strlcpy(options->formation,
					config.formation,
					sizeof(options->formation));

			log_debug("Using --formation \"%s\"", options->formation);
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  options->pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
	return true;
}


/*
 * cli_pg_autoctl_reload signals the pg_autoctl process to reload its
 * configuration by sending it the SIGHUP signal.
 */
bool
cli_pg_autoctl_reload(const char *pidfile)
{
	pid_t pid;

	if (read_pidfile(pidfile, &pid))
	{
		if (pid <= 0)
		{
			log_error("Failed to reload pg_autoctl: "
					  "pid file \"%s\" contains negative-or-zero pid %d",
					  pidfile, pid);
			return false;
		}

		if (kill(pid, SIGHUP) != 0)
		{
			log_error("Failed to send SIGHUP to the pg_autoctl's pid %d: %m",
					  pid);
			return false;
		}
	}

	return true;
}


/*
 * cli_node_metadata_getopts parses the command line options for the
 * pg_autoctl set node metadata command.
 */
int
cli_node_metadata_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "name", required_argument, NULL, 'n' },
		{ "hostname", required_argument, NULL, 'H' },
		{ "pgport", required_argument, NULL, 'p' },
		{ "json", no_argument, NULL, 'J' },
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

	while ((c = getopt_long(argc, argv, "D:n:H:p:JVvqh",
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

			case 'H':
			{
				/* { "hostname", required_argument, NULL, 'h' } */
				strlcpy(options.hostname, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--hostname %s", options.hostname);
				break;
			}

			case 'n':
			{
				/* { "name", required_argument, NULL, 'n' } */
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
				break;
			}

			case 'p':
			{
				/* { "pgport", required_argument, NULL, 'p' } */
				if (!stringToInt(optarg, &options.pgSetup.pgport))
				{
					log_error("Failed to parse --pgport number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--pgport %d", options.pgSetup.pgport);
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

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
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

	/*
	 * Now, all commands need PGDATA validation.
	 */
	cli_common_get_set_pgdata_or_exit(&(options.pgSetup));

	/*
	 * We have a PGDATA setting, prepare our configuration pathnames from it.
	 */
	if (!keeper_config_set_pathnames_from_pgdata(&(options.pathnames),
												 options.pgSetup.pgdata))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing now */
	keeperOptions = options;

	return optind;
}


/*
 * cli_get_name_getopts parses the command line options for the command
 * `pg_autoctl get|set` commands and the `pg_autoctl perform promotion`
 * command, a list of commands which needs to target a node given by name.
 */
int
cli_get_name_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "formation", required_argument, NULL, 'f' },
		{ "name", required_argument, NULL, 'a' },
		{ "json", no_argument, NULL, 'J' },
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

	while ((c = getopt_long(argc, argv, "D:f:g:n:Vvqh",
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

			case 'm':
			{
				if (!validate_connection_string(optarg))
				{
					log_fatal("Failed to parse --monitor connection string, "
							  "see above for details.");
					exit(EXIT_CODE_BAD_ARGS);
				}
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
				break;
			}

			case 'f':
			{
				strlcpy(options.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.formation);
				break;
			}

			case 'a':
			{
				/* { "name", required_argument, NULL, 'a' }, */
				strlcpy(options.name, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--name %s", options.name);
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

			case 'J':
			{
				outputJSON = true;
				log_trace("--json");
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

	/* now that we have the command line parameters, prepare the options */
	/* when we have a monitor URI we don't need PGDATA */
	if (cli_use_monitor_option(&options))
	{
		if (!IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
		{
			log_warn("Given --monitor URI, the --pgdata option is ignored");
			log_info("Connecting to monitor at \"%s\"", options.monitor_pguri);

			/* the rest of the program needs pgdata actually empty */
			bzero((void *) options.pgSetup.pgdata,
				  sizeof(options.pgSetup.pgdata));
		}
	}
	else
	{
		(void) prepare_keeper_options(&options);
	}

	/* ensure --formation, or get it from the configuration file */
	if (!cli_common_ensure_formation(&options))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_use_monitor_option returns true when the --monitor option should be
 * used, or when PG_AUTOCTL_MONITOR has been set in the environment. In that
 * case the options->monitor_pguri is also set to the value found in the
 * environment.
 */
bool
cli_use_monitor_option(KeeperConfig *options)
{
	/* if --monitor is used, then use it */
	if (!IS_EMPTY_STRING_BUFFER(options->monitor_pguri))
	{
		return true;
	}

	/* otherwise, have a look at the PG_AUTOCTL_MONITOR environment variable */
	if (env_exists(PG_AUTOCTL_MONITOR) &&
		get_env_copy(PG_AUTOCTL_MONITOR,
					 options->monitor_pguri,
					 sizeof(options->monitor_pguri)) &&
		!IS_EMPTY_STRING_BUFFER(options->monitor_pguri))
	{
		log_debug("Using environment PG_AUTOCTL_MONITOR \"%s\"",
				  options->monitor_pguri);
		return true;
	}

	/*
	 * Still nothing? well don't use --monitor then.
	 *
	 * Now, on commands that are compatible with using just a monitor and no
	 * local pg_autoctl node, we want to include an error message about the
	 * lack of a --monitor when we also lack --pgdata.
	 */
	if (IS_EMPTY_STRING_BUFFER(options->pgSetup.pgdata) &&
		!env_exists("PGDATA"))
	{
		log_error("Failed to get value for environment variable '%s', "
				  "which is unset", PG_AUTOCTL_MONITOR);
		log_warn("This command also supports the --monitor option, which "
				 "is not used here");
	}

	return false;
}


/*
 * cli_monitor_init_from_option_or_config initialises a monitor connection
 * either from the --monitor Postgres URI given on the command line, or from
 * the configuration file of the local node (monitor or keeper).
 */
void
cli_monitor_init_from_option_or_config(Monitor *monitor, KeeperConfig *kconfig)
{
	if (IS_EMPTY_STRING_BUFFER(kconfig->monitor_pguri))
	{
		if (!monitor_init_from_pgsetup(monitor, &(kconfig->pgSetup)))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_CONFIG);
		}
	}
	else
	{
		if (!monitor_init(monitor, kconfig->monitor_pguri))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}


/*
 * cli_ensure_node_name ensures that we have a node name to continue with,
 * either from the command line itself, or from the configuration file when
 * we're dealing with a keeper node.
 */
void
cli_ensure_node_name(Keeper *keeper)
{
	/* if we have a --name option, we're done already */
	if (!IS_EMPTY_STRING_BUFFER(keeper->config.name))
	{
		return;
	}

	/* we might have --monitor instead of --pgdata */
	if (IS_EMPTY_STRING_BUFFER(keeper->config.pgSetup.pgdata))
	{
		log_fatal("Please use either --name or --pgdata "
				  "to target a specific node");
		exit(EXIT_CODE_BAD_ARGS);
	}

	switch (ProbeConfigurationFileRole(keeper->config.pathnames.config))
	{
		case PG_AUTOCTL_ROLE_MONITOR:
		{
			log_fatal("Please use --name to target a specific node");
			exit(EXIT_CODE_BAD_ARGS);
			break;
		}

		case PG_AUTOCTL_ROLE_KEEPER:
		{
			bool monitorDisabledIsOk = false;

			if (!keeper_config_read_file_skip_pgsetup(&(keeper->config),
													  monitorDisabledIsOk))
			{
				/* errors have already been logged */
				exit(EXIT_CODE_BAD_CONFIG);
			}
			break;
		}

		default:
		{
			log_fatal("Unrecognized configuration file \"%s\"",
					  keeper->config.pathnames.config);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}


/*
 * cli_set_groupId sets the kconfig.groupId depending on the --group argument
 * given on the command line, and if that was not given then figures it out:
 *
 * - it could be that we have a single group in the formation, in that case
 *   --group must be zero, so we set it that way,
 *
 * - we may have a local keeper node setup thanks to --pgdata, in that case
 *   read the configuration file and grab the groupId from there.
 */
void
cli_set_groupId(Monitor *monitor, KeeperConfig *kconfig)
{
	int groupsCount = 0;

	if (!monitor_count_groups(monitor, kconfig->formation, &groupsCount))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (groupsCount == 0)
	{
		/* nothing to be done here */
		log_fatal("The monitor currently has no Postgres nodes "
				  "registered in formation \"%s\"",
				  kconfig->formation);
		exit(EXIT_CODE_BAD_STATE);
	}

	/*
	 * When --group was not given, we may proceed when there is only one
	 * possible target group in the formation, which is the case with Postgres
	 * standalone setups.
	 */
	if (kconfig->groupId == -1)
	{
		/*
		 * When --group is not given and we have a keeper node, we can grab a
		 * default from the configuration file. We have to support the usage
		 * either --monitor or --pgdata. We have a local keeper node/role only
		 * when we have been given --pgdata.
		 */
		if (!IS_EMPTY_STRING_BUFFER(kconfig->pgSetup.pgdata))
		{
			pgAutoCtlNodeRole role =
				ProbeConfigurationFileRole(kconfig->pathnames.config);

			if (role == PG_AUTOCTL_ROLE_KEEPER)
			{
				const bool missingPgdataIsOk = true;
				const bool pgIsNotRunningIsOk = true;
				const bool monitorDisabledIsOk = false;

				if (!keeper_config_read_file(kconfig,
											 missingPgdataIsOk,
											 pgIsNotRunningIsOk,
											 monitorDisabledIsOk))
				{
					/* errors have already been logged */
					exit(EXIT_CODE_BAD_CONFIG);
				}

				log_info("Targetting group %d in formation \"%s\"",
						 kconfig->groupId,
						 kconfig->formation);
			}
		}
	}

	/*
	 * We tried to see if we have a local keeper configuration to grab the
	 * groupId from, what if we don't have a local setup, or the local setup is
	 * not a keeper role.
	 */
	if (kconfig->groupId == -1)
	{
		if (groupsCount == 1)
		{
			/* we have only one group, it's group number zero, proceed */
			kconfig->groupId = 0;
			kconfig->pgSetup.pgKind = NODE_KIND_STANDALONE;
		}
		else
		{
			log_error("Please use the --group option to target a "
					  "specific group in formation \"%s\"",
					  kconfig->formation);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}
}
