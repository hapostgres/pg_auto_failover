/*
 * src/bin/pg_autoctl/cli_manual.c
 *   pg_autoctl manual — operator-driven FSM operations and low-level controls.
 *
 *   These commands let an operator manually drive individual FSM transitions
 *   or low-level operations that pg_autoctl automation would normally own.
 *   Intended for manual recovery when the automated FSM is stopped or stuck,
 *   or for driving a live upgrade step-by-step.
 *
 *   "manual" is the antonym of the automated behaviour pg_autoctl normally
 *   provides: you are doing by hand what the system would otherwise do for you.
 *
 *   Read-only diagnostics belong in "pg_autoctl inspect".
 *   Internal subprocess entry points belong in "pg_autoctl do" (hidden).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include "commandline.h"
#include "cli_manual.h"
#include "cli_do_root.h"

/*
 * manual service: user-visible service controls only.
 *
 * Intentionally excludes the internal subprocess entry points
 * (pgcontroller / postgres / listener / node-active) which are spawned by the
 * supervisor via fork+exec and are not meant for direct operator use.
 * Those live under the hidden "pg_autoctl do service" group.
 */
static CommandLine *manual_service_subcommands[] = {
	&do_service_getpid_commands,        /* getpid postgres|listener|node-active */
	&do_service_restart_commands,       /* restart postgres|listener|node-active */
	&do_service_postgres_ctl_commands,  /* pgctl on|off */
	NULL
};

static CommandLine manual_service_commands =
	make_command_set("service",
	                 "Inspect or restart pg_autoctl sub-processes",
	                 NULL, NULL, NULL, manual_service_subcommands);

/*
 * manual monitor: mutating monitor operations.
 *
 * "get" and "parse-notification" are read-only and live under
 * "pg_autoctl inspect monitor".  Here we expose only the mutating calls:
 *
 *   register  — manually step through the node registration protocol
 *   active    — manually call the node_active RPC on the monitor
 *   version   — check monitor extension version and ALTER EXTENSION UPDATE if needed
 */
static CommandLine *manual_monitor_subcommands[] = {
	&monitor_register_command,
	&monitor_node_active_command,
	&monitor_version_command,
	NULL
};

static CommandLine manual_monitor_commands =
	make_command_set("monitor",
	                 "Manually drive monitor RPCs (register / active / version)",
	                 NULL, NULL, NULL, manual_monitor_subcommands);

static CommandLine *manual_subcommands[] = {
	&do_fsm_commands,           /* state/list/gv  +  assign/step/nodes set */
	&manual_service_commands,   /* getpid / restart / pgctl on|off */
	&manual_monitor_commands,   /* register / active / version */
	&do_primary_,               /* slot create|drop / adduser monitor|replica / defaults / identify */
	&do_standby_,               /* init / rewind / crash-recovery / promote */
	&do_coordinator_commands,   /* add / activate / remove / update prepare|commit|rollback */
	NULL
};

CommandLine manual_commands =
	make_command_set("manual",
	                 "Manual FSM operations — drive by hand what automation normally does",
	                 "[sub-command]",
	                 "  fsm         Inspect or manually step the keeper FSM\n"
	                 "  service     Restart or inspect individual sub-processes\n"
	                 "  monitor     Manually drive monitor registration protocol\n"
	                 "  primary     Manual primary-side PostgreSQL operations\n"
	                 "  standby     Manual standby-side PostgreSQL operations\n"
	                 "  coordinator Citus coordinator operations\n",
	                 NULL, manual_subcommands);
