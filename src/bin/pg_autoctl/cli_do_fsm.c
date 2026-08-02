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
#include "file_utils.h"
#include "fsm.h"
#include "fsm_mermaid.h"
#include "keeper_config.h"
#include "keeper.h"
#include "parsing.h"
#include "pgctl.h"
#include "state.h"
#include "step_socket.h"
#include "string_utils.h"


static void cli_do_fsm_init(int argc, char **argv);
static void cli_do_fsm_state(int argc, char **argv);
static void cli_do_fsm_list(int argc, char **argv);
static void cli_do_fsm_gv(int argc, char **argv);
static void cli_do_fsm_mermaid_init(int argc, char **argv);
static void cli_do_fsm_mermaid_steady_state(int argc, char **argv);
static void cli_do_fsm_mermaid_failover(int argc, char **argv);
static void cli_do_fsm_mermaid_maintenance(int argc, char **argv);
static void cli_do_fsm_mermaid_removal(int argc, char **argv);
static void cli_do_fsm_assign(int argc, char **argv);
static void cli_do_fsm_step(int argc, char **argv);

static void cli_do_fsm_get_nodes(int argc, char **argv);
static void cli_do_fsm_set_nodes(int argc, char **argv);

CommandLine fsm_init =
	make_command("init",
				 "Initialize the keeper's state on-disk",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_init);

CommandLine fsm_state =
	make_command("state",
				 "Read the keeper's state from disk and display it",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_state);

CommandLine fsm_list =
	make_command("list",
				 "List reachable FSM states from current state",
				 CLI_PGDATA_USAGE,
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_list);

CommandLine fsm_gv =
	make_command("gv",
				 "Output the FSM as a .gv program suitable for graphviz/dot",
				 "", NULL, NULL, cli_do_fsm_gv);

static CommandLine fsm_mermaid_init =
	make_command("init",
				 "Mermaid diagram: how a node comes into existence or rejoins",
				 "", NULL, NULL, cli_do_fsm_mermaid_init);

static CommandLine fsm_mermaid_steady_state =
	make_command("steady-state",
				 "Mermaid diagram: normal operation, no failure",
				 "", NULL, NULL, cli_do_fsm_mermaid_steady_state);

static CommandLine fsm_mermaid_failover =
	make_command("failover",
				 "Mermaid diagram: primary failover/promotion, including "
				 "multi-standby candidate election",
				 "", NULL, NULL, cli_do_fsm_mermaid_failover);

static CommandLine fsm_mermaid_maintenance =
	make_command("maintenance",
				 "Mermaid diagram: planned maintenance",
				 "", NULL, NULL, cli_do_fsm_mermaid_maintenance);

static CommandLine fsm_mermaid_removal =
	make_command("removal",
				 "Mermaid diagram: node removal/drop",
				 "", NULL, NULL, cli_do_fsm_mermaid_removal);

static CommandLine *fsm_mermaid_[] = {
	&fsm_mermaid_init,
	&fsm_mermaid_steady_state,
	&fsm_mermaid_failover,
	&fsm_mermaid_maintenance,
	&fsm_mermaid_removal,
	NULL
};

CommandLine fsm_mermaid =
	make_command_set("mermaid",
					 "Output the FSM as Mermaid stateDiagram-v2 programs, "
					 "split by phase for readability", NULL, NULL,
					 NULL, fsm_mermaid_);

CommandLine fsm_assign =
	make_command("assign",
				 "Assign a new goal state to the keeper",
				 CLI_PGDATA_USAGE "<goal state>",
				 CLI_PGDATA_OPTION,
				 cli_getopt_pgdata,
				 cli_do_fsm_assign);

CommandLine fsm_step =
	make_command("step",
				 "Make a state transition if instructed by the monitor",
				 CLI_PGDATA_USAGE "[report|advance]",
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
	&fsm_mermaid,
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
 * cli_do_fsm_mermaid_{init,steady_state,failover,maintenance,removal} each
 * output one phase of the FSM as a Mermaid stateDiagram-v2 program. See
 * fsm_mermaid.c for why the phases are split this way.
 */
static void
cli_do_fsm_mermaid_init(int argc, char **argv)
{
	print_fsm_mermaid_for_phase(FSM_PHASE_INIT);
}


static void
cli_do_fsm_mermaid_steady_state(int argc, char **argv)
{
	print_fsm_mermaid_for_phase(FSM_PHASE_STEADY_STATE);
}


static void
cli_do_fsm_mermaid_failover(int argc, char **argv)
{
	print_fsm_mermaid_for_phase(FSM_PHASE_FAILOVER);
}


static void
cli_do_fsm_mermaid_maintenance(int argc, char **argv)
{
	print_fsm_mermaid_for_phase(FSM_PHASE_MAINTENANCE);
}


static void
cli_do_fsm_mermaid_removal(int argc, char **argv)
{
	print_fsm_mermaid_for_phase(FSM_PHASE_REMOVAL);
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
 *
 * An optional first positional argument, "report" or "advance", splits
 * that combined behavior into its two halves -- see keeper_fsm_step_report/
 * _advance's own comments (fsm.c) for why: keeper_fsm_step's own
 * report-then-immediately-transition shape happens atomically, in one
 * call, which makes it impossible to observe (or hold a node frozen at)
 * the moment in between, something exercising some MonitorFSM[] gap-closing
 * transitions live requires. Bare "step" (no argument) keeps its original,
 * unchanged combined behavior -- every existing caller (the node-active
 * service's own autopilot loop, service_keeper.c; already-written pgaftest
 * specs) keeps working exactly as before.
 */
static void
cli_do_fsm_step(int argc, char **argv)
{
	Keeper keeper = { 0 };

	bool missingPgdataIsOk = true;
	bool pgIsNotRunningIsOk = true;
	bool monitorDisabledIsOk = true;

	keeper.config = keeperOptions;

	if (argc > 1)
	{
		log_error("USAGE: do fsm step [report|advance]");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	bool reportOnly = argc == 1 && streq(argv[0], "report");
	bool advanceOnly = argc == 1 && streq(argv[0], "advance");

	if (argc == 1 && !reportOnly && !advanceOnly)
	{
		log_error("USAGE: do fsm step [report|advance]");
		commandline_help(stderr);
		exit(EXIT_CODE_BAD_ARGS);
	}

	if (!keeper_config_read_file(&(keeper.config),
								 missingPgdataIsOk,
								 pgIsNotRunningIsOk,
								 monitorDisabledIsOk))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_CONFIG);
	}

	/*
	 * When a node-active service is running suspended (PG_AUTOCTL_SUSPENDED),
	 * it owns the keeper's FSM: it's the one already holding the long-lived
	 * state and Postgres process supervision. Delegate to it over its
	 * control socket rather than stepping the FSM ourselves from a fresh
	 * one-shot process, which would race the running service.
	 */
	char stepSocketPath[MAXPGPATH] = { 0 };

	if (step_socket_path(keeper.config.pathnames.pid,
						 stepSocketPath, sizeof(stepSocketPath)) &&
		file_exists(stepSocketPath))
	{
		char response[BUFSIZE * 2] = { 0 };

		const char *socketCommand =
			reportOnly ? STEP_SOCKET_COMMAND_REPORT :
			advanceOnly ? STEP_SOCKET_COMMAND_ADVANCE :
			STEP_SOCKET_COMMAND_STEP;

		if (!step_socket_send_command(stepSocketPath, socketCommand,
									  response, sizeof(response)))
		{
			log_fatal("Failed to reach the suspended node-active service at "
					  "\"%s\"", stepSocketPath);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		if (strncmp(response, "ERROR", 5) == 0)
		{
			log_fatal("%s", response);
			exit(EXIT_CODE_BAD_STATE);
		}

		if (outputJSON)
		{
			log_warn("This command does not support JSON output at the moment");
		}

		/* response is "OK <oldRole> <newRole>" */
		char oldRole[NAMEDATALEN] = { 0 };
		char newRole[NAMEDATALEN] = { 0 };

		if (sscanf(response, "OK %63s %63s", /* IGNORE-BANNED */
				   oldRole, newRole) != 2)
		{
			log_fatal("Failed to parse the suspended-node service response: \"%s\"",
					  response);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		fformat(stdout, "%s ➜ %s\n", oldRole, newRole);
		return;
	}

	if (keeper.config.monitorDisabled)
	{
		log_fatal("The command `pg_autoctl manual fsm step` is meant to step as "
				  "instructed by the monitor, and the monitor is disabled.");
		log_info("HINT: see `pg_autoctl manual fsm assign` instead");
		exit(EXIT_CODE_BAD_CONFIG);
	}

	if (!keeper_init(&keeper, &keeper.config))
	{
		log_fatal("Failed to initialize keeper, see above for details");
		exit(EXIT_CODE_PGCTL);
	}

	const char *oldRole = NodeStateToString(keeper.state.current_role);

	bool stepOk =
		reportOnly ? keeper_fsm_step_report(&keeper) :
		advanceOnly ? keeper_fsm_step_advance(&keeper) :
		keeper_fsm_step(&keeper);

	if (!stepOk)
	{
		/* errors have already been logged */
		exit(EXIT_CODE_BAD_STATE);
	}

	const char *newRole =
		reportOnly
		? NodeStateToString(keeper.state.assigned_role)
		: NodeStateToString(keeper.state.current_role);

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
