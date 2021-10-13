/*
 * src/bin/pg_autoctl/cli_archive.c
 *     Implementation of the pg_autoctl archive commands (archiving WAL files
 *     and pgdata, aka base backups).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>

#include "archiving.h"
#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "string_utils.h"

typedef struct ArchiverPolicyOptions
{
	char monitor_pguri[MAXCONNINFO];
	MonitorArchiverPolicy policy;

	bool outputJSON;
} ArchiverPolicyOptions;

ArchiverPolicyOptions archiverPolicyOptions = { 0 };

static int cli_archive_policy_getopts(int argc, char **argv);

static void cli_create_archive_policy(int argc, char **argv);
static void cli_drop_archive_policy(int argc, char **argv);
static void cli_get_archive_policy(int argc, char **argv);
static void cli_set_archive_policy(int argc, char **argv);

CommandLine create_archiver_policy_command =
	make_command(
		"archiver-policy",
		"Create an archiving policy for a given formation",
		"--formation --method --target --config filename [ ... ] ",
		"  --monitor          pg_auto_failover Monitor Postgres URL\n"
		"  --formation        pg_auto_failover formation\n"
		"  --target           archiving target name (default)\n"
		"  --method           archiving method to use for this policy (wal-g)\n"
		"  --config           archiving method configuration file, in JSON\n"
		"  --backup-interval  how often to archive PGDATA\n"
		"  --backup-max-count how many archives of PGDATA to keep\n"
		"  --backup-max-age   how long to keep a PGDATA archive\n",
		cli_archive_policy_getopts,
		cli_create_archive_policy);

CommandLine drop_archiver_policy_command =
	make_command(
		"archiver-policy",
		"Drop an archiving policy for a given formation",
		"--formation --target",
		"  --formation        pg_auto_failover formation\n"
		"  --target           archiving target name (default)\n",
		cli_archive_policy_getopts,
		cli_drop_archive_policy);

CommandLine get_archiver_policy_command =
	make_command(
		"archiver-policy",
		"Get archiving policy properties for a given formation",
		"--formation --target",
		"  --formation        pg_auto_failover formation\n"
		"  --target           archiving target name (default)\n",
		cli_archive_policy_getopts,
		cli_get_archive_policy);

CommandLine set_archiver_policy_command =
	make_command(
		"archiver-policy",
		"Set archiving policy properties for a given formation",
		"--formation --target",
		"  --formation        pg_auto_failover formation\n"
		"  --target           archiving target name (default)\n",
		cli_archive_policy_getopts,
		cli_set_archive_policy);


/*
 * cli_create_archive_policy_getopts parses the command line for pg_autoctl
 * archive policy commands.
 */
static int
cli_archive_policy_getopts(int argc, char **argv)
{
	ArchiverPolicyOptions options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "formation", required_argument, NULL, 'f' },
		{ "monitor", required_argument, NULL, 'm' },
		{ "target", required_argument, NULL, 't' },
		{ "method", required_argument, NULL, 'M' },
		{ "config", required_argument, NULL, 'C' },
		{ "backup-interval", required_argument, NULL, 'I' },
		{ "backup-max-count", required_argument, NULL, 'N' },
		{ "backup-max-age", required_argument, NULL, 'A' },
		{ "json", no_argument, NULL, 'J' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	/* set some defaults */
	strlcpy(options.policy.formation,
			FORMATION_DEFAULT,
			sizeof(options.policy.formation));

	strlcpy(options.policy.method,
			ARCHIVER_POLICY_DEFAULT_METHOD,
			sizeof(options.policy.method));

	strlcpy(options.policy.backupInterval,
			ARCHIVER_POLICY_DEFAULT_BACKUP_INTERVAL,
			sizeof(options.policy.backupInterval));

	options.policy.backupMaxCount = ARCHIVER_POLICY_DEFAULT_BACKUP_MAX_COUNT;

	strlcpy(options.policy.backupMaxAge,
			ARCHIVER_POLICY_DEFAULT_BACKUP_MAX_AGE,
			sizeof(options.policy.backupMaxAge));

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
			case 'f':
			{
				/* { "formation", required_argument, NULL, 'f' } */
				strlcpy(options.policy.formation, optarg, NAMEDATALEN);
				log_trace("--formation %s", options.policy.formation);
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
				strlcpy(options.monitor_pguri, optarg, MAXCONNINFO);
				log_trace("--monitor %s", options.monitor_pguri);
				break;
			}

			case 't':
			{
				/* { "target", required_argument, NULL, 't' }, */
				strlcpy(options.policy.target, optarg, NAMEDATALEN);
				log_trace("--target %s", options.policy.target);
				break;
			}

			case 'M':
			{
				/* { "method", required_argument, NULL, 'M' }, */
				strlcpy(options.policy.method, optarg, NAMEDATALEN);
				log_trace("--method %s", options.policy.method);
				break;
			}

			case 'C':
			{
				/* { "config", required_argument, NULL, 'C' }, */
				strlcpy(options.policy.config, optarg, NAMEDATALEN);
				log_trace("--config %s", options.policy.config);
				break;
			}

			case 'I':
			{
				/* { "backup-interval", required_argument, NULL, 'I' }, */
				strlcpy(options.policy.backupInterval, optarg, NAMEDATALEN);
				log_trace("--backup-interval %s", options.policy.backupInterval);
				break;
			}

			case 'N':
			{
				/* { "backup-max-count", required_argument, NULL, 'N' }, */
				if (!stringToInt(optarg, &options.policy.backupMaxCount))
				{
					log_error("Failed to parse --backup-max-count number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--backup-max-count %d", options.policy.backupMaxCount);
				break;
			}

			case 'A':
			{
				/* { "backup-max-age", required_argument, NULL, 'A' }, */
				strlcpy(options.policy.backupInterval, optarg, NAMEDATALEN);
				log_trace("--backup-interval %s", options.policy.backupInterval);
				break;
			}

			case 'J':
			{
				options.outputJSON = true;
				log_trace("--json");
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

	if (IS_EMPTY_STRING_BUFFER(options.policy.formation))
	{
		log_error("Option --formation is mandatory");
		++errors;
	}

	if (IS_EMPTY_STRING_BUFFER(options.policy.method))
	{
		log_error("Option --method is mandatory");
		++errors;
	}

	if (IS_EMPTY_STRING_BUFFER(options.policy.target))
	{
		log_error("Option --target is mandatory");
		++errors;
	}

	if (IS_EMPTY_STRING_BUFFER(options.policy.config))
	{
		log_error("Option --config is mandatory");
		++errors;
	}

	if (!file_exists(options.policy.config))
	{
		log_error("Failed to parse --config: file \"%s\" does not exists",
				  options.policy.config);
		++errors;
	}

	if (IS_EMPTY_STRING_BUFFER(options.monitor_pguri))
	{
		if (env_exists(PG_AUTOCTL_MONITOR) &&
			get_env_copy(PG_AUTOCTL_MONITOR,
						 options.monitor_pguri,
						 sizeof(options.monitor_pguri)) &&
			!IS_EMPTY_STRING_BUFFER(options.monitor_pguri))
		{
			log_debug("Using environment PG_AUTOCTL_MONITOR \"%s\"",
					  options.monitor_pguri);
		}
		else
		{
			log_error("Please provide either --monitor or PG_AUTOCTL_MONITOR "
					  "in the environment.");
			++errors;
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* publish our option parsing in the global variable */
	archiverPolicyOptions = options;

	return optind;
}


/*
 * cli_archive_policy_add adds an archiver policy to an exiting formation.
 */
static void
cli_create_archive_policy(int argc, char **argv)
{
	ArchiverPolicyOptions options = archiverPolicyOptions;
	MonitorArchiverPolicy policy = { 0 };
	Monitor monitor = { 0 };

	if (!monitor_init(&monitor, options.monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	char *config = NULL;
	long size = 0L;

	if (!read_file_if_exists(options.policy.config, &config, &size))
	{
		log_error("Failed to read the configuration from file \"%s\"",
				  options.policy.config);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!monitor_register_archiver_policy(&monitor,
										  options.policy.formation,
										  options.policy.target,
										  options.policy.method,
										  config,
										  options.policy.backupInterval,
										  options.policy.backupMaxCount,
										  options.policy.backupMaxAge,
										  &policy))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	free(config);

	log_info("Created archiver policy %lld "
			 "for formation \"%s\" and target \"%s\"",
			 (long long) policy.policyId,
			 policy.formation,
			 policy.target);
}


/*
 * cli_drop_archive_policy drops an archive policy
 */
static void
cli_drop_archive_policy(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * cli_get_archive_policy gets an archive policy properties
 */
static void
cli_get_archive_policy(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * cli_set_archive_policy sets an archive policy properties
 */
static void
cli_set_archive_policy(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}
