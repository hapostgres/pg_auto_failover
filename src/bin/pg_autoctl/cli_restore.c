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

static int cli_restore_getopts(int argc, char **argv);

static void cli_restore_wal(int argc, char **argv);
static void cli_restore_pgdata(int argc, char **argv);
static void cli_restore_show(int argc, char **argv);

CommandLine restore_wal_command =
	make_command(
		"wal",
		"Restore a WAL file",
		" [ --pgdata | --monitor ] [ --formation --group ] [ --json ] filename",
		"  --pgdata      path to data directory\n"
		"  --monitor     pg_auto_failover Monitor Postgres URL\n"
		"  --formation   restore WAL for given formation\n"
		"  --group       restore WAL for given group\n"
		"  --config      restore command configuration\n"
		"  --json        output data in the JSON format\n",
		cli_restore_getopts,
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
