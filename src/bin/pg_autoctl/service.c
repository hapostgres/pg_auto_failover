/*
 * src/bin/pg_autoctl/service.c
 *   Utilities to start/stop the pg_autoctl service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "defaults.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "state.h"
#include "service.h"
#include "signals.h"
#include "string_utils.h"

static bool service_init(const char *pidfile, pid_t *pid);

static bool service_supervisor(
	pid_t start_pid,
	Service services[],
	int serviceCount,
	const char *pidfile);

static bool service_find_subprocess(
	Service services[],
	int serviceCount,
	pid_t pid,
	Service **result);

static void service_quit_other_subprocesses(
	pid_t pid,
	Service services[],
	int serviceCount);


/*
 * service_start starts given services as sub-processes and then supervise
 * them.
 */
bool
service_start(Service services[], int serviceCount, const char *pidfile)
{
	pid_t start_pid = 0;
	int serviceIndex = 0;

	/*
	 * Create our PID file, or quit now if another pg_autoctl instance is
	 * runnning.
	 */
	if (!service_init(pidfile, &start_pid))
	{
		log_fatal("Failed to setup pg_autoctl pidfile and signal handlers");
		return false;
	}

	/*
	 * Start all the given services, in order.
	 *
	 * If we fail to start one of the given services, then we SIGQUIT the
	 * services we managed to start before, in reverse order of starting-up,
	 * and stop here.
	 */
	for (serviceIndex=0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(services[serviceIndex]);
		bool started = false;

		log_info("Starting pg_autoctl %s service", service->name);

		started = (*service->startFunction)(service->context, &(service->pid));

		if (!started)
		{
			/* SIGQUIT the processes that started successfully */
			int idx = 0;

			log_error("Failed to start service %s, "
					  "stopping already started services and pg_autoctl",
					  service->name);

			for (idx = serviceIndex-1; idx > 0; idx--)
			{
				if (kill(services[idx].pid, SIGQUIT) != 0)
				{
					log_error("Failed to send SIGQUIT to service %s with pid %d",
							  services[idx].name, services[idx].pid);
				}
			}

			/* we return false always, even if service_stop is successful */
			(void) service_stop(pidfile);

			return false;
		}
	}

	/* now supervise sub-processes and implement retry strategy */
	if (!service_supervisor(start_pid, services, serviceCount, pidfile))
	{
		log_fatal("Something went wrong in sub-process supervision, "
				  "stopping now. See above for details.");
	}

	return service_stop(pidfile);
}


/*
 * service_supervisor calls waitpid() in a loop until the sub processes that
 * implement our main activities have stopped, and then it cleans-up the PID
 * file.
 */
static bool
service_supervisor(pid_t start_pid,
				   Service services[],
				   int serviceCount,
				   const char *pidfile)
{
	int subprocessCount = serviceCount;

	/* wait until all subprocesses are done */
	while (subprocessCount > 0)
	{
		pid_t pid;
		int status;

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(pidfile, start_pid);

		/* ignore errors */
		pid = waitpid(-1, &status, WNOHANG);

		switch (pid)
		{
			case -1:
			{
				if (errno == ECHILD)
				{
					/* no more childrens */
					if (asked_to_stop || asked_to_stop_fast)
					{
						/* off we go */
						log_info("Internal subprocesses are done, stopping");
						return true;
					}
				}
				else
				{
					log_fatal("Oops, waitpid() failed with: %s",
							  strerror(errno));
					return false;
				}
			}

			case 0:
			{
				/*
				 * We're using WNOHANG, 0 means there are no stopped or exited
				 * children, it's all good. It's the expected case when
				 * everything is running smoothly, so enjoy and sleep for
				 * awhile.
				 */
				sleep(1);
				break;
			}

			default:
			{
				char *verb = WIFEXITED(status) ? "exited" : "failed";
				int returnCode = WEXITSTATUS(status);
				Service *dead = NULL;

				if (!service_find_subprocess(services, serviceCount, pid, &dead))
				{
					log_error("Unknown subprocess died with pid %d", pid);
					break;
				}

				/* one child process is no more */
				--subprocessCount;

				if (returnCode == 0)
				{
					log_info("Stopped pg_autoctl service %s", dead->name);
				}
				else
				{
					/* TODO: implement a maxRetry/maxTime restart strategy */
					bool restarted = false;

					log_error("Subprocess %s %s with exit status %d",
							  dead->name, verb, returnCode);

					restarted =
						(*dead->startFunction)(dead->context, &(dead->pid));

					if (!restarted)
					{
						log_fatal("Failed to restart service %s", dead->name);

						(void) service_quit_other_subprocesses(dead->pid,
															   services,
															   serviceCount);
					}
				}

				break;
			}
		}
	}

	return true;
}


/*
 * service_get_pid_name loops over the SubProcess array to find given pid and
 * return its entry in the array.
 */
static bool
service_find_subprocess(Service services[],
						int serviceCount,
						pid_t pid,
						Service **result)
{
	int serviceIndex = 0;

	for (serviceIndex=0; serviceIndex < serviceCount; serviceIndex++)
	{
		if (pid == services[serviceIndex].pid)
		{
			*result = &(services[serviceIndex]);
			return true;
		}
	}

	return false;
}


/*
 * service_quit_other_subprocesses sends the QUIT signal to other known
 * sub-processes when on of does is reported dead.
 */
static void
service_quit_other_subprocesses(pid_t pid, Service services[], int serviceCount)
{
	int serviceIndex = 0;

	/*
	 * In case of unexpected stop (bug), we stop the other processes too.
	 * Someone might then notice (such as systemd) and restart the whole
	 * thing again.
	 */
	if (!(asked_to_stop || asked_to_stop_fast))
	{
		for (serviceIndex=0; serviceIndex < serviceCount; serviceIndex++)
		{
			Service target = services[serviceIndex];

			if (pid != target.pid)
			{
				if (kill(target.pid, SIGQUIT) != 0)
				{
					log_error("Failed to send SIGQUIT to service %s with pid %d",
							  target.name, target.pid);
				}
			}
		}
	}
}


/*
 * service_init initializes our PID file and sets our signal handlers.
 */
static bool
service_init(const char *pidfile, pid_t *pid)
{
	log_trace("service_init");

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	/* Check that the keeper service is not already running */
	if (read_pidfile(pidfile, pid))
	{
		log_fatal("An instance of pg_autoctl is already running with PID %d, "
				  "as seen in pidfile \"%s\"", *pid, pidfile);
		return false;
	}

	/* Ok, we're going to start. Time to create our PID file. */
	*pid = getpid();

	if (!create_pidfile(pidfile, *pid))
	{
		log_fatal("Failed to write our PID to \"%s\"", pidfile);
		return false;
	}

	return true;
}


/*
 * service_stop stops the service and removes the pid file.
 */
bool
service_stop(const char *pidfile)
{
	log_info("Stop pg_autoctl");

	if (!remove_pidfile(pidfile))
	{
		log_error("Failed to remove pidfile \"%s\"", pidfile);
		return false;
	}
	return true;
}


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
	char content[BUFSIZE];

	log_trace("create_pidfile(%d): \"%s\"", pid, pidfile);

	sformat(content, BUFSIZE, "%d", pid);

	return write_file(content, strlen(content), pidfile);
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
