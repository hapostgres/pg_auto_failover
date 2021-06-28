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
#include <time.h>

#include "postgres_fe.h"

#include "cli_common.h"
#include "commandline.h"
#include "defaults.h"
#include "fsm.h"
#include "keeper_config.h"
#include "keeper.h"
#include "parsing.h"
#include "pgctl.h"
#include "state.h"
#include "string_utils.h"


static void cli_do_fsm_init(int argc, char **argv);
static void cli_do_fsm_state(int argc, char **argv);
static void cli_do_fsm_list(int argc, char **argv);
static void cli_do_fsm_gv(int argc, char **argv);
static void cli_do_fsm_assign(int argc, char **argv);
static void cli_do_fsm_step(int argc, char **argv);

static void cli_do_fsm_get_nodes(int argc, char **argv);
static void cli_do_fsm_set_nodes(int argc, char **argv);

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
				 CLI_PGDATA_USAGE "<goal state>",
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

static CommandLine fsm_nodes_get =
	make_command("get",
				 "Get the list of nodes from file (see --disable-monitor)",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_get_nodes);

static CommandLine fsm_nodes_set =
	make_command("set",
				 "Set the list of nodes to file (see --disable-monitor)",
				 CLI_PGDATA_USAGE "</path/to/input/nodes.json>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_set_nodes);


static CommandLine *fsm_nodes_[] = {
	&fsm_nodes_get,
	&fsm_nodes_set,
	NULL
};

CommandLine fsm_nodes =
	make_command_set("nodes",
					 "Manually manage the keeper's nodes list", NULL, NULL,
					 NULL, fsm_nodes_);

static CommandLine *fsm[] = {
	&fsm_init,
	&fsm_state,
	&fsm_list,
	&fsm_gv,
	&fsm_assign,
	&fsm_step,
	&fsm_nodes,
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

	if (!keeper_update_pg_state(&keeper, LOG_ERROR))
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

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	int timeout = 30;
	int attempts = 0;
	uint64_t startTime = time(NULL);

	if (!keeper_config_read_file(&config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (argc != 1)
	{
		log_error("USAGE: do fsm state <goal state>");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	NodeState goalState = NodeStateFromString(argv[0]);

	if (goalState == NO_STATE)
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* now read keeper's state */
	if (!keeper_init(&keeper, &config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* assign the new state */
	keeper.state.assigned_role = goalState;

	if (!keeper_store_state(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	/* loop over reading the state until assigned state has been reached */
	for (attempts = 0; keeper.state.current_role != goalState; attempts++)
	{
		uint64_t now = time(NULL);

		if (!keeper_load_state(&keeper))
		{
			/* errors have already been logged */
			exit(EXIT_CODE_BAD_STATE);
		}

		/* we're done if we reach the timeout */
		if ((now - startTime) >= timeout)
		{
			break;
		}

		/* sleep 100 ms in between state file probes */
		pg_usleep(100 * 1000);
	}

	if (keeper.state.current_role != goalState)
	{
		uint64_t now = time(NULL);

		log_warn("Failed to reach goal state \"%s\" in %d attempts and %ds",
				 NodeStateToString(goalState),
				 attempts,
				 (int) (now - startTime));
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
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	const char *oldRole = NodeStateToString(keeper.state.current_role);

	if (!keeper_fsm_step(&keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	const char *newRole = NodeStateToString(keeper.state.assigned_role);

	if (outputJSON)
	{
		log_warn("This command does not support JSON output at the moment");
	}
	fformat(stdout, "%s ➜ %s\n", oldRole, newRole);
}


/*
 * cli_do_fsm_get_nodes displays the list of nodes parsed from the nodes file
 * on-disk. A nodes file is only used when running with --disable-monitor.
 */
static void
cli_do_fsm_get_nodes(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	*config = keeperOptions;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!config->monitorDisabled)
	{
		log_fatal("The monitor is not disabled, there's no nodes file");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_read_nodes_from_file(&keeper, &(keeper.otherNodes)))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) printNodeArray(&(keeper.otherNodes));
}


/*
 * cli_do_fsm_set_nodes parses the list of nodes parsed from the nodes file
 * on-disk. A JSON array of nodes objects is expected. A nodes file is only
 * used when running with --disable-monitor.
 */
static void
cli_do_fsm_set_nodes(int argc, char **argv)
{
	Keeper keeper = { 0 };
	KeeperConfig *config = &(keeper.config);

	char nodesArrayInputFile[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	*config = keeperOptions;

	if (!keeper_config_read_file(config,
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!config->monitorDisabled)
	{
		log_fatal("The monitor is not disabled, there's no nodes file");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (argc != 1)
	{
		commandline_print_usage(&fsm_nodes_set, stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	strlcpy(nodesArrayInputFile, argv[0], sizeof(nodesArrayInputFile));

	if (!read_file_if_exists(nodesArrayInputFile, &contents, &size))
	{
		log_error("Failed to read nodes array from file \"%s\"",
				  nodesArrayInputFile);
		exit(EXIT_CODE_BAD_ARGS);
	}

	/* now read keeper's state */
	if (!keeper_init(&keeper, config))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/* now parse the nodes JSON file */
	if (!parseNodesArray(contents,
						 &(keeper.otherNodes),
						 keeper.state.current_node_id))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* parsing is successful, so let's copy that file to the expected path */
	if (!write_file(contents, size, config->pathnames.nodes))
	{
		log_error("Failed to write input nodes file \"%s\" to \"%s\"",
				  nodesArrayInputFile,
				  config->pathnames.nodes);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	(void) printNodeArray(&(keeper.otherNodes));
}
