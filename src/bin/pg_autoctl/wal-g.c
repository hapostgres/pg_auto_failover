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

#include "config.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

#include "wal-g.h"

#include "runprogram.h"

static void walg_log_errors(Program *program);
static void walg_log_error_lines(int logLevel, char *buffer);


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
		(void) walg_log_errors(&program);
		free_program(&program);

		log_fatal("Failed to archive WAL \"%s\" with wal-g, "
				  "see above for details", wal);

		return false;
	}

	if (program.stdOut != NULL)
	{
		walg_log_error_lines(LOG_INFO, program.stdOut);
	}

	if (program.stdErr != NULL)
	{
		walg_log_error_lines(LOG_INFO, program.stdErr);
	}

	free_program(&program);

	return true;
}


/*
 * walg_prepare_config prepares the configuration in a configuration file.
 *
 * The WAL-G configuration is maintained on the monitor as part of the
 * pgautofailover.archiver_policy table, in a JSONB column. The wal-g command
 * wants a filename where to read the same contents, so that's what we have to
 * prepare now.
 */
bool
walg_prepare_config(const char *pgdata, const char *config,
					char *archiverConfigPathname)
{
	if (!build_xdg_path(archiverConfigPathname,
						XDG_RUNTIME,
						pgdata,
						WAL_G_CONFIGURATION_FILENAME))
	{
		/* highly unexpected */
		log_error("Failed to build wal-g configuration file pathname, "
				  "see above for details.");
		return false;
	}

	log_debug("walg_prepare_config: %s", archiverConfigPathname);

	if (!write_file((char *) config, strlen(config), archiverConfigPathname))
	{
		log_error("Failed to write WAL-G configuration to file \"%s\"",
				  archiverConfigPathname);
		return false;
	}

	return true;
}


/*
 * log_walg_errors logs the output of the given program.
 */
static void
walg_log_errors(Program *program)
{
	if (program->stdOut != NULL)
	{
		walg_log_error_lines(LOG_ERROR, program->stdOut);
	}

	if (program->stdErr != NULL)
	{
		walg_log_error_lines(LOG_ERROR, program->stdErr);
	}
}


/*
 * log_walg_error_lines logs given program output buffer as separate lines.
 */
static void
walg_log_error_lines(int logLevel, char *buffer)
{
	char *lines[BUFSIZE];
	int lineCount = splitLines(buffer, lines, BUFSIZE);
	int lineNumber = 0;

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		log_level(logLevel, "wal-g: %s", lines[lineNumber]);
	}
}
