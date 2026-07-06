/*
 * src/bin/pgaftest/main.c
 *   pgaftest — pg_auto_failover test runner.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commandline.h"
#include "defaults.h"
#include "log.h"
#include "lock_utils.h"
#include "monitor_config.h"

extern CommandLine pgaftest_root;

/*
 * Globals required by shared pg_autoctl source files.
 * In pg_autoctl these live in cli_root.c / cli_create_node.c / main.c.
 * pgaftest owns stub definitions here so the linker is satisfied.
 */
int pgconnect_timeout = 2;
char *ps_buffer;
size_t ps_buffer_size;
size_t last_status_len;
Semaphore log_semaphore = { 0 };

char pg_autoctl_argv0[MAXPGPATH] = "pgaftest";
char pg_autoctl_program[MAXPGPATH] = "pgaftest";

MonitorConfig monitorOptions = { 0 };

/* Stub command roots referenced by cli_common.c's keeper_cli_help */
CommandLine root = make_command("pgaftest", "", "", "", NULL, NULL);
CommandLine root_with_debug = make_command("pgaftest", "", "", "", NULL, NULL);

int
main(int argc, char **argv)
{
	/* store binary path so runner_setup can reference it in tmux panes */
	strlcpy(pg_autoctl_program, argv[0], sizeof(pg_autoctl_program));

	/* default log level: INFO to stderr */
	log_set_level(LOG_INFO);

	if (argc < 2)
	{
		commandline_print_usage(&pgaftest_root, stderr);
		return 1;
	}

	return commandline_run(&pgaftest_root, argc, argv);
}
