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
#include "log.h"

extern CommandLine pgaftest_root;

int
main(int argc, char **argv)
{
	/* default log level: INFO to stderr */
	log_set_level(LOG_INFO);

	if (argc < 2)
	{
		commandline_print_usage(&pgaftest_root, stderr);
		return 1;
	}

	return commandline_run(&pgaftest_root, argc, argv);
}
