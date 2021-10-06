/*
 * src/bin/pg_autoctl/wal-c.h
 *     Implementation of a wrapper around the WAL-G commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <stdbool.h>

#include "postgres_fe.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"

#include "wal-g.h"

#include "runprogram.h"


/*
 * walg_wal_push calls the command "wal-g wal-push" to archive given WAL file.
 */
bool
walg_wal_push(const char *config, const char *wal)
{
	char walg[MAXPGPATH] = { 0 };

	if (!search_path_first("wal-g", walg, LOG_ERROR))
	{
		log_fatal("Failed to find program wal-g in PATH");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	Program program =
		run_program(walg, "wal-push", "--config", config, wal, NULL);

	/* log the exact command line we're using */
	char command[BUFSIZE] = { 0 };
	(void) snprintf_program_command_line(&program, command, BUFSIZE);

	log_info("%s", command);

	if (program.returnCode != 0)
	{
		log_fatal("Failed to archive WAL \"%s\" with wal-g", wal);
		free_program(&program);

		return false;
	}

	free_program(&program);

	return true;
}
