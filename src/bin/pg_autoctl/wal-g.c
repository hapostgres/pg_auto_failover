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
#include "string_utils.h"

#include "wal-g.h"

#include "runprogram.h"

static void log_walg_errors(Program *program);
static void log_walg_error_lines(int logLevel, char *buffer);


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
		(void) log_walg_errors(&program);
		free_program(&program);

		log_fatal("Failed to archive WAL \"%s\" with wal-g, "
				  "see above for details", wal);

		return false;
	}

	if (program.stdOut != NULL)
	{
		log_walg_error_lines(LOG_DEBUG, program.stdOut);
	}

	free_program(&program);

	return true;
}


/*
 * log_walg_errors logs the output of the given program.
 */
static void
log_walg_errors(Program *program)
{
	if (program->stdOut != NULL)
	{
		log_walg_error_lines(LOG_ERROR, program->stdOut);
	}

	if (program->stdErr != NULL)
	{
		log_walg_error_lines(LOG_ERROR, program->stdErr);
	}
}


/*
 * log_walg_error_lines logs given program output buffer as separate lines.
 */
static void
log_walg_error_lines(int logLevel, char *buffer)
{
	char *lines[BUFSIZE];
	int lineCount = splitLines(buffer, lines, BUFSIZE);
	int lineNumber = 0;

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		log_level(logLevel, "wal-g: %s", lines[lineNumber]);
	}
}
