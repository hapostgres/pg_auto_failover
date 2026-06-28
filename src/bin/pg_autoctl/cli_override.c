/*
 * src/bin/pg_autoctl/cli_override.c
 *   pg_autoctl override — manual FSM operations and low-level controls.
 *
 *   These commands force individual FSM transitions or low-level operations
 *   that the FSM would normally own.  Intended for manual recovery when the
 *   automated FSM is stopped or stuck.  Named "override" to make clear that
 *   these bypass normal automation.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include "commandline.h"
#include "cli_override.h"
#include "cli_do_root.h"

static CommandLine *override_subcommands[] = {
	&do_fsm_commands,                  /* state/list/gv/assign/step/nodes */
	&do_service_postgres_ctl_commands, /* restart postgres|listener|node-active */
	&do_service_commands,              /* pgcontroller / postgres / listener / node-active */
	&do_primary_,                      /* slot / adduser / defaults / identify */
	&do_standby_,                      /* init / rewind / crash-recovery / promote */
	&do_coordinator_commands,          /* add / activate / remove / update */
	NULL
};

CommandLine override_commands =
	make_command_set("override",
	                 "Manual FSM operations — bypasses normal automation",
	                 "[sub-command]",
	                 "  fsm         Inspect / manually step the keeper FSM\n"
	                 "  service     Restart or run individual sub-processes\n"
	                 "  primary     Manual primary-side operations\n"
	                 "  standby     Manual standby-side operations\n"
	                 "  coordinator Citus coordinator operations\n",
	                 NULL, override_subcommands);
