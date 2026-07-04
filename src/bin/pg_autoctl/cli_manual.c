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
 *   Read-only diagnostics belong in "pg_autoctl inspect":
 *     inspect fsm     state / list / gv
 *     inspect monitor get / parse-notification
 *     inspect getpid  postgres / listener / node-active
 *
 *   Internal subprocess entry points belong in the hidden "pg_autoctl do".
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include "commandline.h"
#include "cli_manual.h"
#include "cli_do_root.h"

/*
 * manual fsm: mutating FSM operations only.
 *
 * Read-only FSM commands (state, list, gv) live under "pg_autoctl inspect fsm"
 * because they are safe to run on a live node.
 *
 * nodes (get + set) is kept as a unit under manual because both sub-commands
 * target the offline nodes file (--disable-monitor), making them equally
 * operator-oriented regardless of read/write direction.
 */
static CommandLine *manual_fsm_subcommands[] = {
	&fsm_init,
	&fsm_assign,
	&fsm_step,
	&fsm_nodes,   /* nodes get + nodes set (--disable-monitor file) */
	NULL
};

static CommandLine manual_fsm_commands =
	make_command_set("fsm",
					 "Manually drive the keeper FSM (mutating operations)",
					 NULL, NULL, NULL, manual_fsm_subcommands);

/*
 * manual service: user-visible service controls only.
 *
 * getpid is read-only and lives under "pg_autoctl inspect getpid".
 *
 * Intentionally excludes the internal subprocess entry points
 * (pgcontroller / postgres / listener / node-active) which are spawned by the
 * supervisor via fork+exec and are not meant for direct operator use.
 * Those live under the hidden "pg_autoctl internal service" group.
 */
static CommandLine *manual_service_subcommands[] = {
	&do_service_restart_commands,       /* restart postgres|listener|node-active */
	&do_service_postgres_ctl_commands,  /* pgctl on|off */
	NULL
};

static CommandLine manual_service_commands =
	make_command_set("service",
					 "Restart pg_autoctl sub-processes or signal the postgres controller",
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

/*
 * manual coordinator: Citus coordinator metadata management.
 *
 * These commands let an operator manually replay the coordinator update steps
 * that pg_autoctl normally drives automatically during a Citus worker failover:
 *
 *   coordinator update prepare   — call master_update_node() in a prepared
 *                                  transaction, which blocks shard writes while open
 *   coordinator update commit    — COMMIT PREPARED: makes the new address permanent
 *   coordinator update rollback  — ROLLBACK PREPARED: abandons a stuck prepare
 *
 * They are typically needed when the coordinator was unavailable during a
 * failover and the prepared transaction needs to be resolved out-of-band.
 */
static CommandLine *manual_subcommands[] = {
	&manual_fsm_commands,           /* init / assign / step / nodes */
	&manual_service_commands,       /* restart / pgctl on|off */
	&manual_monitor_commands,       /* register / active / version */
	&do_primary_,                   /* slot create|drop / adduser monitor|replica / defaults / identify */
	&do_standby_,                   /* init / rewind / crash-recovery / promote */
	&do_coordinator_commands,       /* add / activate / remove / update prepare|commit|rollback */
	NULL
};

CommandLine manual_commands =
	make_command_set("manual",
					 "Manual FSM operations — drive by hand what automation normally does",
					 "[sub-command]",
					 "  fsm         Manually drive keeper FSM transitions\n"
					 "  service     Restart sub-processes or signal the postgres controller\n"
					 "  monitor     Manually drive monitor registration protocol\n"
					 "  primary     Manual primary-side PostgreSQL operations\n"
					 "  standby     Manual standby-side PostgreSQL operations\n"
					 "  coordinator Citus coordinator metadata management\n",
					 NULL, manual_subcommands);
