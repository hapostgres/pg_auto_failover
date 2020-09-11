/*
 * src/bin/pg_autoctl/cli_do_misc.c
 *     Implementation of a CLI which lets you run operations on the local
 *     postgres server directly.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "string_utils.h"


typedef struct TmuxOptions
{
	char root[MAXPGPATH];
	int firstPort;
	int nodes;
	char layout[BUFSIZE];
} TmuxOptions;

static TmuxOptions tmuxOptions = { 0 };


static void tmux_add_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

static void tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

static void tmux_add_xdg_environment(PQExpBuffer script, const char *root);

static void tmux_pg_autoctl_create(PQExpBuffer script,
								   const char *root,
								   int pgport,
								   const char *role,
								   const char *name);

/*
 * cli_print_version_getopts parses the CLI options for the pg_autoctl version
 * command, which are the usual suspects.
 */
int
cli_do_tmux_script_getopts(int argc, char **argv)
{
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;
	bool printVersion = false;

	TmuxOptions options = { 0 };

	static struct option long_options[] = {
		{ "root", required_argument, NULL, 'D' },
		{ "first-port", required_argument, NULL, 'p' },
		{ "nodes", required_argument, NULL, 'n' },
		{ "layout", required_argument, NULL, 'l' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	/* set our defaults */
	options.nodes = 2;
	options.firstPort = 5500;
	strlcpy(options.root, "/tmp/pgaf/tmux", sizeof(options.root));
	strlcpy(options.layout, "even-vertical", sizeof(options.layout));

	/*
	 * The only command lines that are using keeper_cli_getopt_pgdata are
	 * terminal ones: they don't accept subcommands. In that case our option
	 * parsing can happen in any order and we don't need getopt_long to behave
	 * in a POSIXLY_CORRECT way.
	 *
	 * The unsetenv() call allows getopt_long() to reorder arguments for us.
	 */
	unsetenv("POSIXLY_CORRECT");

	while ((c = getopt_long(argc, argv, "D:p:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.root, optarg, MAXPGPATH);
				log_trace("--root %s", options.root);
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &options.firstPort))
				{
					log_error("Failed to parse --first-port number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--first-port %d", options.firstPort);
				break;
			}

			case 'n':
			{
				if (!stringToInt(optarg, &options.nodes))
				{
					log_error("Failed to parse --nodes number \"%s\"",
							  optarg);
					errors++;
				}
				log_trace("--nodes %d", options.nodes);
				break;
			}

			case 'l':
			{
				strlcpy(options.layout, optarg, MAXPGPATH);
				log_trace("--layout %s", options.layout);
				break;
			}

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				printVersion = true;
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

			default:
			{
				/* getopt_long already wrote an error message */
				errors++;
				break;
			}
		}
	}

	if (errors > 0)
	{
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (printVersion)
	{
		keeper_cli_print_version(argc, argv);
	}

	/* publish parsed options */
	tmuxOptions = options;

	return optind;
}


/*
 * tmux_add_command appends a tmux command to the given script buffer.
 */
static void
tmux_add_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[BUFSIZE] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "%s\n", buffer);
}


/*
 * tmux_add_send_keys_command appends a tmux send-keys command to the given
 * script buffer, with an additional Enter command.
 */
static void
tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
{
	char buffer[BUFSIZE] = { 0 };
	va_list args;

	va_start(args, fmt);
	pg_vsprintf(buffer, fmt, args);
	va_end(args);

	appendPQExpBuffer(script, "send-keys '%s' Enter\n", buffer);
}


/*
 * tmux_add_xdg_environment appends the XDG environment that makes the test
 * target self-contained, as a series of tmux send-keys commands, to the given
 * script buffer.
 */
static void
tmux_add_xdg_environment(PQExpBuffer script, const char *root)
{
	char *xdg[][3] = {
		{ "XDG_DATA_HOME", "share" },
		{ "XDG_CONFIG_HOME", "config" },
		{ "XDG_RUNTIME_DIR", "run" },
	};

	/*
	 * For demo/tests purposes, arrange a self-contained setup where everything
	 * is to be found in the given options.root directory.
	 */
	for (int i = 0; i < 3; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];

		tmux_add_send_keys_command(script,
								   "export %s=\"%s/%s\"", var, root, dir);
	}
}


/*
 * tmux_pg_autoctl_create appends a pg_autoctl create command to the given
 * script buffer, and also the commands to set PGDATA and PGPORT.
 */
static void
tmux_pg_autoctl_create(PQExpBuffer script,
					   const char *root,
					   int pgport,
					   const char *role,
					   const char *name)
{
	char *pg_ctl_opts = "--hostname localhost --ssl-self-signed --auth trust";

	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "export PGPORT=%d", pgport);

	/* the monitor is always named monitor, and does not need --monitor */
	if (strcmp(role, "monitor") == 0)
	{
		tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

		tmux_add_send_keys_command(script,
								   "%s create %s %s --run",
								   pg_autoctl_argv0,
								   role,
								   pg_ctl_opts);
	}
	else
	{
		char monitor[BUFSIZE] = { 0 };

		sformat(monitor, sizeof(monitor),
				"$(%s show uri --pgdata %s/monitor --monitor)",
				pg_autoctl_argv0,
				root);

		tmux_add_send_keys_command(script,
								   "export PGDATA=\"%s/%s\"",
								   root,
								   name);

		tmux_add_send_keys_command(script,
								   "%s create %s %s --monitor %s --run",
								   pg_autoctl_argv0,
								   role,
								   pg_ctl_opts,
								   monitor);
	}
}


/*
 * keeper_cli_tmux_script generates a tmux script to run a test case or a demo
 * for pg_auto_failover easily.
 */
void
cli_do_tmux_script(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;

	char *root = options.root;
	PQExpBuffer script = createPQExpBuffer();

	int pgport = options.firstPort;

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	tmux_add_command(script, "set-option -g default-shell /bin/bash");
	tmux_add_command(script, "new -s pgautofailover");

	/* start a monitor */
	tmux_pg_autoctl_create(script, root, pgport++, "monitor", "monitor");

	/* start the Postgres nodes, using the monitor URI */
	for (int i = 0; i < options.nodes; i++)
	{
		char name[NAMEDATALEN] = { 0 };

		sformat(name, sizeof(name), "node%d", i + 1);

		tmux_add_command(script, "split-window -v");
		tmux_add_command(script, "select-layout even-vertical");

		/* allow some time for each previous node to be ready */
		tmux_add_send_keys_command(script, "sleep %d", 3 * (i + 1));
		tmux_pg_autoctl_create(script, root, pgport++, "postgres", name);
		tmux_add_send_keys_command(script, "pg_autoctl run");
	}

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);
	tmux_add_send_keys_command(script,
							   "watch -n 0.2 %s show state",
							   pg_autoctl_argv0);

	/* add a window for interactive pg_autoctl commands */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

	/* now select our target layout */
	tmux_add_command(script, "select-layout %s", options.layout);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s", script->data);
	destroyPQExpBuffer(script);

	if (env_exists("TMUX_EXTRA_COMMANDS"))
	{
		char extra_commands[BUFSIZE] = { 0 };

		char *extraLines[BUFSIZE];
		int lineCount = 0;
		int lineNumber = 0;

		if (!get_env_copy("TMUX_EXTRA_COMMANDS", extra_commands, BUFSIZE))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		lineCount = splitLines(extra_commands, extraLines, BUFSIZE);

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			fformat(stdout, "%s\n", extraLines[lineNumber]);
		}
	}
}
