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

/* forward declaration */
static void resolve_interactive_context(void);

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

	optind = 0;

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
		log_error("Usage: pgaftest setup [--tmux] [--work-dir <dir>] <spec.pgaf>");
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

	bool ok = runner_setup(spec, pgaftestOpts.workDir,
						   pgaftestOpts.withTmux);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest step <step-name> [<spec.pgaf>]
 *
 * The spec file is resolved in order:
 *   1. explicit second positional argument (a .pgaf path)
 *   2. PGAFTEST_SPEC env var (set inside the pgaftest container)
 *   3. /spec.pgaf  (fixed mount point in the container)
 *   4. <workDir>/spec.pgaf  (host-side leftover from a previous setup)
 * ----------------------------------------------------------------------- */
static void
cli_step(int argc, char **argv)
{
	bool autoNext = false;

	/*
	 * `pgaftest step` with no positional args runs the next (or retries the
	 * last failed) step automatically using the state file.
	 * `pgaftest step <name>` runs that specific step by name.
	 */
	if (argc >= 1 && argv[0] != NULL && argv[0][0] != '-')
	{
		strlcpy(pgaftestOpts.stepName, argv[0], sizeof(pgaftestOpts.stepName));

		/* Optional spec.pgaf as second positional */
		if (argc >= 2 && argv[1] != NULL && argv[1][0] != '-')
		{
			strlcpy(pgaftestOpts.specFile, argv[1],
					sizeof(pgaftestOpts.specFile));
		}
	}
	else
	{
		autoNext = true;
	}

	/* Fill specFile + workDir from env / known paths when not set yet */
	resolve_interactive_context();

	if (pgaftestOpts.specFile[0] == '\0')
	{
		if (pgaftestOpts.workDir[0] != '\0')
		{
			sformat(pgaftestOpts.specFile, sizeof(pgaftestOpts.specFile),
					"%s/spec.pgaf", pgaftestOpts.workDir);
		}
		else
		{
			log_error("No spec file: pass <spec.pgaf>, set PGAFTEST_SPEC, "
					  "or use --work-dir");
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

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

	bool ok;
	if (autoNext)
	{
		ok = runner_step_next(spec, pgaftestOpts.workDir);
	}
	else
	{
		ok = runner_step(spec, pgaftestOpts.workDir, pgaftestOpts.stepName);
	}

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
 * pgaftest show — sub-command set
 *
 * show compose    print the generated docker-compose.yml
 * show spec       print the spec in canonical (indented) form
 * show step       list steps with * on next / ! on failed
 * show services   list the compose service names
 * ----------------------------------------------------------------------- */

/*
 * Shared spec resolution for show sub-commands: accept an optional positional
 * spec path, then fall back to env / /spec.pgaf.
 */
static TestSpec *
show_resolve_spec(int argc, char **argv)
{
	if (argc >= 1 && argv[0] && argv[0][0] != '-' &&
		strstr(argv[0], ".pgaf") != NULL)
	{
		strlcpy(pgaftestOpts.specFile, argv[0], sizeof(pgaftestOpts.specFile));
	}
	resolve_interactive_context();
	if (pgaftestOpts.specFile[0] == '\0')
	{
		log_error("No spec file: pass <spec.pgaf> or set PGAFTEST_SPEC");
		exit(EXIT_CODE_BAD_ARGS);
	}
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
	return spec;
}


static void
cli_show_compose(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);
	bool ok = runner_show(spec);
	exit(ok ? 0 : 1);
}


static void
cli_show_spec(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);

	/* Re-print the spec source — the canonical copy is at specFile itself */
	FILE *f = fopen(pgaftestOpts.specFile, "r"); /* IGNORE-BANNED */
	if (!f)
	{
		log_error("Cannot open \"%s\": %m", pgaftestOpts.specFile);
		exit(1);
	}
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		fwrite(buf, 1, n, stdout);
	}
	fclose(f);
	(void) spec; /* parsed for validation; content comes from the file */
	exit(0);
}


static void
cli_show_step(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);

	TestRunnerState st;
	bool hasState = runner_state_read(pgaftestOpts.workDir, &st);

	for (int i = 0; i < spec->sequenceLength; i++)
	{
		const char *name = spec->sequence[i];

		if (!hasState)
		{
			/* No state yet — mark index 0 as the pending next step */
			fformat(stdout, "%s %s\n",
					(i == 0) ? "*" : " ", name);
		}
		else if (!st.last_ok && strcmp(name, st.last_step) == 0)
		{
			/* Last step failed — mark it for retry */
			fformat(stdout, "! %s\n", name);
		}
		else if (i == st.current)
		{
			/* This is the next step to run */
			fformat(stdout, "* %s\n", name);
		}
		else if (i < st.current)
		{
			fformat(stdout, "  %s\n", name);
		}
		else
		{
			fformat(stdout, "  %s\n", name);
		}
	}
	exit(0);
}


static void
cli_show_services(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);

	if (spec->cluster.withMonitor)
	{
		fformat(stdout, "monitor\n");
	}

	for (int fi = 0; fi < spec->cluster.formationCount; fi++)
	{
		const TestFormation *form = &spec->cluster.formations[fi];
		for (int ni = 0; ni < form->nodeCount; ni++)
		{
			fformat(stdout, "%s\n", form->nodes[ni].name);
		}
	}
	exit(0);
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
	if (!ok)
	{
		exit(1);
	}

	/* Print step list and usage hint directly to the tty. */
	fformat(stdout, "\n");
	if (spec->sequenceLength > 0)
	{
		fformat(stdout, "Steps:");
		for (int i = 0; i < spec->sequenceLength; i++)
		{
			fformat(stdout, "  %s", spec->sequence[i]);
		}
		fformat(stdout, "\n");
	}
	fformat(stdout,
			"Try: pgaftest step <name>"
			"  |  pgaftest network disconnect <node>"
			"  |  pgaftest wait until <node> state = <state>"
			"  |  pgaftest down\n\n");

	/*
	 * Replace this process with bash so the tmux pane becomes an
	 * interactive shell without opening a new process layer.
	 */
	execlp("bash", "bash", NULL);

	/* execlp only returns on error */
	log_error("Failed to exec bash: %m");
	exit(1);
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

static CommandLine show_compose_command =
	make_command("compose",
				 "Print the generated docker-compose.yml",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n",
				 pgaftest_getopts, cli_show_compose);

static CommandLine show_spec_command =
	make_command("spec",
				 "Print the spec file source",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n",
				 pgaftest_getopts, cli_show_spec);

static CommandLine show_step_command =
	make_command("step",
				 "List steps in sequence order with * on the current/next step",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n"
				 "\n"
				 "  Markers: *=next to run  !=failed (will retry)  (space)=done\n",
				 pgaftest_getopts, cli_show_step);

static CommandLine show_services_command =
	make_command("services",
				 "List the compose service names",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n",
				 pgaftest_getopts, cli_show_services);

static CommandLine *show_subcommands[] = {
	&show_compose_command,
	&show_spec_command,
	&show_step_command,
	&show_services_command,
	NULL
};

static CommandLine show_command =
	make_command_set("show",
					 "Show information about a spec or running cluster",
					 "<compose|spec|step|services> [<spec.pgaf>]",
					 "  compose    Print the generated docker-compose.yml\n"
					 "  spec       Print the spec file source\n"
					 "  step       List steps with * on next / ! on failed\n"
					 "  services   List compose service names\n",
					 pgaftest_getopts, show_subcommands);

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

static void
cli_help(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	commandline_help(stdout);
	exit(0);
}


static CommandLine help_command =
	make_command("help",
				 "Show this help message",
				 "",
				 "",
				 pgaftest_getopts, cli_help);

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
	&help_command,
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
					 "  show      Show information (compose, spec, step, services)\n"
					 "  prepare   Write compose files + Makefile to a directory\n"
					 "  down      Tear down the cluster\n"
					 "  indent    Rewrite a spec with canonical indentation\n"
					 "  wait      Wait until a node reaches a state\n"
					 "  sql       Run SQL on a node and print the result\n"
					 "  network   Connect or disconnect a node from the network\n"
					 "  assert    Assert a node's current state\n"
					 "  help      Show this help message\n"
					 "  demo      Demo application\n",
					 pgaftest_getopts, root_subcommands);
