/*
 * src/bin/pg_autoctl/pidfile.c
 *   Utilities to manage the pg_autoctl pidfile.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "lock_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "pidfile.h"
#include "state.h"
#include "signals.h"
#include "string_utils.h"

/* pidfile for this process */
char service_pidfile[MAXPGPATH] = { 0 };

static void remove_service_pidfile_atexit(void);

/*
 * create_pidfile writes our pid in a file.
 *
 * When running in a background loop, we need a pidFile to add a command line
 * tool that send signals to the process. The pidfile has a single line
 * containing our PID.
 */
bool
create_pidfile(const char *pidfile, pid_t pid)
{
	PQExpBuffer content = createPQExpBuffer();

	bool success = false;

	log_trace("create_pidfile(%d): \"%s\"", pid, pidfile);

	if (content == NULL)
	{
		log_fatal("Failed to allocate memory to update our PID file");
		return false;
	}

	if (!prepare_pidfile_buffer(content, pid))
	{
		/* errors have already been logged */
		return false;
	}

	success = write_file(content->data, content->len, pidfile);
	destroyPQExpBuffer(content);

	return success;
}


/*
 * prepare_pidfile_buffer prepares a PQExpBuffer content with the information
 * expected to be found in a pidfile.
 */
bool
prepare_pidfile_buffer(PQExpBuffer content, pid_t pid)
{
	char pgdata[MAXPGPATH] = { 0 };

	/* we get PGDATA from the environment */
	if (!get_env_pgdata(pgdata))
	{
		log_fatal("Failed to get PGDATA to create the PID file");
		return false;
	}

	/*
	 * line #
	 *		1	supervisor PID
	 *		2	data directory path
	 *		3	version number (PG_AUTOCTL_VERSION)
	 *		4	shared semaphore id (used to serialize log writes)
	 */
	appendPQExpBuffer(content, "%d\n", pid);
	appendPQExpBuffer(content, "%s\n", pgdata);
	appendPQExpBuffer(content, "%s\n", PG_AUTOCTL_VERSION);
	appendPQExpBuffer(content, "%d\n", log_semaphore.semId);

	return true;
}


/*
 * create_pidfile writes the given serviceName pidfile, using getpid().
 */
bool
create_service_pidfile(const char *pidfile, const char *serviceName)
{
	pid_t pid = getpid();

	/* compute the service pidfile and store it in our global variable */
	(void) get_service_pidfile(pidfile, serviceName, service_pidfile);

	/* register our service pidfile clean-up atexit */
	atexit(remove_service_pidfile_atexit);

	return create_pidfile(service_pidfile, pid);
}


/*
 * get_service_pidfile computes the pidfile names for the given service.
 */
void
get_service_pidfile(const char *pidfile,
					const char *serviceName,
					char *servicePidFilename)
{
	char filename[MAXPGPATH] = { 0 };

	sformat(filename, sizeof(filename), "pg_autoctl_%s.pid", serviceName);
	path_in_same_directory(pidfile, filename, servicePidFilename);
}


/*
 * remove_service_pidfile_atexit is called atexit() to remove the service
 * pidfile.
 */
static void
remove_service_pidfile_atexit()
{
	(void) remove_pidfile(service_pidfile);
}


/*
 * read_pidfile read pg_autoctl pid from a file, and returns true when we could
 * read a PID that belongs to a currently running process.
 */
bool
read_pidfile(const char *pidfile, pid_t *pid)
{
	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[1];
	bool error = false;
	int pidnum = 0;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	splitLines(fileContents, fileLines, 1);
	stringToInt(fileLines[0], &pidnum);

	*pid = pidnum;

	free(fileContents);

	if (!error)
	{
		/* is it a stale file? */
		if (kill(*pid, 0) == 0)
		{
			return true;
		}
		else
		{
			log_debug("Failed to signal pid %d: %m", *pid);
			*pid = 0;

			log_info("Found a stale pidfile at \"%s\"", pidfile);
			log_warn("Removing the stale pid file \"%s\"", pidfile);

			/*
			 * We must return false here, after having determined that the
			 * pidfile belongs to a process that doesn't exist anymore. So we
			 * remove the pidfile and don't take the return value into account
			 * at this point.
			 */
			(void) remove_pidfile(pidfile);

			/* we might have to cleanup a stale SysV semaphore, too */
			(void) semaphore_cleanup(pidfile);

			return false;
		}
	}
	else
	{
		log_debug("Failed to read the PID file \"%s\", removing it", pidfile);
		(void) remove_pidfile(pidfile);

		return false;
	}

	/* no warning, it's cool that the file doesn't exists. */
	return false;
}


/*
 * remove_pidfile removes pg_autoctl pidfile.
 */
bool
remove_pidfile(const char *pidfile)
{
	if (remove(pidfile) != 0)
	{
		log_error("Failed to remove keeper's pid file \"%s\": %m", pidfile);
		return false;
	}
	return true;
}


/*
 * check_pidfile checks that the given PID file still contains the known pid of
 * the service. If the file is owned by another process, just quit immediately.
 */
void
check_pidfile(const char *pidfile, pid_t start_pid)
{
	pid_t checkpid = 0;

	/*
	 * It might happen that the PID file got removed from disk, then
	 * allowing another process to run.
	 *
	 * We should then quit in an emergency if our PID file either doesn't
	 * exist anymore, or has been overwritten with another PID.
	 *
	 */
	if (read_pidfile(pidfile, &checkpid))
	{
		if (checkpid != start_pid)
		{
			log_fatal("Our PID file \"%s\" now contains PID %d, "
					  "instead of expected pid %d. Quitting.",
					  pidfile, checkpid, start_pid);

			exit(EXIT_CODE_QUIT);
		}
	}
	else
	{
		/*
		 * Surrendering seems the less risky option for us now.
		 *
		 * Any other strategy would need to be careful about race conditions
		 * happening when several processes (keeper or others) are trying to
		 * create or remove the pidfile at the same time, possibly in different
		 * orders. Yeah, let's quit.
		 */
		log_fatal("PID file not found at \"%s\", quitting.", pidfile);
		exit(EXIT_CODE_QUIT);
	}
}


/*
 * read_service_pidfile_version_string reads a service pidfile and copies the
 * version string found on line PIDFILE_LINE_VERSION_STRING into the
 * pre-allocated buffer versionString.
 */
bool
read_service_pidfile_version_string(const char *pidfile, char *versionString)
{
	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineCount = 0;
	int lineNumber;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		int pidLine = lineNumber + 1; /* zero-based, one-based */

		/* version string */
		if (pidLine == PIDFILE_LINE_VERSION_STRING)
		{
			strlcpy(versionString, fileLines[lineNumber], BUFSIZE);
			return true;
		}
	}

	return false;
}
