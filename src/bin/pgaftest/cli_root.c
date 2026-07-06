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
#include "cli_indent.h"

/* -----------------------------------------------------------------------
 * Shared option parsing
 * ----------------------------------------------------------------------- */

typedef struct PgaftestOpts
{
	char specFile[1024];
	char workDir[1024];   /* empty = auto-derive from spec name */
	char stepName[64];
	char schedule[1024];
	char expected[1024];
	bool withTmux;
	bool noCleanup;       /* --no-cleanup: skip compose down after run */
	int verbose;
} PgaftestOpts;

static PgaftestOpts pgaftestOpts;   /* zero-initialised: workDir = "" */

/*
 * derive_work_dir — build $TMPDIR/pgaftest/<basename-without-.pgaf>
 *
 * Using TMPDIR (or /tmp when unset) + "pgaftest" + the test name means:
 *   - Every spec gets its own isolated directory.
 *   - Multiple instances of different specs can run concurrently.
 *   - The directory name matches the Docker Compose project name, so
 *     `docker compose -p basic_operation` addresses the right stack.
 *
 * Examples:
 *   tests/tap/specs/basic_operation.pgaf  →  /tmp/pgaftest/basic_operation
 *   failover.pgaf                         →  /tmp/pgaftest/failover
 */
static void
derive_work_dir(const char *specPath, char *buf, int buflen)
{
	/* base name without directory */
	const char *base = strrchr(specPath, '/');
	base = base ? base + 1 : specPath;

	char name[256] = { 0 };
	strncpy(name, base, sizeof(name) - 1);

	/* strip .pgaf extension */
	char *dot = strrchr(name, '.');
	if (dot && strcmp(dot, ".pgaf") == 0)
	{
		*dot = '\0';
	}

	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir || *tmpdir == '\0')
	{
		tmpdir = "/tmp";
	}

	snprintf(buf, buflen, "%s/pgaftest/%s", tmpdir, name);
}


static struct option long_options[] = {
	{ "work-dir", required_argument, NULL, 'w' },
	{ "schedule", required_argument, NULL, 'S' },
	{ "expected", required_argument, NULL, 'E' },
	{ "tmux", no_argument, NULL, 't' },
	{ "no-cleanup", no_argument, NULL, 'n' },
	{ "verbose", no_argument, NULL, 'v' },
	{ "debug", no_argument, NULL, 'd' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

static int
pgaftest_getopts(int argc, char **argv)
{
	int c, option_index = 0;

	while ((c = getopt_long(argc, argv, "w:S:E:tnvdh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'w':
			{
				strncpy(pgaftestOpts.workDir, optarg,
						sizeof(pgaftestOpts.workDir) - 1);
				break;
			}

			case 'S':
			{
				strncpy(pgaftestOpts.schedule, optarg,
						sizeof(pgaftestOpts.schedule) - 1);
				break;
			}

			case 'E':
			{
				strncpy(pgaftestOpts.expected, optarg,
						sizeof(pgaftestOpts.expected) - 1);
				break;
			}

			case 't':
			{
				pgaftestOpts.withTmux = true;
				break;
			}

			case 'n':
			{
				pgaftestOpts.noCleanup = true;
				break;
			}

			case 'v':
			{
				pgaftestOpts.verbose++;
				log_set_level(LOG_DEBUG);
				break;
			}

			case 'd':
			{
				pgaftestOpts.verbose += 2;
				log_set_level(LOG_TRACE);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
			}

			default:
			{
				break;
			}
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
	/*
	 * The subcommand library already called pgaftest_getopts() and stripped
	 * all options from argv before calling us.  argv[0] is the spec file
	 * (if provided), argc is the number of remaining positional arguments.
	 */

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
			if (nl)
			{
				*nl = '\0';
			}
			char *hash = strchr(line, '#');
			if (hash)
			{
				*hash = '\0';
			}

			/* skip blank lines */
			char *p = line;
			while (*p == ' ' || *p == '\t')
			{
				p++;
			}
			if (*p == '\0')
			{
				continue;
			}

			/* build spec path from schedule entry */
			char specPath[1024];
			if (strchr(p, '/'))
			{
				strncpy(specPath, p, sizeof(specPath) - 1);
			}
			else
			{
				snprintf(specPath, sizeof(specPath),
						 "tests/tap/specs/%s.pgaf", p);
			}

			char workDir[1024];
			if (pgaftestOpts.workDir[0] != '\0')
			{
				/* explicit --work-dir: append spec name as subdir */
				const char *base = strrchr(specPath, '/');
				base = base ? base + 1 : specPath;
				char name[128];
				strncpy(name, base, sizeof(name) - 1);
				char *dot = strrchr(name, '.');
				if (dot)
				{
					*dot = '\0';
				}
				snprintf(workDir, sizeof(workDir),
						 "%s/%s", pgaftestOpts.workDir, name);
			}
			else
			{
				derive_work_dir(specPath, workDir, sizeof(workDir));
			}

			TestSpec *spec = parse_test_spec(specPath);
			if (!spec)
			{
				failed++;
				total++;
				continue;
			}

			total++;
			if (!runner_run(spec, workDir, pgaftestOpts.noCleanup))
			{
				failed++;
			}
		}
		fclose(f);

		fprintf(stderr, "\nSchedule complete: %d/%d passed\n",
				total - failed, total);
		exit(failed > 0 ? 1 : 0);
	}

	/* Single spec file */
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest run [--schedule <file>] [<spec.pgaf>]");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[0],
			sizeof(pgaftestOpts.specFile) - 1);

	if (pgaftestOpts.workDir[0] == '\0')
	{
		derive_work_dir(pgaftestOpts.specFile,
						pgaftestOpts.workDir, sizeof(pgaftestOpts.workDir));
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_run(spec, pgaftestOpts.workDir, pgaftestOpts.noCleanup);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest setup <spec.pgaf>
 * ----------------------------------------------------------------------- */
static void
cli_setup(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest setup <spec.pgaf> [--tmux] [--work-dir <dir>]");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[0],
			sizeof(pgaftestOpts.specFile) - 1);

	if (pgaftestOpts.workDir[0] == '\0')
	{
		derive_work_dir(pgaftestOpts.specFile,
						pgaftestOpts.workDir, sizeof(pgaftestOpts.workDir));
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

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
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest step <step-name> [--work-dir <dir>]");
		exit(1);
	}

	/* step name is positional */
	strncpy(pgaftestOpts.stepName, argv[0],
			sizeof(pgaftestOpts.stepName) - 1);

	/* We need the spec file too — look for it in workDir */
	char specPath[1024];
	snprintf(specPath, sizeof(specPath), "%s/spec.pgaf",
			 pgaftestOpts.workDir);

	/* If there's a second positional arg, treat it as the spec path */
	if (argc >= 2 && argv[1] != NULL)
	{
		strncpy(specPath, argv[1], sizeof(specPath) - 1);
	}

	/* derive work dir from spec path when not given explicitly */
	if (pgaftestOpts.workDir[0] == '\0')
	{
		derive_work_dir(specPath, pgaftestOpts.workDir,
						sizeof(pgaftestOpts.workDir));
	}

	TestSpec *spec = parse_test_spec(specPath);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_step(spec, pgaftestOpts.workDir,
						  pgaftestOpts.stepName);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest prepare <spec.pgaf> [<output-dir>]
 *
 * Writes docker-compose.yml, *.ini files, and a Makefile to the output
 * directory.  Prints the docker compose command to stdout.
 * ----------------------------------------------------------------------- */
static void
cli_prepare(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest prepare <spec.pgaf> [<output-dir>]");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile) - 1);

	const char *outDir = (argc >= 2 && argv[1] && argv[1][0] != '-')
						 ? argv[1] : NULL;

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_prepare(spec, outDir);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest show <spec.pgaf>
 * ----------------------------------------------------------------------- */
static void
cli_show(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest show <spec.pgaf>");
		exit(1);
	}

	strncpy(pgaftestOpts.specFile, argv[0],
			sizeof(pgaftestOpts.specFile) - 1);

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_show(spec);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest down
 * ----------------------------------------------------------------------- */
static void
cli_down(int argc, char **argv)
{
	/* optional positional: spec file to derive work dir from */
	if (argc >= 1 && argv[0] && argv[0][0] != '-')
	{
		strncpy(pgaftestOpts.specFile, argv[0],
				sizeof(pgaftestOpts.specFile) - 1);
	}

	if (pgaftestOpts.workDir[0] == '\0' && pgaftestOpts.specFile[0] != '\0')
	{
		derive_work_dir(pgaftestOpts.specFile,
						pgaftestOpts.workDir, sizeof(pgaftestOpts.workDir));
	}

	/* spec file is optional for `down` — teardown may not need it */
	char specPath[1024];
	if (pgaftestOpts.specFile[0] != '\0')
	{
		strncpy(specPath, pgaftestOpts.specFile, sizeof(specPath) - 1);
	}
	else
	{
		snprintf(specPath, sizeof(specPath), "%s/spec.pgaf",
				 pgaftestOpts.workDir);
	}

	TestSpec *spec = NULL;
	if (access(specPath, R_OK) == 0)
	{
		spec = parse_test_spec(specPath);
	}

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
				 "  --work-dir <dir>   Working directory\n"
				 "                     (default: $TMPDIR/pgaftest/<testname>)\n"
				 "  --no-cleanup       Leave the compose stack running after the\n"
				 "                     run (pass or fail) for post-mortem inspection.\n"
				 "                     Use `pgaftest down <spec.pgaf>` to clean up.\n"
				 "  --verbose          Enable DEBUG log level\n"
				 "  --debug            Enable TRACE log level\n",
				 pgaftest_getopts, cli_run);

static CommandLine setup_command =
	make_command("setup",
				 "Bring up a cluster from a spec file (interactive mode)",
				 "[options] <spec.pgaf>",
				 "  --tmux             Launch a tmux session after setup\n"
				 "  --work-dir <dir>   Working directory\n"
				 "                     (default: $TMPDIR/pgaftest/<testname>)\n"
				 "  --verbose          Enable DEBUG log level\n"
				 "  --debug            Enable TRACE log level\n",
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

static CommandLine prepare_command =
	make_command("prepare",
				 "Write compose files and Makefile to a directory for manual use",
				 "<spec.pgaf> [<output-dir>]",
				 "  <spec.pgaf>    Path to the spec file\n"
				 "  <output-dir>   Output directory (default: <spec-stem>-compose/)\n",
				 pgaftest_getopts, cli_prepare);

static CommandLine down_command =
	make_command("down",
				 "Run teardown block and stop the compose stack",
				 "[options]",
				 "  --work-dir <dir>   Working directory (default: /tmp/pgaftest)\n",
				 pgaftest_getopts, cli_down);

extern void cli_indent(int argc, char **argv);

static CommandLine indent_command =
	make_command("indent",
				 "Parse a .pgaf spec and rewrite it with canonical indentation",
				 "<spec.pgaf>",
				 "",
				 pgaftest_getopts, cli_indent);

static CommandLine *root_subcommands[] = {
	&run_command,
	&setup_command,
	&step_command,
	&show_command,
	&prepare_command,
	&down_command,
	&indent_command,
	&pgaftest_demo_command,
	NULL
};

CommandLine pgaftest_root =
	make_command_set("pgaftest",
					 "pg_auto_failover test runner",
					 "[command] [options]",
					 "  run      Run a .pgaf spec (CI mode)\n"
					 "  setup    Bring up a cluster interactively\n"
					 "  step     Run one named step\n"
					 "  show     Print generated docker-compose.yml\n"
					 "  prepare  Write compose files + Makefile to a directory\n"
					 "  down     Tear down the cluster\n"
					 "  indent   Rewrite a spec with canonical indentation\n"
					 "  demo     Demo application\n",
					 NULL, root_subcommands);
