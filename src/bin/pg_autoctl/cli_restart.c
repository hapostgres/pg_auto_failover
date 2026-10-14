/*
 * src/bin/pg_autoctl/cli_restart.c
 *     Implementation of the pg_autoctl restart CLI, which restarts Postgres
 *     on the local node through pg_autoctl's own supervised process,
 *     optionally wrapping the operation with maintenance mode.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <getopt.h>
#include <inttypes.h>
#include <sys/wait.h>

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "keeper_config.h"
#include "keeper.h"
#include "log.h"
#include "monitor.h"

static bool optMaintenance = false;
static bool optAllowFailover = false;
static bool optForce = false;
static char optExecCommand[BUFSIZE] = { 0 };

static int cli_restart_postgres_getopts(int argc, char **argv);
static void cli_restart_postgres(int argc, char **argv);

static bool cli_restart_checkpoint_postgres(Keeper *keeper);
static bool cli_restart_stop_exec_and_start(Keeper *keeper);
static bool cli_restart_run_exec_command(void);

CommandLine restart_postgres_command =
	make_command("postgres",
				 "Restart Postgres on this node, going through pg_autoctl's "
				 "own supervised process",
				 " [ --pgdata ] [ --maintenance [ --allow-failover ] | --force ] "
				 "[ --exec <command> ]",
				 "  --pgdata          path to data directory\n"
				 "  --maintenance     enable/disable maintenance around the restart\n"
				 "  --allow-failover  with --maintenance, allow restarting the primary\n"
				 "  --force           skip state checks and restart unconditionally\n"
				 "  --exec            shell command to run while Postgres is stopped\n",
				 cli_restart_postgres_getopts,
				 cli_restart_postgres);

static CommandLine *restart_subcommands[] = {
	&restart_postgres_command,
	NULL
};

CommandLine restart_commands =
	make_command_set("restart",
					 "Restart a pg_auto_failover service", NULL, NULL,
					 NULL, restart_subcommands);


/*
 * cli_restart_postgres_getopts parses the command line options for the
 * command `pg_autoctl restart postgres`.
 */
static int
cli_restart_postgres_getopts(int argc, char **argv)
{
	KeeperConfig options = { 0 };
	int c, option_index = 0, errors = 0;
	int verboseCount = 0;

	static struct option long_options[] = {
		{ "pgdata", required_argument, NULL, 'D' },
		{ "maintenance", no_argument, NULL, 'M' },
		{ "allow-failover", no_argument, NULL, 'A' },
		{ "force", no_argument, NULL, 'F' },
		{ "exec", required_argument, NULL, 'E' },
		{ "version", no_argument, NULL, 'V' },
		{ "verbose", no_argument, NULL, 'v' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	optind = 0;

	while ((c = getopt_long(argc, argv, "D:MAFE:Vvqh",
							long_options, &option_index)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(options.pgSetup.pgdata, optarg, MAXPGPATH);
				log_trace("--pgdata %s", options.pgSetup.pgdata);
				break;
			}

			case 'M':
			{
				/* { "maintenance", no_argument, NULL, 'M' }, */
				optMaintenance = true;
				log_trace("--maintenance");
				break;
			}

			case 'A':
			{
				/* { "allow-failover", no_argument, NULL, 'A' }, */
				optAllowFailover = true;
				log_trace("--allow-failover");
				break;
			}

			case 'F':
			{
				/* { "force", no_argument, NULL, 'F' }, */
				optForce = true;
				log_trace("--force");
				break;
			}

			case 'E':
			{
				/* { "exec", required_argument, NULL, 'E' }, */
				if (strlcpy(optExecCommand, optarg, sizeof(optExecCommand)) >=
					sizeof(optExecCommand))
				{
					log_fatal("Option --exec is too long: pg_autoctl supports "
							  "commands up to %lu bytes and this one requires "
							  "%lu bytes",
							  (unsigned long) sizeof(optExecCommand) - 1,
							  (unsigned long) strlen(optarg));
					exit(EXIT_CODE_BAD_ARGS);
				}
				log_trace("--exec %s", optExecCommand);
				break;
			}

			case 'V':
			{
				/* keeper_cli_print_version prints version and exits. */
				keeper_cli_print_version(argc, argv);
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

			case 'h':
			{
				commandline_help(stderr);
				exit(EXIT_CODE_QUIT);
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

	if (optForce && (optMaintenance || optAllowFailover))
	{
		log_fatal("Please use either --force, or --maintenance "
				  "(optionally with --allow-failover), not both: "
				  "--force skips maintenance mode entirely");
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (optAllowFailover && !optMaintenance)
	{
		log_fatal("Option --allow-failover requires --maintenance");
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* now that we have the command line parameters, prepare the options */
	(void) prepare_keeper_options(&options);

	/* publish our option parsing in the global variable */
	keeperOptions = options;

	return optind;
}


/*
 * cli_restart_run_exec_command runs the --exec command, if any, through the
 * shell while Postgres is stopped -- the same approach Postgres itself uses
 * to run archive_command: hand the whole string to system(3) rather than
 * splitting it into an argv and execv()'ing it directly, so the command can
 * be a shell script, a Python script, a pipeline, or anything else the
 * shell knows how to run, not just a single directly-executable binary.
 *
 * As with archive_command, the string is trusted: it comes from the
 * operator running this CLI locally, not from any remote or untrusted
 * input, so handing it to the shell as-is is the same trust model, not a
 * new one.
 */
static bool
cli_restart_run_exec_command(void)
{
	if (IS_EMPTY_STRING_BUFFER(optExecCommand))
	{
		return true;
	}

	log_info("Running --exec command while Postgres is stopped: %s",
			 optExecCommand);

	int wstatus = system(optExecCommand);

	if (wstatus == -1)
	{
		log_error("Failed to run --exec command \"%s\": %m", optExecCommand);
		return false;
	}

	if (WIFSIGNALED(wstatus))
	{
		log_error("--exec command \"%s\" was terminated by signal %d",
				  optExecCommand, WTERMSIG(wstatus));
		return false;
	}

	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
	{
		log_error("--exec command \"%s\" exited with code %d",
				  optExecCommand, WEXITSTATUS(wstatus));
		return false;
	}

	log_info("--exec command completed successfully");

	return true;
}


/*
 * cli_restart_checkpoint_postgres issues a couple of CHECKPOINTs against the
 * running Postgres, via the same primary_checkpoint() some FSM transitions
 * already call before a planned shutdown (fsm_checkpoint_and_stop_postgres()
 * in fsm_transition.c, used for e.g. entering maintenance on a primary), so
 * that the final shutdown checkpoint Postgres itself issues once stopped
 * has as little work left to do as possible.
 *
 * Deliberately runs before maintenance mode is touched at all: a no-op if
 * Postgres isn't running, otherwise this is the first thing the command
 * does, ahead of any maintenance-state check or transition.
 */
static bool
cli_restart_checkpoint_postgres(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	PostgresSetup *pgSetup = &(postgres->postgresSetup);

	if (!pg_setup_is_running(pgSetup))
	{
		return true;
	}

	log_info("Preparing Postgres for restart: CHECKPOINT;");

	for (int i = 0; i < 2; i++)
	{
		if (!primary_checkpoint(postgres))
		{
			log_warn("Failed to checkpoint before restarting Postgres");
		}
	}

	return true;
}


/*
 * cli_restart_stop_exec_and_start stops Postgres, runs the --exec command if
 * any while it's down, and starts it back up through pg_autoctl's own
 * supervised process.
 *
 * When --exec fails, Postgres is deliberately left stopped rather than
 * started back up on top of a possibly-incomplete offline operation
 * (interrupted pg_checksums --enable, say): the caller must not clear
 * maintenance mode either, so the node stays exactly where the operator
 * can inspect and finish the job by hand.
 */
static bool
cli_restart_stop_exec_and_start(Keeper *keeper)
{
	LocalPostgresServer *postgres = &(keeper->postgres);
	bool updateRetries = false;

	if (!ensure_postgres_service_is_stopped(postgres))
	{
		log_error("Failed to stop Postgres, see above for details");
		return false;
	}

	if (!cli_restart_run_exec_command())
	{
		log_error("--exec command failed: leaving Postgres stopped, "
				  "see above for details");
		return false;
	}

	if (!keeper_ensure_postgres_is_running(keeper, updateRetries))
	{
		log_error("Failed to restart Postgres, see above for details");
		return false;
	}

	log_info("Postgres has been restarted at \"%s\"",
			 postgres->postgresSetup.pgdata);

	return true;
}


/*
 * cli_restart_postgres implements `pg_autoctl restart postgres`: CHECKPOINT
 * twice, enable maintenance (unless already in it, or --force), stop
 * Postgres, run --exec if any, start Postgres back up, then disable
 * maintenance again when --maintenance owns it.
 */
static void
cli_restart_postgres(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = false;

	keeper.config = keeperOptions;

	(void) exit_unless_role_is_keeper(&(keeper.config));

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_KEEPER);
	}

	if (!monitor_init(&(keeper.monitor), keeper.config.monitor_pguri))
	{
		log_fatal("Failed to initialize the monitor connection, "
				  "see above for details.");
		exit(EXIT_CODE_MONITOR);
	}

	(void) cli_restart_checkpoint_postgres(&keeper);

	if (optForce)
	{
		log_warn("--force given: skipping maintenance-state and "
				 "primary/standby checks");
	}
	else
	{
		bool alreadyInMaintenance =
			keeper.state.current_role == MAINTENANCE_STATE;

		if (!alreadyInMaintenance)
		{
			if (!optMaintenance)
			{
				log_fatal("This node is not in maintenance. Use "
						  "`pg_autoctl enable maintenance` first, pass "
						  "--maintenance to have this command manage it "
						  "automatically, or --force to skip this check "
						  "entirely.");
				exit(EXIT_CODE_BAD_STATE);
			}

			/* exits the process directly on failure */
			(void) keeper_cli_enable_maintenance(&keeper, optAllowFailover);
		}
	}

	if (!cli_restart_stop_exec_and_start(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	if (optMaintenance)
	{
		/*
		 * --maintenance owns both ends of the operation: whether we just
		 * entered maintenance above or the node was already in it before
		 * this command ran, disable it now that the restart is done.
		 * Exits the process directly on failure.
		 */
		(void) keeper_cli_disable_maintenance(&keeper);

		log_info("Node has rejoined after the restart, maintenance "
				 "mode is now disabled");
	}
	else if (!optForce)
	{
		log_info("This node remains in maintenance mode; run "
				 "`pg_autoctl disable maintenance` when ready.");
	}
}
