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
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <termios.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "string_utils.h"

#include "runprogram.h"

typedef struct TmuxOptions
{
	char root[MAXPGPATH];
	int firstPort;
	int nodes;
	char layout[BUFSIZE];
} TmuxOptions;

static TmuxOptions tmuxOptions = { 0 };

char *xdg[][3] = {
	{ "XDG_DATA_HOME", "share" },
	{ "XDG_CONFIG_HOME", "config" },
	{ "XDG_RUNTIME_DIR", "run" },
	{ NULL, NULL }
};


static void tmux_add_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

static void tmux_add_send_keys_command(PQExpBuffer script, const char *fmt, ...)
__attribute__((format(printf, 2, 3)));

static void tmux_add_xdg_environment(PQExpBuffer script, const char *root);
static bool tmux_prepare_XDG_environment(const char *root);

static void tmux_pg_autoctl_create_monitor(PQExpBuffer script,
										   const char *root,
										   int pgport,
										   bool setXDG);

static void tmux_pg_autoctl_create_postgres(PQExpBuffer script,
											const char *root,
											int pgport,
											const char *name,
											bool setXDG);

static bool tmux_start_server(const char *root, const char *scriptName);
static bool pg_autoctl_stop(const char *root, const char *name);
static bool tmux_stop_pg_autoctl(TmuxOptions *options);


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
		{ "first-pgport", required_argument, NULL, 'p' },
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
 * tmux_prepare_XDG_environment set XDG environment variables in the current
 * process tree.
 */
static bool
tmux_prepare_XDG_environment(const char *root)
{
	for (int i = 0; xdg[i][0] != NULL; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];
		char *env = (char *) malloc(MAXPGPATH * sizeof(char));

		if (env == NULL)
		{
			log_fatal("Failed to malloc MAXPGPATH bytes: %m");
			return false;
		}

		sformat(env, MAXPGPATH, "%s/%s", root, dir);

		log_debug("mkdir -p \"%s\"", env);
		if (!ensure_empty_dir(env, 0700))
		{
			/* errors have already been logged */
			return false;
		}

		if (!normalize_filename(env, env, MAXPGPATH))
		{
			/* errors have already been logged */
			return false;
		}

		log_info("export %s=\"%s\"", var, env);

		if (setenv(var, env, 1) != 0)
		{
			log_error("Failed to set environment variable %s to \"%s\": %m",
					  var, env);
		}
	}

	return true;
}


/*
 * tmux_add_xdg_environment appends the XDG environment that makes the test
 * target self-contained, as a series of tmux send-keys commands, to the given
 * script buffer.
 */
static void
tmux_add_xdg_environment(PQExpBuffer script, const char *root)
{
	/*
	 * For demo/tests purposes, arrange a self-contained setup where everything
	 * is to be found in the given options.root directory.
	 */
	for (int i = 0; xdg[i][0] != NULL; i++)
	{
		char *var = xdg[i][0];
		char *dir = xdg[i][1];

		tmux_add_send_keys_command(script,
								   "export %s=\"%s/%s\"", var, root, dir);
	}
}


/*
 * tmux_pg_autoctl_create_monitor appends a pg_autoctl create monitor command
 * to the given script buffer, and also the commands to set PGDATA and PGPORT.
 */
static void
tmux_pg_autoctl_create_monitor(PQExpBuffer script,
							   const char *root,
							   int pgport,
							   bool setXDG)
{
	char *pg_ctl_opts = "--hostname localhost --ssl-self-signed --auth trust";

	if (setXDG)
	{
		tmux_add_xdg_environment(script, root);
	}

	tmux_add_send_keys_command(script, "export PGPORT=%d", pgport);

	/* the monitor is always named monitor, and does not need --monitor */
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

	tmux_add_send_keys_command(script,
							   "%s create monitor %s --run",
							   pg_autoctl_argv0,
							   pg_ctl_opts);
}


/*
 * tmux_pg_autoctl_create_postgres appends a pg_autoctl create postgres command
 * to the given script buffer, and also the commands to set PGDATA and PGPORT.
 */
static void
tmux_pg_autoctl_create_postgres(PQExpBuffer script,
								const char *root,
								int pgport,
								const char *name,
								bool setXDG)
{
	char monitor[BUFSIZE] = { 0 };
	char *pg_ctl_opts = "--hostname localhost --ssl-self-signed --auth trust";

	if (setXDG)
	{
		tmux_add_xdg_environment(script, root);
	}

	tmux_add_send_keys_command(script, "export PGPORT=%d", pgport);


	sformat(monitor, sizeof(monitor),
			"$(%s show uri --pgdata %s/monitor --monitor)",
			pg_autoctl_argv0,
			root);

	tmux_add_send_keys_command(script,
							   "export PGDATA=\"%s/%s\"",
							   root,
							   name);

	tmux_add_send_keys_command(script,
							   "%s create postgres %s "
							   "--monitor %s --name %s --run",
							   pg_autoctl_argv0,
							   pg_ctl_opts,
							   monitor,
							   name);
}


/*
 * prepare_tmux_script prepares a script for a tmux session with the given
 * nodes, root directory, first pgPort, and layout.
 *
 * This script can be saved to disk and used later, or used straight away for
 * an interactive session. When used for interactive session, then the XDG
 * environment variables are set in the main pg_autoctl process (running this
 * code), and inherited in all the shells in the tmux sessions thereafter.
 *
 * As a result in that case we don't need to include the XDG environment
 * settings in the tmux script itself.
 */
static void
prepare_tmux_script(TmuxOptions *options, PQExpBuffer script, bool setXDG)
{
	char *root = options->root;
	int pgport = options->firstPort;
	char sessionName[BUFSIZE] = { 0 };

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", options->firstPort);

	tmux_add_command(script, "set-option -g default-shell /bin/bash");
	tmux_add_command(script, "new-session -s %s", sessionName);

	/* start a monitor */
	tmux_pg_autoctl_create_monitor(script, root, pgport++, setXDG);

	/* start the Postgres nodes, using the monitor URI */
	for (int i = 0; i < options->nodes; i++)
	{
		char name[NAMEDATALEN] = { 0 };

		sformat(name, sizeof(name), "node%d", i + 1);

		tmux_add_command(script, "split-window -v");
		tmux_add_command(script, "select-layout even-vertical");

		/* ensure that the first node is always the primary */
		if (i == 0)
		{
			/* on the primary, wait until the monitor is ready */
			tmux_add_send_keys_command(script, "sleep 2");
			tmux_add_send_keys_command(script,
									   "%s do pgsetup wait --pgdata %s/monitor",
									   pg_autoctl_argv0,
									   root);
		}
		else
		{
			/* on the other nodes, wait until the primary is ready */
			tmux_add_send_keys_command(script, "sleep 2");
			tmux_add_send_keys_command(script,
									   "%s do pgsetup wait --pgdata %s/node1",
									   pg_autoctl_argv0,
									   root);
		}

		tmux_pg_autoctl_create_postgres(script, root, pgport++, name, setXDG);
		tmux_add_send_keys_command(script, "pg_autoctl run");
	}

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	if (setXDG)
	{
		tmux_add_xdg_environment(script, root);
	}
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);
	tmux_add_send_keys_command(script,
							   "watch -n 0.2 %s show state",
							   pg_autoctl_argv0);

	/* add a window for interactive pg_autoctl commands */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	/* seems that we need to inconditionnaly set the XDG env in this one */
	tmux_add_xdg_environment(script, root);
	tmux_add_send_keys_command(script, "cd \"%s\"", root);
	tmux_add_send_keys_command(script, "export PGDATA=\"%s/monitor\"", root);

	/* now select our target layout */
	tmux_add_command(script, "select-layout %s", options->layout);

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
			appendPQExpBuffer(script, "%s\n", extraLines[lineNumber]);
		}
	}
}


/*
 * tmux_start_server starts a tmux session with the given script.
 */
static bool
tmux_start_server(const char *root, const char *scriptName)
{
	Program program;

	char *args[8];
	int argsIndex = 0;

	char tmux[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(root))
	{
		return false;
	}

	if (setenv("PG_AUTOCTL_DEBUG", "1", 1) != 0)
	{
		log_error("Failed to set environment PG_AUTOCTL_DEBUG: %m");
		return false;
	}

	if (!search_path_first("tmux", tmux))
	{
		log_fatal("Failed to find program tmux in PATH");
		return false;
	}

	/*
	 * Run the tmux command with our script:
	 *   tmux start-server \; source-file ${scriptName}
	 */
	args[argsIndex++] = (char *) tmux;
	args[argsIndex++] = "-v";
	args[argsIndex++] = "start-server";
	args[argsIndex++] = ";";
	args[argsIndex++] = "source-file";
	args[argsIndex++] = (char *) scriptName;
	args[argsIndex] = NULL;

	/* we do not want to call setsid() when running this program. */
	program = initialize_program(args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	/* log the exact command line we're using */
	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	(void) execute_subprogram(&program);

	/* we only get there when the tmux session is done */
	return true;
}


/*
 * pg_autoctl_stop calls pg_autoctl stop --pgdata ${root}/${name}
 */
static bool
pg_autoctl_stop(const char *root, const char *name)
{
	Program program;
	char command[BUFSIZE] = { 0 };
	char pgdata[MAXPGPATH] = { 0 };

	bool success = true;

	sformat(pgdata, sizeof(pgdata), "%s/%s", root, name);

	program = run_program(pg_autoctl_argv0, "stop", "--pgdata", pgdata, NULL);

	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	if (program.stdErr != NULL)
	{
		char *errorLines[BUFSIZE];
		int lineCount = splitLines(program.stdErr, errorLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			fformat(stderr, "%s\n", errorLines[lineNumber]);
		}
	}

	if (program.returnCode != 0)
	{
		success = false;
		log_warn("Failed to stop pg_autoctl for \"%s\"", pgdata);
	}

	free_program(&program);

	return success;
}


/*
 * tmux_stop_pg_autoctl stops all started pg_autoctl programs in a tmux
 * sessions.
 */
static bool
tmux_stop_pg_autoctl(TmuxOptions *options)
{
	bool success = true;

	/* first stop all the nodes */
	for (int i = 0; i < options->nodes; i++)
	{
		char name[NAMEDATALEN] = { 0 };

		sformat(name, sizeof(name), "node%d", i + 1);

		success = success && pg_autoctl_stop(options->root, name);
	}

	/* and then the monitor */
	success = success && pg_autoctl_stop(options->root, "monitor");

	return success;
}


/*
 * tmux_kill_session runs the command:
 *   tmux kill-session -t pgautofailover-${first-pgport}
 */
static bool
tmux_kill_session(TmuxOptions *options)
{
	Program program;
	char tmux[MAXPGPATH] = { 0 };
	char command[BUFSIZE] = { 0 };
	char sessionName[BUFSIZE] = { 0 };

	bool success = true;

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", options->firstPort);

	if (!search_path_first("tmux", tmux))
	{
		log_fatal("Failed to find program tmux in PATH");
		return false;
	}

	program = run_program(tmux, "kill-session", "-t", sessionName, NULL);

	(void) snprintf_program_command_line(&program, command, BUFSIZE);
	log_info("%s", command);

	if (program.stdOut)
	{
		fformat(stdout, "%s", program.stdOut);
	}

	if (program.stdErr)
	{
		fformat(stderr, "%s", program.stdErr);
	}

	if (program.returnCode != 0)
	{
		success = false;
		log_warn("Failed to kill tmux sessions \"%s\"", sessionName);
	}

	free_program(&program);

	return success;
}


/*
 * keeper_cli_tmux_script generates a tmux script to run a test case or a demo
 * for pg_auto_failover easily.
 */
void
cli_do_tmux_script(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the tmux script */
	(void) prepare_tmux_script(&options, script, true);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	fformat(stdout, "%s", script->data);

	destroyPQExpBuffer(script);
}


/*
 * cli_do_tmux_session starts an interactive tmux session with the given
 * specifications for a cluster. When the session is detached, the pg_autoctl
 * processes are stopped.
 */
void
cli_do_tmux_session(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	char scriptName[MAXPGPATH] = { 0 };

	bool success = true;

	/*
	 * Write the script to "script-${first-pgport}.tmux" file in the root
	 * directory.
	 */
	log_debug("mkdir -p \"%s\"", options.root);
	if (!ensure_empty_dir(options.root, 0700))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!normalize_filename(options.root, options.root, sizeof(options.root)))
	{
		/* errors have already been logged. */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Prepare the tmux script.
	 */
	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) prepare_tmux_script(&options, script, false);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Write the script to file.
	 */
	sformat(scriptName, sizeof(scriptName),
			"%s/script-%d.tmux", options.root, options.firstPort);

	log_info("Writing tmux session script \"%s\"", scriptName);

	if (!write_file(script->data, script->len, scriptName))
	{
		log_fatal("Failed to write tmux script at \"%s\"", scriptName);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	destroyPQExpBuffer(script);

	/*
	 * Start a tmux session from the script.
	 */
	if (!tmux_start_server(options.root, scriptName))
	{
		success = false;
		log_fatal("Failed to start the tmux session, see above for details");
	}

	/*
	 * Stop our pg_autoctl processes and kill the tmux session.
	 */
	log_info("tmux session ended: kill pg_autoct processes");
	success = success && tmux_stop_pg_autoctl(&options);
	success = success && tmux_kill_session(&options);

	if (!success)
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * cli_do_tmux_stop runs pg_autoctl stop on all the pg_autoctl process that
 * might be running in a tmux session.
 */
void
cli_do_tmux_stop(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;

	/* prepare the XDG environment */
	if (!tmux_prepare_XDG_environment(options.root))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (!tmux_stop_pg_autoctl(&options))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}
