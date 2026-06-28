/*
 * src/bin/pg_autoctl/cli_node.c
 *   pg_autoctl node — declarative node lifecycle driven by a pg_autoctl_node.ini file.
 *
 *   pg_autoctl node run   <file>    Create (if needed) then run the supervisor.
 *   pg_autoctl node apply <file>    Converge mutable fields on a running node.
 *   pg_autoctl node show            Dump current config as pg_autoctl_node.ini.
 *   pg_autoctl node check <file>    Validate the file without creating anything.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli_common.h"
#include "cli_node.h"
#include "cli_root.h"
#include "commandline.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "keeper_config.h"
#include "log.h"
#include "nodespec.h"
#include "pgsetup.h"
#include "runprogram.h"
#include "string_utils.h"


static int  cli_node_run_getopts(int argc, char **argv);
static void cli_node_run(int argc, char **argv);
static int  cli_node_apply_getopts(int argc, char **argv);
static void cli_node_apply(int argc, char **argv);
static int  cli_node_show_getopts(int argc, char **argv);
static void cli_node_show(int argc, char **argv);
static int  cli_node_check_getopts(int argc, char **argv);
static void cli_node_check(int argc, char **argv);


static CommandLine node_run_command =
	make_command(
		"run",
		"Create (if needed) and run a node described by a pg_autoctl_node.ini file",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n"
		"               (default: " PG_AUTOCTL_NODESPEC_PATH ")\n",
		cli_node_run_getopts,
		cli_node_run);

static CommandLine node_apply_command =
	make_command(
		"apply",
		"Apply mutable settings from a pg_autoctl_node.ini to a running node",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n",
		cli_node_apply_getopts,
		cli_node_apply);

static CommandLine node_show_command =
	make_command(
		"show",
		"Dump current node configuration as a pg_autoctl_node.ini file",
		"[--pgdata <dir>]",
		"  --pgdata <dir>   location of the Postgres data directory\n",
		cli_node_show_getopts,
		cli_node_show);

static CommandLine node_check_command =
	make_command(
		"check",
		"Validate a pg_autoctl_node.ini file without creating anything",
		"<file.ini>",
		"  <file.ini>   path to the pg_autoctl_node.ini file\n",
		cli_node_check_getopts,
		cli_node_check);

static CommandLine *node_subcommands[] = {
	&node_run_command,
	&node_apply_command,
	&node_show_command,
	&node_check_command,
	NULL
};

CommandLine node_commands =
	make_command_set(
		"node",
		"Declarative node lifecycle — create, run, and reconfigure from a .ini file",
		NULL, NULL, NULL, node_subcommands);


/* -----------------------------------------------------------------------
 * Shared state
 * ----------------------------------------------------------------------- */

static char nodeSpecPath[MAXPGPATH] = { 0 };


/* -----------------------------------------------------------------------
 * pg_autoctl node run <file>
 * ----------------------------------------------------------------------- */

static int
cli_node_run_getopts(int argc, char **argv)
{
	if (argc > 0 && argv[0][0] != '-')
		strlcpy(nodeSpecPath, argv[0], sizeof(nodeSpecPath));
	else
		strlcpy(nodeSpecPath, PG_AUTOCTL_NODESPEC_PATH, sizeof(nodeSpecPath));

	return 0;
}

/*
 * cli_node_run reads the pg_autoctl_node.ini file, runs `pg_autoctl create
 * <kind> --run` if PGDATA does not yet exist, or `pg_autoctl run` if it does.
 *
 * We exec() rather than call the internal functions directly so that:
 *  1. The supervisor's fork+exec live-upgrade pattern is preserved.
 *  2. The already-running supervisor (PID 1) can restart this process after
 *     a binary upgrade without special-casing the "node run" path.
 */
static void
cli_node_run(int argc, char **argv)
{
	NodeSpec spec = { 0 };
	char *args[32];
	int nargs;

	if (!nodespec_read(nodeSpecPath, &spec))
		exit(EXIT_CODE_BAD_CONFIG);

	/*
	 * If PGDATA already has a pg_autoctl.cfg, the node was created before.
	 * In that case run `pg_autoctl run` (no --run, no create flags).
	 * Otherwise run `pg_autoctl create <kind> ... --run`.
	 */
	char cfgPath[MAXPGPATH];
	bool cfgExists = false;

	if (!IS_EMPTY_STRING_BUFFER(spec.pgdata))
	{
		sformat(cfgPath, sizeof(cfgPath),
				"%s/pg_autoctl.cfg", spec.pgdata);
		cfgExists = file_exists(cfgPath);
	}

	if (cfgExists)
	{
		/*
		 * Node was already created.  Apply any mutable changes that might
		 * have been made to the spec file since the last run, then hand off
		 * to the normal run path.
		 */
		NodeSpec prev = { 0 };

		/* best-effort: ignore errors if we can't re-read the spec */
		(void) nodespec_read(nodeSpecPath, &prev);
		(void) nodespec_apply(&spec, &prev);

		args[0] = (char *) pg_autoctl_program;
		args[1] = "run";
		args[2] = "--pgdata";
		args[3] = spec.pgdata;
		args[4] = NULL;
		nargs = 4;
	}
	else
	{
		nargs = nodespec_create_argv(&spec, pg_autoctl_program,
									 args, 32);
		if (nargs < 0)
			exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* Tell the supervisor which spec file to watch for live changes */
	setenv("PG_AUTOCTL_NODESPEC", nodeSpecPath, 1);

	/* Log the command we're about to exec */
	{
		PQExpBuffer cmd = createPQExpBuffer();
		for (int i = 0; i < nargs; i++)
		{
			if (i > 0) appendPQExpBufferChar(cmd, ' ');
			appendPQExpBufferStr(cmd, args[i]);
		}
		log_info("pg_autoctl node run: %s", cmd->data);
		destroyPQExpBuffer(cmd);
	}

	execv(args[0], args);

	/* If we get here execv failed */
	log_fatal("execv(\"%s\"): %m", args[0]);
	exit(EXIT_CODE_INTERNAL_ERROR);
}


/* -----------------------------------------------------------------------
 * pg_autoctl node apply <file>
 * ----------------------------------------------------------------------- */

static int
cli_node_apply_getopts(int argc, char **argv)
{
	if (argc > 0 && argv[0][0] != '-')
		strlcpy(nodeSpecPath, argv[0], sizeof(nodeSpecPath));
	else
	{
		log_error("pg_autoctl node apply requires a file argument");
		exit(EXIT_CODE_BAD_ARGS);
	}
	return 0;
}

static void
cli_node_apply(int argc, char **argv)
{
	NodeSpec new_spec = { 0 };
	NodeSpec cur_spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &new_spec))
		exit(EXIT_CODE_BAD_CONFIG);

	/* Read the current spec from the same path as a baseline */
	(void) nodespec_read(nodeSpecPath, &cur_spec);

	if (!nodespec_apply(&new_spec, &cur_spec))
	{
		log_error("Failed to apply node spec from \"%s\"", nodeSpecPath);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/* -----------------------------------------------------------------------
 * pg_autoctl node show [--pgdata <dir>]
 * ----------------------------------------------------------------------- */

static int
cli_node_show_getopts(int argc, char **argv)
{
	/* honour --pgdata from the global keeperOptions */
	return cli_getopt_pgdata(argc, argv);
}

static void
cli_node_show(int argc, char **argv)
{
	/*
	 * Build a NodeSpec from the running keeper/monitor config and emit it.
	 * We read the existing pg_autoctl.cfg rather than the node spec file so
	 * that `pg_autoctl node show` reflects the live configuration.
	 */
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		log_error("Failed to read pg_autoctl configuration");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	NodeSpec spec = { 0 };

	spec.kind = config.pgSetup.pgKind;
	strlcpy(spec.pgdata, config.pgSetup.pgdata, sizeof(spec.pgdata));
	strlcpy(spec.hostname, config.hostname, sizeof(spec.hostname));
	spec.port = config.pgSetup.pgport;
	strlcpy(spec.monitor_pguri, config.monitor_pguri,
			sizeof(spec.monitor_pguri));
	strlcpy(spec.formation, config.formation, sizeof(spec.formation));
	spec.group = config.groupId;

	/* Mutable settings defaults — we'd need a monitor round-trip for live values */
	spec.candidate_priority = 50;
	spec.replication_quorum = true;

	/* Create-time defaults */
	strlcpy(spec.ssl, "self-signed", sizeof(spec.ssl));
	strlcpy(spec.auth, "trust", sizeof(spec.auth));
	spec.pg_hba_lan = true;

	(void) nodespec_write(&spec, stdout);
}


/* -----------------------------------------------------------------------
 * pg_autoctl node check <file>
 * ----------------------------------------------------------------------- */

static int
cli_node_check_getopts(int argc, char **argv)
{
	if (argc > 0 && argv[0][0] != '-')
		strlcpy(nodeSpecPath, argv[0], sizeof(nodeSpecPath));
	else
	{
		log_error("pg_autoctl node check requires a file argument");
		exit(EXIT_CODE_BAD_ARGS);
	}
	return 0;
}

static void
cli_node_check(int argc, char **argv)
{
	NodeSpec spec = { 0 };

	if (!nodespec_read(nodeSpecPath, &spec))
	{
		log_error("Invalid node spec file \"%s\"", nodeSpecPath);
		exit(EXIT_CODE_BAD_CONFIG);
	}

	const char *kindStr;
	switch (spec.kind)
	{
		case NODE_KIND_UNKNOWN:           kindStr = "monitor";      break;
		case NODE_KIND_STANDALONE:        kindStr = "postgres";     break;
		case NODE_KIND_CITUS_COORDINATOR: kindStr = "coordinator";  break;
		case NODE_KIND_CITUS_WORKER:      kindStr = "worker";       break;
		default:                          kindStr = "unknown";      break;
	}

	fprintf(stdout, "Node spec \"%s\" is valid.\n", nodeSpecPath);
	fprintf(stdout, "  kind               : %s\n", kindStr);
	fprintf(stdout, "  pgdata             : %s\n", spec.pgdata);
	fprintf(stdout, "  hostname           : %s\n", spec.hostname);
	fprintf(stdout, "  port               : %d\n", spec.port);

	if (spec.kind != NODE_KIND_UNKNOWN)
	{
		fprintf(stdout, "  monitor_pguri      : %s\n", spec.monitor_pguri);
		fprintf(stdout, "  formation          : %s\n", spec.formation);
		fprintf(stdout, "  group              : %d\n", spec.group);
	}

	fprintf(stdout, "  candidate_priority : %d\n", spec.candidate_priority);
	fprintf(stdout, "  replication_quorum : %s\n",
			spec.replication_quorum ? "true" : "false");
	fprintf(stdout, "  ssl                : %s\n", spec.ssl);
	fprintf(stdout, "  auth               : %s\n", spec.auth);
	fprintf(stdout, "  pg_hba_lan         : %s\n",
			spec.pg_hba_lan ? "true" : "false");
}
