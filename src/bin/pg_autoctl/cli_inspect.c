/*
 * src/bin/pg_autoctl/cli_inspect.c
 *   pg_autoctl inspect — read-only diagnostics, always visible.
 *
 *   All commands here read local or cluster state without mutating anything.
 *   Safe to run at any time, even while `pg_autoctl run` is active.
 *
 *   For mutating recovery commands (fsm assign, standby promote, etc.) see
 *   cli_manual.c ("pg_autoctl manual …").
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include "commandline.h"
#include "cli_inspect.h"
#include "cli_do_root.h"

/*
 * Read-only FSM sub-commands: display the current state, list reachable
 * transitions, or dump the full FSM as a graphviz .gv file.
 * Mutating operations (init, assign, step, nodes set) live under "manual fsm".
 */
static CommandLine *inspect_fsm_subcommands[] = {
	&fsm_state,
	&fsm_node_state,
	&fsm_list,
	&fsm_gv,
	NULL
};

static CommandLine inspect_fsm_commands =
	make_command_set("fsm",
					 "Display keeper FSM state and transitions (read-only)",
					 NULL, NULL, NULL, inspect_fsm_subcommands);

/*
 * Read-only monitor sub-commands: get primary/others/candidate-count/coordinator
 * and parse-notification.  We intentionally exclude "register", "active", and
 * "version" (ALTER EXTENSION) which are mutating — those live under "manual".
 */
static CommandLine *inspect_monitor_subcommands[] = {
	&monitor_get_command,
	&monitor_parse_notification_command,
	&monitor_node_state_command,
	&monitor_formation_states_command,
	NULL
};

static CommandLine inspect_monitor_commands =
	make_command_set("monitor",
					 "Query the monitor's current state (read-only)",
					 NULL, NULL, NULL, inspect_monitor_subcommands);

/*
 * Aggregate the read-only do_* command sets under "inspect".
 *
 * service here is only "getpid" — inspecting PIDs of running sub-processes.
 * "restart" and "pgctl on/off" mutate state and belong under "manual service".
 */
static CommandLine *inspect_subcommands[] = {
	&do_show_commands,           /* ipaddr / cidr / lookup / hostname / reverse */
	&do_pgsetup_commands,        /* discover / ready / wait / logs / tune / pg_ctl */
	&inspect_fsm_commands,       /* state / list / gv */
	&inspect_monitor_commands,   /* get primary|others|candidate-count + parse-notification */
	&do_service_getpid_commands, /* getpid postgres|listener|node-active */
	NULL
};

CommandLine inspect_commands =
	make_command_set("inspect",
					 "Read-only diagnostics (safe on live nodes)",
					 "[sub-command]",
					 "  show      Networking and hostname diagnostics\n"
					 "  pgsetup   Local PostgreSQL setup inspection\n"
					 "  fsm       Display keeper FSM state and reachable transitions\n"
					 "  monitor   Query the monitor's current state\n"
					 "  getpid    Get PIDs of pg_autoctl sub-processes\n",
					 NULL, inspect_subcommands);
