/*
 * src/bin/pg_autoctl/cli_inspect.c
 *   pg_autoctl inspect — read-only diagnostics, always visible.
 *
 *   These commands read local or cluster state without mutating anything.
 *   Safe to run at any time, even while `pg_autoctl run` is active.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include "commandline.h"
#include "cli_inspect.h"
#include "cli_do_root.h"

/*
 * Aggregate the read-only do_* command sets under a single "inspect" group.
 * We reference the existing command sets by address — do_show_commands,
 * do_pgsetup_commands, etc. are extern CommandLine objects already declared
 * in cli_do_root.h.
 */

static CommandLine *inspect_subcommands[] = {
	&do_show_commands,        /* ipaddr / cidr / lookup / hostname / reverse */
	&do_pgsetup_commands,     /* discover / ready / wait / logs / tune / pg_ctl */
	&do_monitor_commands,     /* get primary|others / version / register / active */
	&do_service_commands,     /* getpid postgres|listener|node-active */
	NULL
};

CommandLine inspect_commands =
	make_command_set("inspect",
	                 "Read-only diagnostics (safe on live nodes)",
	                 "[sub-command]",
	                 "  show      Networking and hostname diagnostics\n"
	                 "  pgsetup   Local PostgreSQL setup inspection\n"
	                 "  monitor   Query the monitor's current state\n"
	                 "  service   Get PIDs of pg_autoctl sub-processes\n",
	                 NULL, inspect_subcommands);
