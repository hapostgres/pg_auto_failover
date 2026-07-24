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

/* forward declarations */
static void resolve_interactive_context(void);
extern CommandLine pgaftest_root;

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


/*
 * pgaftest_last_file — path of the "last active work dir" pointer file.
 * Written on every successful cluster startup; read as the default workDir
 * when --work-dir is not given and no spec file is available.
 */
static void
pgaftest_last_file(char *buf, int buflen)
{
	const char *tmpdir = getenv("TMPDIR"); /* IGNORE-BANNED */
	if (!tmpdir || *tmpdir == '\0')
	{
		tmpdir = "/tmp";
	}
	sformat(buf, buflen, "%s/pgaftest/.last", tmpdir);
}


/*
 * pgaftest_write_last — record workDir as the most-recently-started cluster.
 */
static void
pgaftest_write_last(const char *workDir)
{
	char path[1024];
	pgaftest_last_file(path, sizeof(path));

	FILE *f = fopen(path, "w"); /* IGNORE-BANNED */
	if (!f)
	{
		/* non-fatal: .last is a convenience, not required for correctness */
		return;
	}
	fprintf(f, "%s\n", workDir); /* IGNORE-BANNED */
	fclose(f); /* IGNORE-BANNED */
}


/*
 * pgaftest_read_last — fill buf with the last active work dir, or leave it
 * unchanged if the file doesn't exist or is empty.
 */
static void
pgaftest_read_last(char *buf, int buflen)
{
	char path[1024];
	pgaftest_last_file(path, sizeof(path));

	FILE *f = fopen(path, "r"); /* IGNORE-BANNED */
	if (!f)
	{
		return;
	}

	char line[1024] = { 0 };
	if (fgets(line, sizeof(line), f))
	{
		/* strip trailing newline */
		char *nl = strchr(line, '\n');
		if (nl)
		{
			*nl = '\0';
		}
		if (line[0] != '\0')
		{
			strlcpy(buf, line, buflen);
		}
	}
	fclose(f); /* IGNORE-BANNED */
}


static struct option long_options[] = {
	{ "work-dir", required_argument, NULL, 'w' },
	{ "schedule", required_argument, NULL, 'S' },
	{ "expected", required_argument, NULL, 'E' },
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

	while ((c = getopt_long(argc, argv, "w:S:E:nvdh",
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
		fclose(f); /* IGNORE-BANNED */

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
		log_error("Usage: pgaftest cluster setup [--work-dir <dir>] <spec.pgaf>");
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

	pgaftest_write_last(pgaftestOpts.workDir);
	bool ok = runner_setup(spec, pgaftestOpts.workDir, false);
	exit(ok ? 0 : 1);
}


static void
cli_tmux(int argc, char **argv)
{
	if (argc < 1 || argv[0] == NULL)
	{
		log_error("Usage: pgaftest tmux [--work-dir <dir>] <spec.pgaf>");
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

	pgaftest_write_last(pgaftestOpts.workDir);
	bool ok = runner_setup(spec, pgaftestOpts.workDir, true);
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
	fclose(f); /* IGNORE-BANNED */
	(void) spec; /* parsed for validation; content comes from the file */
	exit(0);
}


static void
cli_show_steps(int argc, char **argv)
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
cli_show_step(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);

	TestRunnerState st;
	runner_state_read(pgaftestOpts.workDir, &st);

	int idx = st.current;
	if (idx >= spec->sequenceLength)
	{
		fformat(stdout, "All %d steps complete.\n", spec->sequenceLength);
		exit(0);
	}

	const char *name = spec->sequence[idx];
	TestStep *step = spec_find_step(spec, name);
	if (!step)
	{
		log_error("Step \"%s\" not found in spec", name);
		exit(1);
	}

	fformat(stdout, "step %s {\n", name);
	for (TestCmd *cmd = step->commands; cmd != NULL; cmd = cmd->next)
	{
		test_cmd_print(stdout, cmd, 4);
	}
	fformat(stdout, "}\n");
	exit(0);
}


static void
cli_show_state(int argc, char **argv)
{
	TestSpec *spec = show_resolve_spec(argc, argv);
	bool ok = runner_show_state(spec, pgaftestOpts.workDir);
	exit(ok ? 0 : 1);
}


/* -----------------------------------------------------------------------
 * pgaftest down
 * ----------------------------------------------------------------------- */
static void
cli_sh(int argc, char **argv)
{
	resolve_interactive_context();

	if (pgaftestOpts.workDir[0] == '\0')
	{
		log_error("Cannot determine work directory: "
				  "provide --work-dir, a spec file argument, "
				  "or run from inside the pgaftest container");
		exit(1);
	}

	const char *base = strrchr(pgaftestOpts.workDir, '/');
	const char *projectName = (base && *(base + 1)) ? base + 1
							  : pgaftestOpts.workDir;

	char cmd[2048];
	sformat(cmd, sizeof(cmd),
			"docker compose -p %s -f %s/docker-compose.yml run --rm -it setup bash",
			projectName, pgaftestOpts.workDir);

	int rc = system(cmd);
	exit(rc == 0 ? 0 : 1);
}


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
			"Try: pgaftest step"
			"  |  pgaftest network disconnect <node>"
			"  |  pgaftest show state  |  pgaftest down\n\n");

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
		else
		{
			/* last resort: most recently started cluster */
			pgaftest_read_last(pgaftestOpts.workDir, sizeof(pgaftestOpts.workDir));
		}
	}
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
 * pgaftest nodeini get <node> <key>
 * pgaftest nodeini set <node> <key> <value>
 * ----------------------------------------------------------------------- */
static void
cli_nodeini(int argc, char **argv)
{
	bool isGet = (argc >= 1 && strcmp(argv[0], "get") == 0);
	bool isSet = (argc >= 1 && strcmp(argv[0], "set") == 0);

	if ((!isGet && !isSet) ||
		(isGet && argc < 3) ||
		(isSet && argc < 4))
	{
		log_error("Usage: pgaftest nodeini get <node> <key>"
				  "  |  pgaftest nodeini set <node> <key> <value>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	const char *nodeName = argv[1];
	const char *key = argv[2];

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

	if (isSet)
	{
		const char *value = argv[3];
		bool ok = runner_nodeini_set(spec, pgaftestOpts.workDir,
									 nodeName, key, value);
		exit(ok ? 0 : 1);
	}
	else
	{
		char value[4096] = "";
		bool ok = runner_nodeini_get(spec, pgaftestOpts.workDir,
									 nodeName, key, value, sizeof(value));
		if (ok)
		{
			fformat(stdout, "%s\n", value);
		}
		exit(ok ? 0 : 1);
	}
}


/* -----------------------------------------------------------------------
 * pgaftest compose start|stop|kill|down|exec — thin wrappers around
 * `docker compose ...` for the running stack, sparing users from having to
 * remember the -p/-f flags by hand (see the "Manually starting/stopping a
 * node" section in docs/ref/pgaftest.rst).
 * ----------------------------------------------------------------------- */
static bool
compose_base_cmd(char *buf, int buflen)
{
	resolve_interactive_context();

	if (pgaftestOpts.workDir[0] == '\0')
	{
		log_error("Cannot determine work directory: "
				  "provide --work-dir, a spec file argument, "
				  "or run from inside the pgaftest container");
		return false;
	}

	const char *base = strrchr(pgaftestOpts.workDir, '/');
	const char *projectName = (base && *(base + 1)) ? base + 1
							  : pgaftestOpts.workDir;

	sformat(buf, buflen, "docker compose -p %s -f %s/docker-compose.yml",
			projectName, pgaftestOpts.workDir);
	return true;
}


static void
cli_compose_start(int argc, char **argv)
{
	if (argc < 1)
	{
		log_error("Usage: pgaftest compose start <node>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	char base[2048];
	if (!compose_base_cmd(base, sizeof(base)))
	{
		exit(1);
	}

	/*
	 * `up -d --no-recreate --no-deps` rather than `start`: `start` only
	 * works for already-created containers, but a node declared with
	 * "launch deferred" is never created during the initial compose up.
	 */
	char cmd[2200];
	sformat(cmd, sizeof(cmd), "%s up -d --no-recreate --no-deps %s",
			base, argv[0]);

	int rc = system(cmd); /* IGNORE-BANNED */
	exit(rc == 0 ? 0 : 1);
}


static void
cli_compose_stop(int argc, char **argv)
{
	if (argc < 1)
	{
		log_error("Usage: pgaftest compose stop <node>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	char base[2048];
	if (!compose_base_cmd(base, sizeof(base)))
	{
		exit(1);
	}

	char cmd[2200];
	sformat(cmd, sizeof(cmd), "%s stop %s", base, argv[0]);

	int rc = system(cmd); /* IGNORE-BANNED */
	exit(rc == 0 ? 0 : 1);
}


static void
cli_compose_kill(int argc, char **argv)
{
	if (argc < 1)
	{
		log_error("Usage: pgaftest compose kill <node>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	char base[2048];
	if (!compose_base_cmd(base, sizeof(base)))
	{
		exit(1);
	}

	char cmd[2200];
	sformat(cmd, sizeof(cmd), "%s kill %s", base, argv[0]);

	int rc = system(cmd); /* IGNORE-BANNED */
	exit(rc == 0 ? 0 : 1);
}


static void
cli_compose_down(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	char base[2048];
	if (!compose_base_cmd(base, sizeof(base)))
	{
		exit(1);
	}

	char cmd[2200];
	sformat(cmd, sizeof(cmd), "%s down", base);

	int rc = system(cmd); /* IGNORE-BANNED */
	exit(rc == 0 ? 0 : 1);
}


static void
cli_compose_exec(int argc, char **argv)
{
	if (argc < 2)
	{
		log_error("Usage: pgaftest compose exec <node> <args...>");
		exit(EXIT_CODE_BAD_ARGS);
	}

	char base[2048];
	if (!compose_base_cmd(base, sizeof(base)))
	{
		exit(1);
	}

	/* re-join argv[1..] into a single shell-visible argument string */
	char rest[4096] = "";
	for (int i = 1; i < argc; i++)
	{
		if (i > 1)
		{
			strlcat(rest, " ", sizeof(rest));
		}
		strlcat(rest, argv[i], sizeof(rest));
	}

	char cmd[8192];
	sformat(cmd, sizeof(cmd), "%s exec -it %s %s", base, argv[0], rest);

	int rc = system(cmd); /* IGNORE-BANNED */
	exit(rc == 0 ? 0 : 1);
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

static CommandLine show_steps_command =
	make_command("steps",
				 "List steps in sequence order with * on the current/next step",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n"
				 "\n"
				 "  Markers: *=next to run  !=failed (will retry)  (space)=done\n",
				 pgaftest_getopts, cli_show_steps);

static CommandLine show_step_command =
	make_command("step",
				 "Show commands for the next step to run",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n",
				 pgaftest_getopts, cli_show_step);

static CommandLine show_state_command =
	make_command("state",
				 "Show pg_autoctl show state output with step progress header",
				 "[<spec.pgaf>]",
				 "  <spec.pgaf>   Path to spec (default: PGAFTEST_SPEC or /spec.pgaf)\n",
				 pgaftest_getopts, cli_show_state);

static CommandLine *show_subcommands[] = {
	&show_compose_command,
	&show_spec_command,
	&show_steps_command,
	&show_step_command,
	&show_state_command,
	NULL
};

static CommandLine show_command =
	make_command_set("show",
					 "Show information about a spec or running cluster",
					 "<compose|spec|steps|step|state> [<spec.pgaf>]",
					 "  compose    Print the generated docker-compose.yml\n"
					 "  spec       Print the spec file source\n"
					 "  steps      List steps with * on next / ! on failed\n"
					 "  step       Show commands for the next step\n"
					 "  state      Show pg_autoctl show state with step header\n",
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

static CommandLine sh_command =
	make_command("sh",
				 "Open a bash shell in the running pgaftest container",
				 "[options]",
				 "  --work-dir <dir>   Working directory (default: derived from spec)\n",
				 pgaftest_getopts, cli_sh);

static CommandLine *cluster_subcommands[] = {
	&setup_command,
	&prepare_command,
	&down_command,
	&sh_command,
	NULL
};

static CommandLine cluster_command =
	make_command_set("cluster",
					 "Manage the cluster lifecycle (bring up, prepare, tear down)",
					 "<setup|prepare|down|sh> [options]",
					 "  setup     Bring up a cluster interactively\n"
					 "  prepare   Write compose files + Makefile to a directory\n"
					 "  down      Tear down the cluster\n"
					 "  sh        Open a bash shell in the pgaftest container\n",
					 pgaftest_getopts, cluster_subcommands);

static CommandLine tmux_command =
	make_command("tmux",
				 "Bring up a cluster and launch a tmux session",
				 "[options] <spec.pgaf>",
				 "  --work-dir <dir>   Working directory\n"
				 "                     (default: $TMPDIR/pgaftest/<testname>)\n"
				 "  --verbose          Enable DEBUG log level\n"
				 "  --debug            Enable TRACE log level\n",
				 pgaftest_getopts, cli_tmux);

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

static CommandLine nodeini_command =
	make_command("nodeini",
				 "Read or edit a node's host-side .ini [settings] entry directly (interactive)",
				 "get <node> <key>  |  set <node> <key> <value>",
				 "  get <node> <key>          Print the current value\n"
				 "  set <node> <key> <value>  Write a new value (exercises the\n"
				 "                            supervisor's file-watch live-apply path)\n",
				 pgaftest_getopts, cli_nodeini);

static CommandLine compose_start_command =
	make_command("start",
				 "Start a stopped or deferred node",
				 "<node>",
				 "",
				 pgaftest_getopts, cli_compose_start);

static CommandLine compose_stop_command =
	make_command("stop",
				 "Stop a running node (graceful)",
				 "<node>",
				 "",
				 pgaftest_getopts, cli_compose_stop);

static CommandLine compose_kill_command =
	make_command("kill",
				 "Kill a running node (SIGKILL, no grace)",
				 "<node>",
				 "",
				 pgaftest_getopts, cli_compose_kill);

static CommandLine compose_down_command =
	make_command("down",
				 "Tear down the compose stack (no teardown{} block; see `pgaftest down`)",
				 "",
				 "",
				 pgaftest_getopts, cli_compose_down);

static CommandLine compose_exec_command =
	make_command("exec",
				 "Run a command inside a node's container (interactive TTY)",
				 "<node> <args...>",
				 "",
				 pgaftest_getopts, cli_compose_exec);

static CommandLine *compose_subcommands[] = {
	&compose_start_command,
	&compose_stop_command,
	&compose_kill_command,
	&compose_down_command,
	&compose_exec_command,
	NULL
};

static CommandLine compose_command =
	make_command_set("compose",
					 "Control individual compose services directly (interactive)",
					 "<start|stop|kill|down|exec> <node> [args...]",
					 "  start <node>          Start a stopped or deferred node\n"
					 "  stop <node>           Stop a running node (graceful)\n"
					 "  kill <node>           Kill a running node (SIGKILL)\n"
					 "  down                  Tear down the compose stack\n"
					 "  exec <node> <args...> Run a command inside a node's container\n",
					 pgaftest_getopts, compose_subcommands);

static void
cli_help(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	commandline_print_command_tree(&pgaftest_root, stdout);

	if (getenv("PGAFTEST_IN_CONTAINER")) /* IGNORE-BANNED */
	{
		printf( /* IGNORE-BANNED */
			"\n"
			"Interactive session quick-start (run these inside the container):\n"
			"\n"
			"  pgaftest show state    # cluster FSM state from pg_autoctl show state\n"
			"  pgaftest show steps    # sequence steps with progress markers\n"
			"  pgaftest show step     # DSL commands that will run on the next step\n"
			"  pgaftest step          # run the next pending step (auto-advance)\n"
			"  pgaftest step <name>   # run a specific named step\n"
			"\n"
			);
	}

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
	&cluster_command,
	&tmux_command,
	&step_command,
	&show_command,
	&indent_command,
	&sql_command,
	&network_command,
	&nodeini_command,
	&compose_command,
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
					 "  cluster   Manage the cluster lifecycle\n"
					 "  tmux      Bring up a cluster with a tmux session\n"
					 "  step      Run one named step\n"
					 "  show      Show information (compose, spec, steps, step, state)\n"
					 "  indent    Rewrite a spec with canonical indentation\n"
					 "  sql       Run SQL on a node and print the result\n"
					 "  network   Connect or disconnect a node from the network\n"
					 "  nodeini   Read or edit a node's .ini file directly\n"
					 "  compose   Control individual compose services (start/stop/kill/exec)\n"
					 "  help      Show this help message\n"
					 "  demo      Demo application\n",
					 pgaftest_getopts, root_subcommands);
