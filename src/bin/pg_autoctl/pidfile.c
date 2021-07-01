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

#include "cli_common.h"
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


	log_trace("create_pidfile(%d): \"%s\"", pid, pidfile);

	if (content == NULL)
	{
		log_fatal("Failed to allocate memory to update our PID file");
		return false;
	}

	if (!prepare_pidfile_buffer(content, pid))
	{
		/* errors have already been logged */
		destroyPQExpBuffer(content);
		return false;
	}


	/* memory allocation could have failed while building string */
	if (PQExpBufferBroken(content))
	{
		log_error("Failed to create pidfile \"%s\": out of memory", pidfile);
		destroyPQExpBuffer(content);
		return false;
	}

	bool success = write_file(content->data, content->len, pidfile);
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
	 *		4	extension version number (PG_AUTOCTL_EXTENSION_VERSION)
	 *		5	shared semaphore id (used to serialize log writes)
	 */
	appendPQExpBuffer(content, "%d\n", pid);
	appendPQExpBuffer(content, "%s\n", pgdata);
	appendPQExpBuffer(content, "%s\n", PG_AUTOCTL_VERSION);
	appendPQExpBuffer(content, "%s\n", PG_AUTOCTL_EXTENSION_VERSION);
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
	int pidnum = 0;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		log_debug("Failed to read the PID file \"%s\", removing it", pidfile);
		(void) remove_pidfile(pidfile);

		return false;
	}

	splitLines(fileContents, fileLines, 1);
	stringToInt(fileLines[0], &pidnum);

	*pid = pidnum;

	free(fileContents);

	if (pid <= 0)
	{
		log_debug("Read negative pid %d in file \"%s\", removing it",
				  *pid, pidfile);
		(void) remove_pidfile(pidfile);

		return false;
	}

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


/*
 * remove_pidfile removes pg_autoctl pidfile.
 */
bool
remove_pidfile(const char *pidfile)
{
	if (remove(pidfile) != 0)
	{
		log_error("Failed to remove pid file \"%s\": %m", pidfile);
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
read_service_pidfile_version_strings(const char *pidfile,
									 char *versionString,
									 char *extensionVersionString)
{
	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineNumber;

	if (!read_file_if_exists(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	int lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		int pidLine = lineNumber + 1; /* zero-based, one-based */

		/* version string */
		if (pidLine == PIDFILE_LINE_VERSION_STRING)
		{
			strlcpy(versionString, fileLines[lineNumber], BUFSIZE);
		}

		/* extension version string, comes later in the file */
		if (pidLine == PIDFILE_LINE_EXTENSION_VERSION)
		{
			strlcpy(extensionVersionString, fileLines[lineNumber], BUFSIZE);
			free(fileContents);
			return true;
		}
	}

	free(fileContents);

	return false;
}


/*
 * fprint_pidfile_as_json prints the content of the pidfile as JSON.
 *
 * When includeStatus is true, add a "status" entry for each PID (main service
 * and sub-processes) with either "running" or "stale" as a value, depending on
 * what a kill -0 reports.
 */
void
pidfile_as_json(JSON_Value *js, const char *pidfile, bool includeStatus)
{
	JSON_Value *jsServices = json_value_init_array();
	JSON_Array *jsServicesArray = json_value_get_array(jsServices);

	JSON_Object *jsobj = json_value_get_object(js);

	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineNumber;

	if (!read_file_if_exists(pidfile, &fileContents, &fileSize))
	{
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	int lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		int pidLine = lineNumber + 1; /* zero-based, one-based */
		char *separator = NULL;

		/* main pid */
		if (pidLine == PIDFILE_LINE_PID)
		{
			int pidnum = 0;
			stringToInt(fileLines[lineNumber], &pidnum);
			json_object_set_number(jsobj, "pid", (double) pidnum);

			if (includeStatus)
			{
				if (kill(pidnum, 0) == 0)
				{
					json_object_set_string(jsobj, "status", "running");
				}
				else
				{
					json_object_set_string(jsobj, "status", "stale");
				}
			}
			continue;
		}

		/* data directory */
		if (pidLine == PIDFILE_LINE_DATA_DIR)
		{
			json_object_set_string(jsobj, "pgdata", fileLines[lineNumber]);
		}

		/* version string */
		if (pidLine == PIDFILE_LINE_VERSION_STRING)
		{
			json_object_set_string(jsobj, "version", fileLines[lineNumber]);
		}

		/* extension version string */
		if (pidLine == PIDFILE_LINE_EXTENSION_VERSION)
		{
			/* skip it, the supervisor does not connect to the monitor */
			(void) 0;
		}

		/* semId */
		if (pidLine == PIDFILE_LINE_SEM_ID)
		{
			int semId = 0;

			if (stringToInt(fileLines[lineNumber], &semId))
			{
				json_object_set_number(jsobj, "semId", (double) semId);
			}
			else
			{
				log_error("Failed to parse semId \"%s\"", fileLines[lineNumber]);
			}

			continue;
		}

		if (pidLine >= PIDFILE_LINE_FIRST_SERVICE)
		{
			JSON_Value *jsService = json_value_init_object();
			JSON_Object *jsServiceObj = json_value_get_object(jsService);

			if ((separator = strchr(fileLines[lineNumber], ' ')) == NULL)
			{
				log_debug("Failed to find a space separator in line: \"%s\"",
						  fileLines[lineNumber]);
				continue;
			}
			else
			{
				int pidnum = 0;
				char *serviceName = separator + 1;

				char servicePidFile[BUFSIZE] = { 0 };

				char versionString[BUFSIZE] = { 0 };
				char extensionVersionString[BUFSIZE] = { 0 };

				*separator = '\0';
				stringToInt(fileLines[lineNumber], &pidnum);

				json_object_set_string(jsServiceObj, "name", serviceName);
				json_object_set_number(jsServiceObj, "pid", pidnum);

				if (includeStatus)
				{
					if (kill(pidnum, 0) == 0)
					{
						json_object_set_string(jsServiceObj, "status", "running");
					}
					else
					{
						json_object_set_string(jsServiceObj, "status", "stale");
					}
				}

				/* grab version number of the service by parsing its pidfile */
				get_service_pidfile(pidfile, serviceName, servicePidFile);

				if (!read_service_pidfile_version_strings(
						servicePidFile,
						versionString,
						extensionVersionString))
				{
					/* warn about it and continue */
					log_warn("Failed to read version string for "
							 "service \"%s\" in pidfile \"%s\"",
							 serviceName,
							 servicePidFile);
				}
				else
				{
					json_object_set_string(jsServiceObj,
										   "version", versionString);
					json_object_set_string(jsServiceObj,
										   "pgautofailover",
										   extensionVersionString);
				}
			}

			json_array_append_value(jsServicesArray, jsService);
		}
	}

	json_object_set_value(jsobj, "services", jsServices);

	free(fileContents);
}


bool
is_process_stopped(const char *pidfile, bool *stopped, pid_t *pid)
{
	if (!file_exists(pidfile))
	{
		*stopped = true;
		return true;
	}

	if (!read_pidfile(pidfile, pid))
	{
		log_error("Failed to read PID file \"%s\"", pidfile);
		return false;
	}

	*stopped = false;
	return true;
}


/*
 * wait_for_process_to_stop waits until the PID found in the pidfile is not running
 * anymore.
 */
bool
wait_for_process_to_stop(const char *pidfile, int timeout, bool *stopped, pid_t *pid)
{
	if (!is_process_stopped(pidfile, stopped, pid))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("An instance of pg_autoctl is running with PID %d, "
			 "waiting for it to stop.", *pid);

	int timeout_counter = timeout;

	while (timeout_counter > 0)
	{
		if (kill(*pid, 0) == -1 && errno == ESRCH)
		{
			log_info("The pg_autoctl instance with pid %d "
					 "has now terminated.",
					 *pid);
			*stopped = true;
			return true;
		}

		sleep(1);
		--timeout_counter;
	}

	*stopped = false;
	return true;
}
