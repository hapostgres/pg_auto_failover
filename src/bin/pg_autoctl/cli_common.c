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

#include "cli_common.h"
#include "commandline.h"
#include "ipaddr.h"
#include "keeper.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "monitor_config.h"
#include "pgsetup.h"
#include "pgsql.h"
#include "state.h"

/* handle command line options for our setup. */
KeeperConfig keeperOptions;
bool allowRemovingPgdata = false;
bool skipPgHba = false;


/*
 * cli_create_node_getopts parses the CLI options for the pg_autoctl create
 * command. An example of a long_options parameter would look like this:
 *
 *	static struct option long_options[] = {
 *		{ "pgctl", required_argument, NULL, 'C' },
 *		{ "pgdata", required_argument, NULL, 'D' },
 *		{ "pghost", required_argument, NULL, 'h' },
 *		{ "pgport", required_argument, NULL, 'p' },
 *		{ "listen", required_argument, NULL, 'l' },
 *		{ "proxyport", required_argument, NULL, 'y' },
 *		{ "username", required_argument, NULL, 'U' },
		{ "auth", required_argument, NULL, 'A' },
 *		{ "dbname", required_argument, NULL, 'd' },
 *		{ "nodename", required_argument, NULL, 'n' },
 *		{ "formation", required_argument, NULL, 'f' },
 *		{ "group", required_argument, NULL, 'g' },
 *		{ "monitor", required_argument, NULL, 'm' },
 *		{ "skip-pg-hba", no_argument, NULL, 's' },
 *		{ "allow-removing-pgdata", no_argument, NULL, 'R' },
 *		{ "help", no_argument, NULL, 0 },
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
	KeeperConfig LocalOptionConfig = { 0 };
	int c, option_index, errors = 0;

	/* force some non-zero default values */
	LocalOptionConfig.groupId = -1;
	LocalOptionConfig.network_partition_timeout = -1;
	LocalOptionConfig.prepare_promotion_catchup = -1;
	LocalOptionConfig.prepare_promotion_walreceiver = -1;
	LocalOptionConfig.postgresql_restart_failure_timeout = -1;
	LocalOptionConfig.postgresql_restart_failure_max_retries = -1;

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

			case 'h':
			{
				/* { "pghost", required_argument, NULL, 'h' } */
				strlcpy(LocalOptionConfig.pgSetup.pghost, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--pghost %s", LocalOptionConfig.pgSetup.pghost);
				break;
			}

			case 'p':
			{
				/* { "pgport", required_argument, NULL, 'p' } */
				LocalOptionConfig.pgSetup.pgport = strtol(optarg, NULL, 10);
				if (LocalOptionConfig.pgSetup.pgport == 0 && errno == EINVAL)
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
				LocalOptionConfig.pgSetup.proxyport = strtol(optarg, NULL, 10);
				if (LocalOptionConfig.pgSetup.proxyport == 0 && errno == EINVAL)
				{
					log_error("Failed to parse --proxyport number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--proxy %d", LocalOptionConfig.pgSetup.proxyport);
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
				strlcpy(LocalOptionConfig.pgSetup.authMethod, optarg, NAMEDATALEN);
				log_trace("--auth %s", LocalOptionConfig.pgSetup.authMethod);
				break;
			}
			case 'd':
			{
				/* { "dbname", required_argument, NULL, 'd' } */
				strlcpy(LocalOptionConfig.pgSetup.dbname, optarg, NAMEDATALEN);
				log_trace("--dbname %s", LocalOptionConfig.pgSetup.dbname);
				break;
			}

			case 'n':
			{
				/* { "nodename", required_argument, NULL, 'n' } */
				strlcpy(LocalOptionConfig.nodename, optarg, _POSIX_HOST_NAME_MAX);
				log_trace("--nodename %s", LocalOptionConfig.nodename);
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
				int scanResult = sscanf(optarg, "%d", &LocalOptionConfig.groupId);
				if (scanResult == 0)
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

			case 'R':
			{
				/* { "allow-removing-pgdata", no_argument, NULL, 'R' } */
				allowRemovingPgdata = true;
				log_trace("--allow-removing-pgdata");
				break;
			}

			case 's':
			{
				/* { "skip-pg-hba", no_argument, NULL, 'N' } */
				skipPgHba = true;
				log_trace("--skip-pg-hba");
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

	/*
	 * Now, all commands need PGDATA validation.
	 */
	if (IS_EMPTY_STRING_BUFFER(LocalOptionConfig.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to get PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(LocalOptionConfig.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing now */
	memcpy(options, &LocalOptionConfig, sizeof(KeeperConfig));

	return optind;
}


/*
 * keeper_cli_getopt_pgdata gets the PGDATA options or environment variable,
 * either of those must be set for all of pg_autoctl's commands. This parameter
 * allows to know which PostgreSQL instance we are the keeper of, and also
 * allows to determine where is our configuration file.
 */
int
keeper_cli_getopt_pgdata(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	optind = 0;

	while ((c = getopt_long(argc, argv, "D:",
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
		}
	}

	if (IS_EMPTY_STRING_BUFFER(options.pgSetup.pgdata))
	{
		char *pgdata = getenv("PGDATA");

		if (pgdata == NULL)
		{
			log_fatal("Failed to get PGDATA either from the environment "
					  "or from --pgdata");
			exit(EXIT_CODE_BAD_ARGS);
		}

		strlcpy(options.pgSetup.pgdata, pgdata, MAXPGPATH);
	}

	log_info("Managing PostgreSQL installation at \"%s\"",
			 options.pgSetup.pgdata);

	if (!keeper_config_set_pathnames_from_pgdata(&options.pathnames, options.pgSetup.pgdata))
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
	if (!file_exists(options.pathnames.config))
	{
		log_fatal("Expected configuration file does not exists: \"%s\"",
				  options.pathnames.config);

		if (!directory_exists(options.pgSetup.pgdata))
		{
			log_warn("HINT: Check your PGDATA setting: \"%s\"",
					 options.pgSetup.pgdata);
		}

		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * set_first_pgctl sets the first pg_ctl found in PATH to given KeeperConfig.
 */
void
set_first_pgctl(PostgresSetup *pgSetup)
{
	char **pg_ctls = NULL;
	int n = search_pathlist(getenv("PATH"), "pg_ctl", &pg_ctls);

	if (n < 1)
	{
		log_fatal("Failed to find a pg_ctl command in your PATH");
		exit(EXIT_CODE_BAD_ARGS);
	}
	else
	{
		char *program = pg_ctls[0];
		char *version = pg_ctl_version(program);

		if (version == NULL)
		{
			/* errors have been logged in pg_ctl_version */
			exit(EXIT_CODE_PGCTL);
		}

		strlcpy(pgSetup->pg_ctl, program, MAXPGPATH);
		strlcpy(pgSetup->pg_version, version, PG_VERSION_STRING_MAX);

		free(version);
		search_pathlist_destroy_result(pg_ctls);
	}
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

			(void) monitor_config_init_from_pgsetup(monitor, &mconfig, pgSetup,
													missingPgdataIsOk,
													pgIsNotRunningIsOk);

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
										 pgIsNotRunningIsOk))
			{
				/* errors have already been logged */
				return false;
			}

			if (!monitor_init(&(keeper.monitor), config.monitor_pguri))
			{
				return false;
			}

			*monitor = keeper.monitor;
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
