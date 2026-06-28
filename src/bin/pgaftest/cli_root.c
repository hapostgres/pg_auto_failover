/*
 * src/bin/pgaftest/cli_root.c
 *   Top-level sub-command table for pgaftest.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "commandline.h"
#include "defaults.h"
#include "log.h"
#include "test_spec.h"
#include "test_runner.h"
#include "cli_demo.h"

/* Default work directory when --work-dir is not specified */
#define DEFAULT_WORK_DIR "/tmp/pgaftest"

/* -----------------------------------------------------------------------
 * Shared option parsing
 * ----------------------------------------------------------------------- */

typedef struct PgaftestOpts
{
	char specFile[1024];
	char workDir[1024];
	char stepName[64];
	char schedule[1024];
	char expected[1024];
	bool withTmux;
	int  verbose;
} PgaftestOpts;

static PgaftestOpts pgaftestOpts = {
	.workDir = DEFAULT_WORK_DIR
};

static struct option long_options[] = {
	{ "work-dir",  required_argument, NULL, 'w' },
	{ "schedule",  required_argument, NULL, 'S' },
	{ "expected",  required_argument, NULL, 'E' },
	{ "tmux",      no_argument,       NULL, 't' },
	{ "verbose",   no_argument,       NULL, 'v' },
	{ "help",      no_argument,       NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

static int
pgaftest_getopts(int argc, char **argv)
{
	int c, option_index = 0;

	while ((c = getopt_long(argc, argv, "w:S:E:tvh",
	                        long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'w':
				strncpy(pgaftestOpts.workDir, optarg,
				        sizeof(pgaftestOpts.workDir)-1);
				break;
			case 'S':
				strncpy(pgaftestOpts.schedule, optarg,
				        sizeof(pgaftestOpts.schedule)-1);
				break;
			case 'E':
				strncpy(pgaftestOpts.expected, optarg,
				        sizeof(pgaftestOpts.expected)-1);
				break;
			case 't':
				pgaftestOpts.withTmux = true;
				break;
			case 'v':
				pgaftestOpts.verbose++;
				log_set_level(LOG_DEBUG);
				break;
			case 'h':
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
			default:
				break;
		}
	}
	return optind;
}

/* -----------------------------------------------------------------------
 * pgaftest run <spec.pgaf>
 * ----------------------------------------------------------------------- */

static void
cli_run(int argc, char **argv)
{
	int optind = pgaftest_getopts(argc, argv);

	/* Handle --schedule file */
	if (pgaftestOpts.schedule[0] != '\0')
	{
		FILE *f = fopen(pgaftestOpts.schedule, "r");
		if (!f)
		{
			log_error("Cannot open schedule \"%s\": %m",
			          pgaftestOpts.schedule);
			exit(1);
		}

		char line[256];
		int total = 0, failed = 0;

		while (fgets(line, sizeof(line), f))
		{
			/* strip newline and comments */
			char *nl = strchr(line, '\n');
			if (nl) *nl = '\0';
			char *hash = strchr(line, '#');
			if (hash) *hash = '\0';

			/* skip blank lines */
			char *p = line;
			while (*p == ' ' || *p == '\t') p++;
			if (*p == '\0') continue;

			/* build spec path from schedule entry */
			char specPath[1024];
			if (strchr(p, '/'))
				strncpy(specPath, p, sizeof(specPath)-1);
			else
				snprintf(specPath, sizeof(specPath),
				         "tests/tap/specs/%s.pgaf", p);

			char workDir[1024];
			/* derive per-spec workdir from spec basename */
			const char *base = strrchr(specPath, '/');
			base = base ? base+1 : specPath;
			char name[128];
			strncpy(name, base, sizeof(name)-1);
			char *dot = strrchr(name, '.');
			if (dot) *dot = '\0';
			snprintf(workDir, sizeof(workDir),
			         "%s/%s", pgaftestOpts.workDir, name);

			TestSpec *spec = parse_test_spec(specPath);
			if (!spec) { failed++; total++; continue; }

			total++;
			if (!runner_run(spec, workDir))
				failed++;
		}
		fclose(f);

		fprintf(stderr, "\nSchedule complete: %d/%d passed\n",
		        total - failed, total);
		exit(failed > 0 ? 1 : 0);
	}

	/* Single spec file */
	if (optind > argc)
	{
		log_error("Usage: pgaftest run [--schedule <file>] [<spec.pgaf>]");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[optind - 1],
	        sizeof(pgaftestOpts.specFile)-1);

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec) exit(1);

	bool ok = runner_run(spec, pgaftestOpts.workDir);
	exit(ok ? 0 : 1);
}

/* -----------------------------------------------------------------------
 * pgaftest setup <spec.pgaf>
 * ----------------------------------------------------------------------- */

static void
cli_setup(int argc, char **argv)
{
	int optind = pgaftest_getopts(argc, argv);

	if (optind > argc)
	{
		log_error("Usage: pgaftest setup <spec.pgaf> [--tmux] [--work-dir <dir>]");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[optind - 1],
	        sizeof(pgaftestOpts.specFile)-1);

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec) exit(1);

	bool ok = runner_setup(spec, pgaftestOpts.workDir,
	                       pgaftestOpts.withTmux);
	exit(ok ? 0 : 1);
}

/* -----------------------------------------------------------------------
 * pgaftest step <step-name>
 * ----------------------------------------------------------------------- */

static void
cli_step(int argc, char **argv)
{
	int optind = pgaftest_getopts(argc, argv);

	if (optind > argc)
	{
		log_error("Usage: pgaftest step <step-name> [--work-dir <dir>]");
		exit(1);
	}

	/* step name is positional */
	strncpy(pgaftestOpts.stepName, argv[optind - 1],
	        sizeof(pgaftestOpts.stepName)-1);

	/* We need the spec file too — look for it in workDir */
	char specPath[1024];
	snprintf(specPath, sizeof(specPath), "%s/spec.pgaf",
	         pgaftestOpts.workDir);

	/* If there's a second positional arg, treat it as the spec path */
	if (optind + 1 < argc)
		strncpy(specPath, argv[optind+1], sizeof(specPath)-1);

	TestSpec *spec = parse_test_spec(specPath);
	if (!spec) exit(1);

	bool ok = runner_step(spec, pgaftestOpts.workDir,
	                      pgaftestOpts.stepName);
	exit(ok ? 0 : 1);
}

/* -----------------------------------------------------------------------
 * pgaftest show <spec.pgaf>
 * ----------------------------------------------------------------------- */

static void
cli_show(int argc, char **argv)
{
	int optind = pgaftest_getopts(argc, argv);

	if (optind > argc)
	{
		log_error("Usage: pgaftest show <spec.pgaf>");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[optind - 1],
	        sizeof(pgaftestOpts.specFile)-1);

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec) exit(1);

	bool ok = runner_show(spec);
	exit(ok ? 0 : 1);
}

/* -----------------------------------------------------------------------
 * pgaftest down
 * ----------------------------------------------------------------------- */

static void
cli_down(int argc, char **argv)
{
	pgaftest_getopts(argc, argv);

	/* spec file is optional for `down` — teardown may not need it */
	char specPath[1024];
	snprintf(specPath, sizeof(specPath), "%s/spec.pgaf",
	         pgaftestOpts.workDir);

	TestSpec *spec = NULL;
	if (access(specPath, R_OK) == 0)
		spec = parse_test_spec(specPath);

	/* If no spec, just run compose down */
	if (!spec)
	{
		int rc = system("docker compose down --volumes --remove-orphans 2>&1");
		exit(rc == 0 ? 0 : 1);
	}

	bool ok = runner_down(spec, pgaftestOpts.workDir);
	exit(ok ? 0 : 1);
}

/* -----------------------------------------------------------------------
 * Root command table
 * ----------------------------------------------------------------------- */

static CommandLine run_command =
	make_command("run",
	             "Run a .pgaf spec headlessly (CI mode, TAP output)",
	             "[options] [<spec.pgaf>]",
	             "  --schedule <file>  Run all specs listed in schedule file\n"
	             "  --expected <dir>   Directory of expected output files\n"
	             "  --work-dir <dir>   Working directory (default: /tmp/pgaftest)\n"
	             "  --verbose          Increase log verbosity\n",
	             pgaftest_getopts, cli_run);

static CommandLine setup_command =
	make_command("setup",
	             "Bring up a cluster from a spec file (interactive mode)",
	             "[options] <spec.pgaf>",
	             "  --tmux             Launch a tmux session after setup\n"
	             "  --work-dir <dir>   Working directory (default: /tmp/pgaftest)\n"
	             "  --verbose          Increase log verbosity\n",
	             pgaftest_getopts, cli_setup);

static CommandLine step_command =
	make_command("step",
	             "Run a single named step against a live compose stack",
	             "[options] <step-name> [<spec.pgaf>]",
	             "  --work-dir <dir>   Working directory (default: /tmp/pgaftest)\n",
	             pgaftest_getopts, cli_step);

static CommandLine show_command =
	make_command("show",
	             "Print the docker-compose.yml that would be generated",
	             "[options] <spec.pgaf>",
	             "",
	             pgaftest_getopts, cli_show);

static CommandLine down_command =
	make_command("down",
	             "Run teardown block and stop the compose stack",
	             "[options]",
	             "  --work-dir <dir>   Working directory (default: /tmp/pgaftest)\n",
	             pgaftest_getopts, cli_down);

static CommandLine *root_subcommands[] = {
	&run_command,
	&setup_command,
	&step_command,
	&show_command,
	&down_command,
	&pgaftest_demo_command,
	NULL
};

CommandLine pgaftest_root =
	make_command_set("pgaftest",
	                 "pg_auto_failover test runner",
	                 "[command] [options]",
	                 "  run     Run a .pgaf spec (CI mode)\n"
	                 "  setup   Bring up a cluster interactively\n"
	                 "  step    Run one named step\n"
	                 "  show    Print generated docker-compose.yml\n"
	                 "  down    Tear down the cluster\n"
	                 "  demo    Demo application\n",
	                 NULL, root_subcommands);
