/*
 * src/bin/pg_autoctl/cli_restore.c
 *     Implementation of the pg_autoctl restore commands (archiving WAL files
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

#include "postgres_fe.h"

#include "archiving.h"
#include "cli_common.h"
#include "commandline.h"
#include "env_utils.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "monitor.h"
#include "monitor_config.h"
#include "string_utils.h"

/* cli_archive.c */
extern char configFilename[MAXPGPATH];

static int cli_restore_getopts(int argc, char **argv);

static void cli_restore_wal(int argc, char **argv);
static void cli_restore_pgdata(int argc, char **argv);
static void cli_restore_show(int argc, char **argv);

CommandLine restore_wal_command =
	make_command(
		"wal",
		"Restore a WAL file",
		" [ --pgdata | --monitor ] [ --formation --group ] [ --json ] "
		"filename [ destination ]",
		"  --pgdata      path to data directory\n"
		"  --config      restore command configuration\n"
		"  --json        output data in the JSON format\n",
		cli_archive_getopts,
		cli_restore_wal);

CommandLine restore_pgdata_command =
	make_command(
		"pgdata",
		"Restore a PGDATA directory (a base backup)",
		" [ --pgdata | --monitor ] [ --formation --group ] [ --json ]",
		"  --pgdata      path to data directory\n"
		"  --monitor     pg_auto_failover Monitor Postgres URL\n"
		"  --formation   restore WAL for given formation\n"
		"  --group       restore WAL for given group\n"
		"  --json        output data in the JSON format\n",
		cli_restore_getopts,
		cli_restore_pgdata);

CommandLine restore_show_command =
	make_command(
		"show",
		"Show restores (basebackups and WAL files)",
		" [ --pgdata | --monitor ] [ --formation --group ] [ --json ]",
		"  --pgdata      path to data directory\n"
		"  --monitor     pg_auto_failover Monitor Postgres URL\n"
		"  --formation   restore WAL for given formation\n"
		"  --group       restore WAL for given group\n"
		"  --json        output data in the JSON format\n",
		cli_restore_getopts,
		cli_restore_show);


static CommandLine *restore_subcommands[] = {
	&restore_wal_command,
	&restore_pgdata_command,
	&restore_show_command,
	NULL
};


CommandLine restore_commands =
	make_command_set("restore",
					 "Restore WAL files and PGDATA base backups", NULL, NULL,
					 NULL, restore_subcommands);


/*
 * cli_restore_getopts parses command line options for pg_autoctl restore
 * commands.
 */
static int
cli_restore_getopts(int argc, char **argv)
{
	return 0;
}


/*
 * cli_restore_wal restores a WAL file. Can be used as the restore_command in
 * the Postgres configuration.
 */
static void
cli_restore_wal(int argc, char **argv)
{
	Keeper keeper = { 0 };
	Monitor *monitor = &(keeper.monitor);
	KeeperConfig *config = &(keeper.config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	keeper.config = keeperOptions;

	if (argc < 1 || argc > 2)
	{
		log_error("Failed to parse command line arguments");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	bool monitorDisabledIsOk = false;

	if (!keeper_config_read_file_skip_pgsetup(config,
											  monitorDisabledIsOk))
	{
		log_fatal("Failed to read configuration file \"%s\"",
				  config->pathnames.config);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	const char *filename = argv[0];
	char dest[MAXPGPATH] = { 0 };

	if (argc == 2)
	{
		strlcpy(dest, argv[1], sizeof(dest));
	}
	else
	{
		sformat(dest, sizeof(dest), "%s/pg_wal/%s", pgSetup->pgdata, filename);
	}

	log_debug("Restoring WAL file \"%s\"", filename);
	log_debug("Restoring to destination \"%s\"", dest);

	/*
	 * The `pg_autoctl restore wal` command can be used in two modes:
	 *
	 * - either as the restore_command where we apply the archiver_policy
	 *   maintained on the monitor, using the configuration found on the
	 *   monitor.
	 *
	 * - or as an interactive command that's used to test and validate a local
	 *   configuration, and in this case we don't want to contact the monitor
	 *   at all.
	 *
	 * When using --config foo, we don't implement a monitor archiver_policy.
	 */
	if (!IS_EMPTY_STRING_BUFFER(configFilename))
	{
		if (!restore_wal_with_config(&keeper, configFilename, filename, dest))
		{
			log_fatal("Failed to restore WAL file \"%s\"", filename);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		exit(EXIT_CODE_QUIT);
	}

	/*
	 * When the --config option has not been used, we are handling the monitor
	 * archiver_policy settings. So first grab the policies, and then loop over
	 * each policy and try restoring the WAL file with the given policies.
	 *
	 * Of course we only need to restore the WAL file once, so as soon as any
	 * of the policies we got is successful, that's when we stop.
	 */
	MonitorArchiverPolicyArray policiesArray = { 0 };

	if (!monitor_get_archiver_policies(monitor,
									   config->formation,
									   &policiesArray))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (policiesArray.count == 0)
	{
		log_fatal("Failed to find an archiver policy for this node "
				  "in formation \"%s\" on the monitor",
				  config->formation);

		exit(EXIT_CODE_BAD_CONFIG);
	}

	for (int i = 0; i < policiesArray.count; i++)
	{
		MonitorArchiverPolicy *policy = &(policiesArray.policies[i]);

		if (restore_wal_for_policy(&keeper, policy, filename, dest))
		{
			exit(EXIT_CODE_QUIT);
		}
	}

	/* if we reach this line, we failed to restore using any policy */
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * cli_restore_pgdata makes a full base-backup and restores it.
 */
static void
cli_restore_pgdata(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/*
 * cli_restore_show shows the current backups and associated WAL files that we
 * have in the restore(s).
 */
static void
cli_restore_show(int argc, char **argv)
{
	exit(EXIT_CODE_INTERNAL_ERROR);
}
