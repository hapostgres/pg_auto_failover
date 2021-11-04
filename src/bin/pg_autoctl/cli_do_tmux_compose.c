/*
 * src/bin/pg_autoctl/cli_do_tmux_compose.c
 *     Implementation of a CLI which lets you run operations on a local
 *     docker-compose environment with multiple Postgres nodes.
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

#if defined(__linux__)
#include <linux/limits.h>
#endif

#include "postgres_fe.h"
#include "pqexpbuffer.h"
#include "snprintf.h"

#include "cli_common.h"
#include "cli_do_root.h"
#include "cli_do_tmux.h"
#include "cli_root.h"
#include "commandline.h"
#include "config.h"
#include "env_utils.h"
#include "log.h"
#include "pidfile.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

char *tmux_compose_banner[] = {
	"# to quit tmux: type either `Ctrl+b d` or `tmux detach`",
	"# to test failover: docker-compose exec monitor pg_autoctl perform failover",
	NULL
};


static void prepare_tmux_compose_script(TmuxOptions *options, PQExpBuffer script);
static void prepare_tmux_compose_config(TmuxOptions *options, PQExpBuffer script);

static void tmux_compose_create_volumes(TmuxOptions *options);
static void tmux_compose_docker_build(TmuxOptions *options);
static bool tmux_compose_down(TmuxOptions *options);


/*
 * prepare_tmux_compose_script prepares a script for a tmux session with the
 * given nodes, root directory, first pgPort, and layout.
 */
static void
prepare_tmux_compose_script(TmuxOptions *options, PQExpBuffer script)
{
	char sessionName[BUFSIZE] = { 0 };

	sformat(sessionName, BUFSIZE, "pgautofailover-%d", options->firstPort);

	tmux_add_command(script, "set-option -g default-shell /bin/bash");
	tmux_add_command(script, "new-session -s %s", sessionName);

	/* change to the user given options->root directory */
	tmux_add_send_keys_command(script, "cd \"%s\"", options->root);

	/* docker-compose */
	tmux_add_send_keys_command(script, "docker-compose up -d");
	tmux_add_send_keys_command(script, "docker-compose logs -f");

	/* add a window for pg_autoctl show state */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	/* wait for the docker volumes to be initialized in docker-compose up -d */
	tmux_add_send_keys_command(script, "sleep 5");
	tmux_add_send_keys_command(script,
							   "docker-compose exec monitor "
							   "pg_autoctl watch");

	/* add a window for interactive pg_autoctl commands */
	tmux_add_command(script, "split-window -v");
	tmux_add_command(script, "select-layout even-vertical");

	if (options->numSync != -1)
	{
		/* wait for the monitor to be up-and-running */
		tmux_add_send_keys_command(script, "sleep 10");
		tmux_add_send_keys_command(
			script,
			"docker-compose exec monitor "
			"pg_autoctl set formation number-sync-standbys %d",
			options->numSync);
	}

	/* now select our target layout */
	tmux_add_command(script, "select-layout %s", options->layout);

	if (env_exists("TMUX_EXTRA_COMMANDS"))
	{
		char extra_commands[BUFSIZE] = { 0 };

		char *extraLines[BUFSIZE];
		int lineNumber = 0;

		if (!get_env_copy("TMUX_EXTRA_COMMANDS", extra_commands, BUFSIZE))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		int lineCount = splitLines(extra_commands, extraLines, BUFSIZE);

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			appendPQExpBuffer(script, "%s\n", extraLines[lineNumber]);
		}
	}

	for (int i = 0; tmux_compose_banner[i] != NULL; i++)
	{
		tmux_add_send_keys_command(script, "%s", tmux_compose_banner[i]);
	}
}


/*
 * tmux_compose_add_monitor adds a docker-compose service for the monitor node.
 */
static void
tmux_compose_add_monitor(PQExpBuffer script)
{
	char currentWorkingDirectory[MAXPGPATH] = { 0 };

	if (getcwd(currentWorkingDirectory, MAXPGPATH) == NULL)
	{
		log_error("Failed to get the current working directory: %m");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	appendPQExpBuffer(script, "  monitor:\n");
	appendPQExpBuffer(script, "    build: %s\n", currentWorkingDirectory);
	appendPQExpBuffer(script, "    hostname: monitor\n");
	appendPQExpBuffer(script, "    volumes:\n");
	appendPQExpBuffer(script, "      - monitor_data:/var/lib/postgres:rw\n");
	appendPQExpBuffer(script, "    environment:\n");
	appendPQExpBuffer(script, "      PGDATA: /var/lib/postgres/pgaf\n");
	appendPQExpBuffer(script, "    expose:\n");
	appendPQExpBuffer(script, "     - 5432\n");
	appendPQExpBuffer(script, "    command: "
							  "pg_autoctl create monitor "
							  " --ssl-self-signed --auth trust --run\n");
}


/*
 * tmux_compose_add_monitor adds a docker-compose service for the given
 * Postgres node.
 */
static void
tmux_compose_add_node(PQExpBuffer script,
					  const char *name,
					  const char *pguser,
					  const char *dbname,
					  const char *monitor_pguri)
{
	char currentWorkingDirectory[MAXPGPATH] = { 0 };

	if (getcwd(currentWorkingDirectory, MAXPGPATH) == NULL)
	{
		log_error("Failed to get the current working directory: %m");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	appendPQExpBuffer(script, "  %s:\n", name);
	appendPQExpBuffer(script, "    build: %s\n", currentWorkingDirectory);
	appendPQExpBuffer(script, "    hostname: %s\n", name);
	appendPQExpBuffer(script, "    volumes:\n");
	appendPQExpBuffer(script, "      - %s_data:/var/lib/postgres:rw\n", name);
	appendPQExpBuffer(script, "    environment:\n");
	appendPQExpBuffer(script, "      PGDATA: /var/lib/postgres/pgaf\n");
	appendPQExpBuffer(script, "      PGUSER: %s\n", pguser);
	appendPQExpBuffer(script, "      PGDATABASE: %s\n", dbname);
	appendPQExpBuffer(script, "      PG_AUTOCTL_MONITOR: \"%s\"\n", monitor_pguri);
	appendPQExpBuffer(script, "    expose:\n");
	appendPQExpBuffer(script, "     - 5432\n");
	appendPQExpBuffer(script, "    command: "
							  "pg_autoctl create postgres "
							  "--ssl-self-signed --auth trust "
							  "--pg-hba-lan --run\n");
}


/*
 * tmux_compose_add_volume adds a docker-compose volume for the given node
 * name.
 */
static void
tmux_compose_add_volume(PQExpBuffer script, const char *name)
{
	appendPQExpBuffer(script, "  %s_data:\n", name);
	appendPQExpBuffer(script, "    external: true\n");
	appendPQExpBuffer(script, "    name: vol_%s\n", name);
}


/*
 * prepare_tmux_compose_config prepares a docker-compose configuration for a
 * docker-compose session with the given nodes specifications.
 */
static void
prepare_tmux_compose_config(TmuxOptions *options, PQExpBuffer script)
{
	/* that's optional, but we still fill it as an header of sorts */
	appendPQExpBuffer(script, "version: \"3.9\"\n");
	appendPQExpBuffer(script, "\n");

	appendPQExpBuffer(script, "services:\n");

	/* first, the monitor */
	(void) tmux_compose_add_monitor(script);

	/* then, loop over the nodes for the services */
	for (int i = 0; i < tmuxNodeArray.count; i++)
	{
		TmuxNode *node = &(tmuxNodeArray.nodes[i]);

		(void) tmux_compose_add_node(
			script,
			node->name,
			"demo",
			"demo",
			"postgresql://autoctl_node@monitor/pg_auto_failover");
	}

	appendPQExpBuffer(script, "\n");
	appendPQExpBuffer(script, "volumes:\n");

	/* then, loop over the nodes for the volumes */
	(void) tmux_compose_add_volume(script, "monitor");

	for (int i = 0; i < tmuxNodeArray.count; i++)
	{
		TmuxNode *node = &(tmuxNodeArray.nodes[i]);

		(void) tmux_compose_add_volume(script, node->name);
	}
}


/*
 * log_program_output logs program output as separate lines and with a prefix.
 */
static void
log_program_output(const char *prefix, Program *program)
{
	if (program->stdOut)
	{
		char *outLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program->stdOut, outLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_info("%s: %s", prefix, outLines[lineNumber]);
		}
	}

	if (program->stdErr)
	{
		char *errLines[BUFSIZE] = { 0 };
		int lineCount = splitLines(program->stdOut, errLines, BUFSIZE);
		int lineNumber = 0;

		for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
		{
			log_error("%s: %s", prefix, errLines[lineNumber]);
		}
	}
}


/*
 * tmux_compose_docker_build calls `docker-compose build`.
 */
static void
tmux_compose_docker_build(TmuxOptions *options)
{
	if (chdir(options->root) != 0)
	{
		log_fatal("Failed to change to directory \"%s\": %m", options->root);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("docker-compose build");

	char dockerCompose[MAXPGPATH] = { 0 };

	if (!search_path_first("docker-compose", dockerCompose, LOG_ERROR))
	{
		log_fatal("Failed to find program docker-compose in PATH");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	char pgversion[5] = { 0 };
	char pgversionArg[15] = { 0 };

	if (env_exists("PGVERSION"))
	{
		if (!get_env_copy("PGVERSION", pgversion, sizeof(pgversion)))
		{
			/* errors have already been logged */
			log_warn("Using PGVERSION=14");
			strlcpy(pgversion, "14", sizeof(pgversion));
		}
	}
	else
	{
		strlcpy(pgversion, "14", sizeof(pgversion));
	}

	/* prepare our --build-arg PGVERSION=XX */
	sformat(pgversionArg, sizeof(pgversionArg), "PGVERSION=%s", pgversion);

	char *args[16];
	int argsIndex = 0;

	args[argsIndex++] = (char *) dockerCompose;
	args[argsIndex++] = "build";
	args[argsIndex++] = "--build-arg";
	args[argsIndex++] = (char *) pgversionArg;
	args[argsIndex++] = "--quiet";
	args[argsIndex] = NULL;

	Program program = { 0 };

	(void) initialize_program(&program, args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	char command[BUFSIZE] = { 0 };

	(void) snprintf_program_command_line(&program, command, BUFSIZE);

	char currentWorkingDirectory[MAXPGPATH] = { 0 };

	if (getcwd(currentWorkingDirectory, MAXPGPATH) == NULL)
	{
		log_error("Failed to get the current working directory: %m");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* make it easy for the users to reproduce errors if any */
	log_info("cd \"%s\"", currentWorkingDirectory);
	log_info("%s", command);

	(void) execute_subprogram(&program);
	free_program(&program);

	int returnCode = program.returnCode;

	if (returnCode != 0)
	{
		log_fatal("Failed to run docker-compose build");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * tmux_compose_create_volume calls `docker volume create` for a given volume
 * that is going to be referenced in the docker-compose file.
 */
static void
tmux_compose_create_volume(const char *docker, const char *nodeName)
{
	char volumeName[BUFSIZE] = { 0 };

	sformat(volumeName, sizeof(volumeName), "vol_%s", nodeName);

	log_info("Create docker volume \"%s\"", volumeName);

	Program program =
		run_program(docker, "volume", "create", volumeName, NULL);

	if (program.returnCode != 0)
	{
		char command[BUFSIZE] = { 0 };

		(void) snprintf_program_command_line(&program, command, BUFSIZE);

		log_error("%s [%d]", command, program.returnCode);
		log_program_output("docker volume create", &program);

		log_fatal("Failed to create docker volume: \"%s\"", volumeName);
		free_program(&program);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * tmux_compose_create_volumes calls `docker volume create` for each volume
 * that is going to be referenced in the docker-compose file.
 */
static void
tmux_compose_create_volumes(TmuxOptions *options)
{
	char docker[MAXPGPATH] = { 0 };

	if (!search_path_first("docker", docker, LOG_ERROR))
	{
		log_fatal("Failed to find program docker in PATH");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) tmux_compose_create_volume(docker, "monitor");

	for (int i = 0; i < tmuxNodeArray.count; i++)
	{
		TmuxNode *node = &(tmuxNodeArray.nodes[i]);

		(void) tmux_compose_create_volume(docker, node->name);
	}
}


/*
 * tmux_compose_rm_volume calls `docker volume rm` for a given volume that has
 * been referenced in the docker-compose file.
 */
static void
tmux_compose_rm_volume(const char *docker, const char *nodeName)
{
	char volumeName[BUFSIZE] = { 0 };

	sformat(volumeName, sizeof(volumeName), "vol_%s", nodeName);

	log_info("Remove docker volume \"%s\"", volumeName);

	Program program =
		run_program(docker, "volume", "rm", volumeName, NULL);

	if (program.returnCode != 0)
	{
		char command[BUFSIZE] = { 0 };

		(void) snprintf_program_command_line(&program, command, BUFSIZE);

		log_error("%s [%d]", command, program.returnCode);
		log_program_output("docker volume rm", &program);

		log_fatal("Failed to remove docker volume: \"%s\"", volumeName);
		free_program(&program);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}


/*
 * tmux_compose_down runs `docker-compose down` and then removes the external
 * docker compose volumes that have been created for this run.
 */
static bool
tmux_compose_down(TmuxOptions *options)
{
	char dockerCompose[MAXPGPATH] = { 0 };

	if (!search_path_first("docker-compose", dockerCompose, LOG_ERROR))
	{
		log_fatal("Failed to find program docker-compose in PATH");
		return false;
	}

	/* first docker-compose down */
	log_info("docker-compose down");

	Program program =
		run_program(dockerCompose, "down",
					"--volumes", "--remove-orphans", NULL);

	if (program.returnCode != 0)
	{
		char command[BUFSIZE] = { 0 };

		(void) snprintf_program_command_line(&program, command, BUFSIZE);

		log_error("%s [%d]", command, program.returnCode);
		log_program_output("docker-compose down", &program);

		log_fatal("Failed to run docker-compose down");
		free_program(&program);

		return false;
	}

	/*
	 * Now remove all the docker volumes
	 */
	char docker[MAXPGPATH] = { 0 };

	if (!search_path_first("docker", docker, LOG_ERROR))
	{
		log_fatal("Failed to find program docker in PATH");
		return false;
	}

	(void) tmux_compose_rm_volume(docker, "monitor");

	for (int i = 0; i < tmuxNodeArray.count; i++)
	{
		TmuxNode *node = &(tmuxNodeArray.nodes[i]);

		(void) tmux_compose_rm_volume(docker, node->name);
	}

	return true;
}


/*
 * keeper_cli_tmux_compose_config generates a docker-compose configuration to
 * run a test case or a demo for pg_auto_failover easily, based on using
 * docker-compose.
 */
void
cli_do_tmux_compose_config(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	(void) tmux_process_options(&options);

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the tmux script */
	(void) prepare_tmux_compose_config(&options, script);

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
 * keeper_cli_tmux_compose_script generates a tmux script to run a test case or
 * a demo for pg_auto_failover easily, based on using docker-compose.
 */
void
cli_do_tmux_compose_script(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();

	(void) tmux_process_options(&options);

	if (script == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the tmux script */
	(void) prepare_tmux_compose_script(&options, script);

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
cli_do_tmux_compose_session(int argc, char **argv)
{
	TmuxOptions options = tmuxOptions;
	PQExpBuffer script = createPQExpBuffer();
	PQExpBuffer config = createPQExpBuffer();

	char scriptPathname[MAXPGPATH] = { 0 };
	char configPathname[MAXPGPATH] = { 0 };

	bool success = true;

	(void) tmux_process_options(&options);

	/*
	 * Prepare the tmux script.
	 */
	if (script == NULL || config == NULL)
	{
		log_error("Failed to allocate memory");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* prepare the tmux script and docker-compose config */
	(void) prepare_tmux_compose_script(&options, script);
	(void) prepare_tmux_compose_config(&options, config);

	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(script) || PQExpBufferBroken(config))
	{
		log_error("Failed to allocate memory");
		destroyPQExpBuffer(script);
		destroyPQExpBuffer(config);

		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/*
	 * Write the config to file.
	 */
	sformat(configPathname, sizeof(configPathname),
			"%s/docker-compose.yml", options.root);

	log_info("Writing docker-compose configuration at \"%s\"", configPathname);

	if (!write_file(config->data, config->len, configPathname))
	{
		log_fatal("Failed to write docker-compose config at \"%s\"",
				  configPathname);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	destroyPQExpBuffer(config);

	/*
	 * Write the script to file.
	 */
	sformat(scriptPathname, sizeof(scriptPathname),
			"%s/script-%d.tmux", options.root, options.firstPort);

	log_info("Writing tmux session script \"%s\"", scriptPathname);

	if (!write_file(script->data, script->len, scriptPathname))
	{
		log_fatal("Failed to write tmux script at \"%s\"", scriptPathname);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	destroyPQExpBuffer(script);

	/*
	 * Before starting a tmux session, we have to:
	 *  1. docker-compose build
	 *  2. create all the volumes
	 */
	(void) tmux_compose_docker_build(&options);
	(void) tmux_compose_create_volumes(&options);

	/*
	 * Start a tmux session from the script.
	 */
	if (!tmux_start_server(scriptPathname, options.binpath))
	{
		success = false;
		log_fatal("Failed to start the tmux session, see above for details");
	}

	/*
	 * Stop our pg_autoctl processes and kill the tmux session.
	 */
	log_info("tmux session ended: kill pg_autoct processes");
	success = success && tmux_compose_down(&options);
	success = success && tmux_kill_session(&options);

	if (!success)
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
}
