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
#include "file_utils.h"
#include "string_utils.h"
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
	strlcpy(name, base, sizeof(name));

	/* strip .pgaf extension */
	char *dot = strrchr(name, '.');
	if (dot && strcmp(dot, ".pgaf") == 0)
	{
		*dot = '\0';
	}

	const char *tmpdir = getenv("TMPDIR"); /* IGNORE-BANNED */
	if (!tmpdir || *tmpdir == '\0')
	{
		tmpdir = "/tmp";
	}

	sformat(buf, buflen, "%s/pgaftest/%s", tmpdir, name);
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
				strlcpy(pgaftestOpts.workDir, optarg, sizeof(pgaftestOpts.workDir));
				break;
			}

			case 'S':
			{
				strlcpy(pgaftestOpts.schedule, optarg, sizeof(pgaftestOpts.schedule));
				break;
			}

			case 'E':
			{
				strlcpy(pgaftestOpts.expected, optarg, sizeof(pgaftestOpts.expected));
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
		FILE *f = fopen(pgaftestOpts.schedule, "r"); /* IGNORE-BANNED */
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
				strlcpy(specPath, p, sizeof(specPath));
			}
			else
			{
				sformat(specPath, sizeof(specPath),
						"tests/tap/specs/%s.pgaf", p);
			}

			char workDir[1024];
			if (pgaftestOpts.workDir[0] != '\0')
			{
				/* explicit --work-dir: append spec name as subdir */
				const char *base = strrchr(specPath, '/');
				base = base ? base + 1 : specPath;
				char name[128];
				strlcpy(name, base, sizeof(name));
				char *dot = strrchr(name, '.');
				if (dot)
				{
					*dot = '\0';
				}
				sformat(workDir, sizeof(workDir),
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

		fformat(stderr, "\nSchedule complete: %d/%d passed\n",
				total - failed, total);
		exit(failed > 0 ? 1 : 0);
	}

	/* Single spec file */
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest run [--schedule <file>] [<spec.pgaf>]");
		exit(1);
	}

	strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));

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

	strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));

	/*
	 * The commandline library stops getopt at the first non-option (the spec
	 * file path), so flags that follow it — e.g. `pgaftest setup spec --tmux`
	 * — are not seen on the first pass.  Re-run getopts now: argv[0] acts as
	 * a dummy program name so getopt starts scanning from index 1 onward,
	 * picking up --tmux, --work-dir, etc.  optreset resets BSD/macOS state.
	 */
#ifdef __BSD_VISIBLE
	optreset = 1;
#endif
	optind = 1;
	pgaftest_getopts(argc, argv);

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
	strlcpy(pgaftestOpts.stepName, argv[0], sizeof(pgaftestOpts.stepName));

	/* We need the spec file too — look for it in workDir */
	char specPath[1024];
	sformat(specPath, sizeof(specPath), "%s/spec.pgaf",
			pgaftestOpts.workDir);

	/* If there's a second positional arg, treat it as the spec path */
	if (argc >= 2 && argv[1] != NULL)
	{
		strlcpy(specPath, argv[1], sizeof(specPath));
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

	strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));

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

	strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));

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
		strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));
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
		strlcpy(specPath, pgaftestOpts.specFile, sizeof(specPath));
	}
	else
	{
		sformat(specPath, sizeof(specPath), "%s/spec.pgaf",
				pgaftestOpts.workDir);
	}

	TestSpec *spec = NULL;
	if (access(specPath, R_OK) == 0)
	{
		spec = parse_test_spec(specPath);
	}

	/* If no spec, derive project name from workDir and run targeted compose down */
	if (!spec)
	{
		const char *base = strrchr(pgaftestOpts.workDir, '/');
		const char *projectName = (base && *(base + 1)) ? base + 1
								  : pgaftestOpts.workDir;

		if (projectName[0] == '\0')
		{
			log_error("Cannot determine project name: "
					  "provide --work-dir or a spec file path");
			exit(1);
		}

		char cmd[2048];
		sformat(cmd, sizeof(cmd),
				"docker compose -p %s -f %s/docker-compose.yml "
				"down --volumes --remove-orphans",
				projectName, pgaftestOpts.workDir);

		int rc = system(cmd);
		exit(rc == 0 ? 0 : 1);
	}

	bool ok = runner_down(spec, pgaftestOpts.workDir);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest _setup_ <spec.pgaf> --work-dir <dir>
 *
 * Internal command: runs the setup{} block against an already-running
 * compose stack.  Invoked from the tmux bottom pane by runner_setup()
 * so the user immediately gets the session while setup runs live.
 * Not intended to be called directly by users.
 * ----------------------------------------------------------------------- */
static void
cli_run_setup_only(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest _setup_ <spec.pgaf> [--work-dir <dir>]");
		exit(1);
	}

	strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));

#ifdef __BSD_VISIBLE
	optreset = 1;
#endif
	optind = 1;
	pgaftest_getopts(argc, argv);

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

	bool ok = runner_run_setup_only(spec, pgaftestOpts.workDir);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * Shared helper: resolve spec + work-dir for interactive sub-commands.
 *
 * When running inside the pgaftest container (set up by --tmux), the
 * compose stack injects:
 *   PGAFTEST_SPEC         = /spec.pgaf
 *   PGAFTEST_HOST_WORK_DIR = <host workDir> (same abs path bind-mounted)
 *
 * Priority: explicit CLI arg > env var > /spec.pgaf in CWD.
 * ----------------------------------------------------------------------- */
static void
resolve_interactive_context(void)
{
	if (pgaftestOpts.specFile[0] == '\0')
	{
		const char *envSpec = getenv("PGAFTEST_SPEC"); /* IGNORE-BANNED */
		if (envSpec && *envSpec)
		{
			strlcpy(pgaftestOpts.specFile, envSpec, sizeof(pgaftestOpts.specFile));
		}
		else if (access("/spec.pgaf", R_OK) == 0)
		{
			strlcpy(pgaftestOpts.specFile, "/spec.pgaf", sizeof(pgaftestOpts.specFile));
		}
	}

	if (pgaftestOpts.workDir[0] == '\0')
	{
		const char *envWork = getenv("PGAFTEST_HOST_WORK_DIR"); /* IGNORE-BANNED */
		if (envWork && *envWork)
		{
			strlcpy(pgaftestOpts.workDir, envWork, sizeof(pgaftestOpts.workDir));
		}
		else if (pgaftestOpts.specFile[0])
		{
			derive_work_dir(pgaftestOpts.specFile,
							pgaftestOpts.workDir, sizeof(pgaftestOpts.workDir));
		}
	}
}


/* -----------------------------------------------------------------------
 * pgaftest wait until <node> state = <state> [timeout <N>s]
 * ----------------------------------------------------------------------- */
static void
cli_wait(int argc, char **argv)
{
	/*
	 * Syntax: pgaftest wait until <node> state = <state> [timeout <N>s]
	 *
	 * After option stripping by the command-line library, remaining argv is:
	 *   argv[0] = "until"
	 *   argv[1] = <node>
	 *   argv[2] = "state"
	 *   argv[3] = "="
	 *   argv[4] = <state>
	 *   argv[5] = "timeout"   (optional)
	 *   argv[6] = "<N>s"      (optional)
	 */
	if (argc < 5 ||
		strcmp(argv[0], "until") != 0 ||
		strcmp(argv[2], "state") != 0 ||
		strcmp(argv[3], "=") != 0)
	{
		log_error("Usage: pgaftest wait until <node> state = <state> [timeout <N>s]");
		exit(EXIT_CODE_BAD_ARGS);
	}

	const char *nodeName = argv[1];
	const char *targetState = argv[4];
	int timeoutSecs = 60; /* default */

	if (argc >= 7 && strcmp(argv[5], "timeout") == 0)
	{
		char *end = NULL;
		long v = strtol(argv[6], &end, 10);
		if (end && (*end == 's' || *end == '\0') && v > 0)
		{
			timeoutSecs = (int) v;
		}
	}

	resolve_interactive_context();

	if (pgaftestOpts.specFile[0] == '\0')
	{
		log_error("No spec file: pass one as argument or set PGAFTEST_SPEC");
		exit(EXIT_CODE_BAD_ARGS);
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_wait(spec, pgaftestOpts.workDir,
						  nodeName, targetState, timeoutSecs);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest sql <node> "<query>"
 * ----------------------------------------------------------------------- */
static void
cli_sql(int argc, char **argv)
{
	if (argc < 2)
	{
		log_error("Usage: pgaftest sql <node> \"<query>\"");
		exit(EXIT_CODE_BAD_ARGS);
	}

	const char *service = argv[0];
	const char *query = argv[1];

	resolve_interactive_context();

	if (pgaftestOpts.specFile[0] == '\0')
	{
		log_error("No spec file: pass one as argument or set PGAFTEST_SPEC");
		exit(EXIT_CODE_BAD_ARGS);
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_sql(spec, pgaftestOpts.workDir, service, query);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest network connect|disconnect <node>
 * ----------------------------------------------------------------------- */
static void
cli_network(int argc, char **argv)
{
	if (argc < 2 ||
		(strcmp(argv[0], "connect") != 0 && strcmp(argv[0], "disconnect") != 0))
	{
		log_error("Usage: pgaftest network connect|disconnect <node>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	bool connect = (strcmp(argv[0], "connect") == 0);
	const char *nodeName = argv[1];

	resolve_interactive_context();

	if (pgaftestOpts.specFile[0] == '\0')
	{
		log_error("No spec file: pass one as argument or set PGAFTEST_SPEC");
		exit(EXIT_CODE_BAD_ARGS);
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_network(spec, pgaftestOpts.workDir, nodeName, connect);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest assert <node> state = <state>
 * ----------------------------------------------------------------------- */
static void
cli_assert(int argc, char **argv)
{
	if (argc < 4 ||
		strcmp(argv[1], "state") != 0 ||
		strcmp(argv[2], "=") != 0)
	{
		log_error("Usage: pgaftest assert <node> state = <state>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	const char *nodeName = argv[0];
	const char *targetState = argv[3];

	resolve_interactive_context();

	if (pgaftestOpts.specFile[0] == '\0')
	{
		log_error("No spec file: pass one as argument or set PGAFTEST_SPEC");
		exit(EXIT_CODE_BAD_ARGS);
	}

	TestSpec *spec = parse_test_spec(pgaftestOpts.specFile);
	if (!spec)
	{
		exit(1);
	}

	bool ok = runner_assert(spec, pgaftestOpts.workDir, nodeName, targetState);
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

static CommandLine internal_setup_command =
	make_command("_setup_",
				 "Internal: run setup{} block against a running stack (tmux helper)",
				 "<spec.pgaf> [--work-dir <dir>]",
				 "",
				 pgaftest_getopts, cli_run_setup_only);

static CommandLine wait_command =
	make_command("wait",
				 "Wait until a node reaches a target state (interactive)",
				 "until <node> state = <state> [timeout <N>s]",
				 "  until <node> state = <state>   Poll until node is in state\n"
				 "  timeout <N>s                   Timeout in seconds (default 60)\n"
				 "\n"
				 "  Reads PGAFTEST_SPEC and PGAFTEST_HOST_WORK_DIR when run\n"
				 "  inside the pgaftest container (pgaftest setup --tmux).\n",
				 pgaftest_getopts, cli_wait);

static CommandLine sql_command =
	make_command("sql",
				 "Run a SQL query on a named node and print the result (interactive)",
				 "<node> \"<query>\"",
				 "  <node>    Service name (node1, node2, monitor, …)\n"
				 "  <query>   SQL statement (quote it)\n",
				 pgaftest_getopts, cli_sql);

static CommandLine network_command =
	make_command("network",
				 "Connect or disconnect a node from the compose network (interactive)",
				 "connect|disconnect <node>",
				 "  connect <node>     Restore the node's network access\n"
				 "  disconnect <node>  Sever the node's network access\n",
				 pgaftest_getopts, cli_network);

static CommandLine assert_command =
	make_command("assert",
				 "Assert a node's current state; exit non-zero if it doesn't match (interactive)",
				 "<node> state = <state>",
				 "  <node>    Service name (node1, node2, …)\n"
				 "  <state>   Expected state (primary, secondary, wait_primary, …)\n",
				 pgaftest_getopts, cli_assert);

static CommandLine *root_subcommands[] = {
	&run_command,
	&setup_command,
	&step_command,
	&show_command,
	&prepare_command,
	&down_command,
	&indent_command,
	&wait_command,
	&sql_command,
	&network_command,
	&assert_command,
	&internal_setup_command,
	&pgaftest_demo_command,
	NULL
};

CommandLine pgaftest_root =
	make_command_set("pgaftest",
					 "pg_auto_failover test runner",
					 "[command] [options]",
					 "  run       Run a .pgaf spec (CI mode)\n"
					 "  setup     Bring up a cluster interactively\n"
					 "  step      Run one named step\n"
					 "  show      Print generated docker-compose.yml\n"
					 "  prepare   Write compose files + Makefile to a directory\n"
					 "  down      Tear down the cluster\n"
					 "  indent    Rewrite a spec with canonical indentation\n"
					 "  wait      Wait until a node reaches a state\n"
					 "  sql       Run SQL on a node and print the result\n"
					 "  network   Connect or disconnect a node from the network\n"
					 "  assert    Assert a node's current state\n"
					 "  demo      Demo application\n",
					 NULL, root_subcommands);
