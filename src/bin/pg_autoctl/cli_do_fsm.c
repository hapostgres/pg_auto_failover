/*
 * src/bin/pg_autoctl/cli_fsm.c
 *     Implementation of a CLI which lets you run individual keeper Finite
 *     State Machine routines directly
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <getopt.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "pgctl.h"
#include "state.h"


static void keeper_cli_fsm_init(int argc, char **argv);
static void keeper_cli_fsm_state(int argc, char **argv);
static void keeper_cli_fsm_list(int argc, char **argv);
static void keeper_cli_fsm_gv(int argc, char **argv);
static void keeper_cli_fsm_assign(int argc, char **argv);
static void keeper_cli_fsm_step(int argc, char **argv);

static CommandLine fsm_init =
	make_command("init",
				 "Initialize the keeper's state on-disk",
				 " [ --pgdata ] ",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_fsm_init);

static CommandLine fsm_state =
	make_command("state",
				 "Read the keeper's state from disk and display it",
				 " [ --pgdata ] ",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_fsm_state);

static CommandLine fsm_list =
	make_command("list",
				 "List reachable FSM states from current state",
				 " [ --pgdata ] ",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_fsm_list);

static CommandLine fsm_gv =
	make_command("gv",
				 "Output the FSM as a .gv program suitable for graphviz/dot",
				 "", NULL, NULL, keeper_cli_fsm_gv);

static CommandLine fsm_assign =
	make_command("assign",
				 "Assign a new goal state to the keeper",
				 " [ --pgdata ] <goal state> [<host> <port>]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_fsm_assign);

static CommandLine fsm_step =
	make_command("step",
				 "Make a state transition if instructed by the monitor",
				 " [ --pgdata ]",
				 KEEPER_CLI_PGDATA_OPTION,
				 keeper_cli_getopt_pgdata,
				 keeper_cli_fsm_step);

static CommandLine *fsm[] = {
	&fsm_init,
	&fsm_state,
	&fsm_list,
	&fsm_gv,
	&fsm_assign,
	&fsm_step,
	NULL
};

CommandLine do_fsm_commands =
	make_command_set("fsm",
					 "Manually manage the keeper's state", NULL, NULL,
					 NULL, fsm);


/*
 * keeper_cli_fsm_init initializes the internal Keeper state, and writes it to
 * disk.
 */
static void
keeper_cli_fsm_init(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperStateData keeperState = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	keeper_config_read_file(&config,
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	log_info("Initializing an FSM state in \"%s\"", config.pathnames.state);

	if (!keeper_state_create_file(config.pathnames.state))
	{
		/* errors are logged in keeper_state_write */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_init(&keeper, &config))
	{
		/* errors are logged in keeper_state_read */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_update_pg_state(&keeper))
	{
		log_fatal("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above.");
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_store_state(&keeper))
	{
		/* errors logged in keeper_state_write */
		exit(EXIT_CODE_BAD_STATE);
	}

	print_keeper_state(&keeperState, stdout);
}


/*
 * keeper_cli_fsm_init initializes the internal Keeper state, and writes it to
 * disk.
 */
static void
keeper_cli_fsm_state(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	char keeperStateJSON[BUFSIZE];

	keeper_config_read_file(&config,
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* check that the state matches with running PostgreSQL instance */
	if (!keeper_update_pg_state(&keeper))
	{
		log_fatal("Failed to update the keeper's state from the local "
				  "PostgreSQL instance, see above.");
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_store_state(&keeper))
	{
		/* errors logged in keeper_state_write */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_state_as_json(&keeper, keeperStateJSON, BUFSIZE))
	{
		log_error("Failed to serialize internal keeper state to JSON");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	fprintf(stdout, "%s\n", keeperStateJSON);	
}


/*
 * keeper_cli_fsm_list lists reachable states from the current one.
 */
static void
keeper_cli_fsm_list(int argc, char **argv)
{
	KeeperStateData keeperState = { 0 };

	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;

	keeper_config_read_file(&config,
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	/* now read keeper's state */
	if (!keeper_state_read(&keeperState, config.pathnames.state))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	print_reachable_states(&keeperState);
	fprintf(stdout, "\n");
}


/*
 * keeper_cli_fsm_gv outputs the FSM as a .gv program.
 */
static void
keeper_cli_fsm_gv(int argc, char **argv)
{
	print_fsm_for_graphviz();
}


/*
 * keeper_cli_fsm_assigns a reachable state from the current one.
 */
static void
keeper_cli_fsm_assign(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	char keeperStateJSON[BUFSIZE];

	keeper_config_read_file(&config,
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	switch (argc)
	{
		case 1:
		{
			/* all good, nothing special to do here. */
			break;
		}

		case 3:
		{
			strlcpy(keeper.otherNode.host, argv[1], _POSIX_HOST_NAME_MAX);

			keeper.otherNode.port = strtol(argv[2], NULL, 10);
			if (keeper.otherNode.port == 0 && errno == EINVAL)
			{
				log_error(
					"Failed to parse otherNode port number \"%s\"", argv[2]);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			break;
		}

		default:
		{
			log_error("USAGE: do fsm state <goal state> [<host> <port>]");
			commandline_help(stderr);
			exit(EXIT_CODE_BAD_ARGS);
		}
	}

	/* now read keeper's state */
	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* assign the new state */
	keeper.state.assigned_role = NodeStateFromString(argv[0]);

	/* roll the state machine */
	if (!keeper_fsm_reach_assigned_state(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_store_state(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (!keeper_state_as_json(&keeper, keeperStateJSON, BUFSIZE))
	{
		log_error("Failed to serialize internal keeper state to JSON");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	fprintf(stdout, "%s\n", keeperStateJSON);
}


/*
 * keeper_cli_fsm_step gets the goal state from the monitor, makes
 * the necessary transition, and then reports the current state to
 * the monitor.
 */
static void
keeper_cli_fsm_step(int argc, char **argv)
{
	Keeper keeper = { 0 };
	bool missing_pgdata_is_ok = true;
	bool pg_is_not_running_is_ok = true;
	const char *oldRole = NULL;
	const char *newRole = NULL;

	keeper.config = keeperOptions;

	keeper_config_read_file(&(keeper.config),
							missing_pgdata_is_ok,
							pg_is_not_running_is_ok);

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialise keeper, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	oldRole = NodeStateToString(keeper.state.current_role);

	if (!keeper_fsm_step(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	newRole = NodeStateToString(keeper.state.assigned_role);

	fprintf(stdout, "%s ➜ %s\n", oldRole, newRole);
}
