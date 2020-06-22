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
#include "string_utils.h"


static void cli_do_fsm_init(int argc, char **argv);
static void cli_do_fsm_state(int argc, char **argv);
static void cli_do_fsm_list(int argc, char **argv);
static void cli_do_fsm_gv(int argc, char **argv);
static void cli_do_fsm_assign(int argc, char **argv);
static void cli_do_fsm_step(int argc, char **argv);

static CommandLine fsm_init =
	make_command("init",
				 "Initialize the keeper's state on-disk",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_init);

static CommandLine fsm_state =
	make_command("state",
				 "Read the keeper's state from disk and display it",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_state);

static CommandLine fsm_list =
	make_command("list",
				 "List reachable FSM states from current state",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_list);

static CommandLine fsm_gv =
	make_command("gv",
				 "Output the FSM as a .gv program suitable for graphviz/dot",
				 "", NULL, NULL, cli_do_fsm_gv);

static CommandLine fsm_assign =
	make_command("assign",
				 "Assign a new goal state to the keeper",
				 CLI_PGDATA_USAGE "<goal state> [ <host> <port> ]",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_assign);

static CommandLine fsm_step =
	make_command("step",
				 "Make a state transition if instructed by the monitor",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_step);

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
 * cli_do_fsm_init initializes the internal Keeper state, and writes it to
 * disk.
 */
static void
cli_do_fsm_init(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	char keeperStateJSON[BUFSIZE];

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

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

	if (!keeper_state_as_json(&keeper, keeperStateJSON, BUFSIZE))
	{
		log_error("Failed to serialize internal keeper state to JSON");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	fformat(stdout, "%s\n", keeperStateJSON);
}


/*
 * cli_do_fsm_init initializes the internal Keeper state, and writes it to
 * disk.
 */
static void
cli_do_fsm_state(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;

	char keeperStateJSON[BUFSIZE];

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_state_as_json(&keeper, keeperStateJSON, BUFSIZE))
	{
		log_error("Failed to serialize internal keeper state to JSON");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	fformat(stdout, "%s\n", keeperStateJSON);
}


/*
 * cli_do_fsm_list lists reachable states from the current one.
 */
static void
cli_do_fsm_list(int argc, char **argv)
{
	KeeperStateData keeperState = { 0 };
	KeeperConfig config = keeperOptions;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* now read keeper's state */
	if (!keeper_state_read(&keeperState, config.pathnames.state))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	if (outputJSON)
	{
		log_warn("This command does not support JSON output at the moment");
	}

	print_reachable_states(&keeperState);
	fformat(stdout, "\n");
}


/*
 * cli_do_fsm_gv outputs the FSM as a .gv program.
 */
static void
cli_do_fsm_gv(int argc, char **argv)
{
	print_fsm_for_graphviz();
}


/*
 * cli_do_fsm_assigns a reachable state from the current one.
 */
static void
cli_do_fsm_assign(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig config = keeperOptions;
	char keeperStateJSON[BUFSIZE];
	NodeState goalState = NO_STATE;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	switch (argc)
	{
		case 1:
		{
			goalState = NodeStateFromString(argv[0]);
			break;
		}

		case 4:
		{
			/* we just accept a single standby at a time here */
			NodeAddress *otherNode = &(keeper.otherNodes.nodes[0]);

			keeper.otherNodes.count = 1;

			goalState = NodeStateFromString(argv[1]);

			/* now prepare id, host, and port in keeper.otherNodes */
			if (!stringToInt(argv[2], &otherNode->nodeId))
			{
				log_error("Failed to parse otherNode id \"%s\"", argv[2]);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}

			strlcpy(otherNode->host, argv[3], _POSIX_HOST_NAME_MAX);

			if (!stringToInt(argv[4], &otherNode->port))
			{
				log_error(
					"Failed to parse otherNode port number \"%s\"", argv[4]);
				exit(EXIT_CODE_INTERNAL_ERROR);
			}
			break;
		}

		default:
		{
			log_error("USAGE: do fsm state <goal state> [<id> <host> <port>]");
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
	keeper.state.assigned_role = goalState;

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
	fformat(stdout, "%s\n", keeperStateJSON);
}


/*
 * cli_do_fsm_step gets the goal state from the monitor, makes
 * the necessary transition, and then reports the current state to
 * the monitor.
 */
static void
cli_do_fsm_step(int argc, char **argv)
{
	Keeper keeper = { 0 };
	const char *oldRole = NULL;
	const char *newRole = NULL;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	keeper.config = keeperOptions;

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (keeper.config.monitorDisabled)
	{
		log_fatal("The command `pg_autoctl do fsm step` is meant to step as "
				  "instructed by the monitor, and the monitor is disabled.");
		log_info("HINT: see `pg_autoctl do fsm assign` instead");
		exit(EXIT_CODE_BAD_CONFIG);
	}

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

	if (outputJSON)
	{
		log_warn("This command does not support JSON output at the moment");
	}
	fformat(stdout, "%s ➜ %s\n", oldRole, newRole);
}
