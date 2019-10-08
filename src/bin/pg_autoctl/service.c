/*
 * src/bin/pg_autoctl/service.c
 *   Starts and stop the sub-processes needed for pg_autoctl run. That's the
 *   embedded HTTPd process, the main loop when using a monitor, the internal
 *   sub-command listener, and the postgres main process itself, too.
 *
 *   pg_autoctl run
 *    - keeper run loop   [monitor enabled]
 *    - httpd server      [all cases]
 *    - listener          [all cases] [published API varies]
 *      - pg_autoctl do fsm assign single
 *      - pg_autoctl do fsm assign wait_primary
 *      - pg_autoctl enable maintenance
 *      - pg_autoctl disable maintenance
 *    - postgres -p 5432 -h localhost -k /tmp
 *
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cli_root.h"
#include "defaults.h"
#include "httpd.h"
#include "keeper.h"
#include "keeper_listener.h"
#include "keeper_pg_init.h"
#include "service.h"
#include "signals.h"


/* internal services API */
typedef struct subprocess
{
	char name[NAMEDATALEN];		/* process internal name */
	pid_t pid;					/* process pid */
} SubProcess;

static bool service_start_with_monitor(Keeper *keeper);
static bool service_start_without_monitor(Keeper *keeper);
static bool service_supervisor(int countSubprocesses, SubProcess pids[]);
static bool service_quit_other_subprocesses(pid_t pid, int status,
											SubProcess pids[]);
static SubProcess service_find_subprocess(pid_t pid, SubProcess pids[]);

/* pid file creation and reading */
static bool create_pidfile(const char *pidfile, pid_t pid);
static bool remove_pidfile(const char *pidfile);


/*
 * keeper_service_init initialises the bits and pieces that the keeper service
 * depend on:
 *
 *  - sets the signal handlers
 *  - check pidfile to see if the service is already running
 *  - creates the pidfile for our service
 *  - clean-up from previous execution
 */
bool
service_init(Keeper *keeper, pid_t *pid)
{
	KeeperConfig *config = &(keeper->config);

	log_trace("keeper_service_init");

	/* Establish a handler for signals. */
	(void) set_signal_handlers();

	/* Check that the keeper service is not already running */
	if (read_pidfile(config->pathnames.pid, pid))
	{
		log_fatal("An instance of this keeper is already running with PID %d, "
				  "as seen in pidfile \"%s\"",
				  *pid, config->pathnames.pid);
		return false;
	}

	/*
	 * Check that the init is finished. This function is called from
	 * cli_service_run when used in the CLI `pg_autoctl run`, and the
	 * function cli_service_run calls into keeper_init(): we know that we could
	 * read a keeper state file.
	 */
	if (!config->monitorDisabled && file_exists(config->pathnames.init))
	{
		log_warn("The `pg_autoctl create` did not complete, completing now.");

		if (!keeper_pg_init_continue(keeper, config))
		{
			/* errors have already been logged. */
			return false;
		}
	}

	/* Ok, we're going to start. Time to create our PID file. */
	*pid = getpid();

	if (!create_pidfile(config->pathnames.pid, *pid))
	{
		log_fatal("Failed to write our PID to \"%s\"", config->pathnames.pid);
		return false;
	}

	return true;
}


/*
 * keeper_service_stop stops the service and removes the pid file.
 */
bool
service_stop(Keeper *keeper)
{
	KeeperConfig *config = &(keeper->config);

	log_info("pg_autoctl service stopping");

	if (!remove_pidfile(config->pathnames.pid))
	{
		log_error("Failed to remove pidfile \"%s\"", config->pathnames.pid);
		return false;
	}
	return true;
}


/*
 * service_start starts the sub-processes that collectively implement our
 * pg_autoctl run service. The list of sub-processes is not the same depending
 * if we're running with or without a monitor.
 */
bool
service_start(Keeper *keeper)
{
	if (keeper->config.monitorDisabled)
	{
		return service_start_without_monitor(keeper);
	}
	else
	{
		return service_start_with_monitor(keeper);
	}
}


/*
 * service_start_with_monitor starts all the sub-processes needed when running
 * the keeper service with a monitor. That includes the main loop.
 */
static bool
service_start_with_monitor(Keeper *keeper)
{
	int countSubprocesses = 0;
	SubProcess pids[] = {
		{ "Node Active", 0 },
		{ "HTTPd",       0 },
		{ "", -1 }
	};

	if (!keeper_check_monitor_extension_version(keeper))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_MONITOR);
	}

	if (!keeper_start_node_active_process(keeper, &pids[0].pid))
	{
		log_fatal("Failed to start the node_active process");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	++countSubprocesses;

	if (!httpd_start_process(keeper->config.pgSetup.pgdata,
							 keeper->config.httpd.listen_address,
							 keeper->config.httpd.port,
							 &pids[1].pid))
	{
		/* well terminate here, and signal the node_active process to quit */
		kill(pids[0].pid, SIGQUIT);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	++countSubprocesses;

	if (!service_supervisor(countSubprocesses, pids))
	{
		log_fatal("Something went wrong in sub-process supervision, "
				  "stopping now. See above for details.");
	}

	return service_stop(keeper);
}


/*
 * service_start_without_monitor starts all the sub-processes needed when
 * running the keeper service without a monitor.
 *
 * TODO: check the signals situation and wait() for sub-processes in the parent
 * process.
 */
static bool
service_start_without_monitor(Keeper *keeper)
{
	int countSubprocesses = 0;
	SubProcess pids[] = {
		{ "listener", 0 },
		{ "HTTPd",    0 },
		{ "", -1 }
	};

	log_info("pg_autoctl is setup to run without a monitor, "
			 "the NodeActive protocol is not used.");

	/* start the command pipe sub-process */
	if (!keeper_listener_start(keeper->config.pgSetup.pgdata, &(pids[0].pid)))
	{
		log_fatal("Failed to start the command listener process");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	++countSubprocesses;

	/* start the HTTPd service in a sub-process */
	if (!httpd_start_process(keeper->config.pgSetup.pgdata,
							 keeper->config.httpd.listen_address,
							 keeper->config.httpd.port,
							 &(pids[1].pid)))
	{
		/* well terminate here, and signal the listener to do the same */
		kill(pids[0].pid, SIGQUIT);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}
	++countSubprocesses;

	if (!service_supervisor(countSubprocesses, pids))
	{
		log_fatal("Something went wrong in sub-process supervision, "
				  "stopping now. See above for details.");
	}

	return service_stop(keeper);
}


/*
 * service_supervisor calls waitpid() in a loop until the sub processes that
 * implement our main activities have stopped, and then it cleans-up the PID
 * file.
 */
static bool
service_supervisor(int startedSubprocesses, SubProcess pids[])
{
	int countSubprocesses = startedSubprocesses;

	/* wait until all subprocesses are done */
	while (countSubprocesses > 0)
	{
		pid_t pid;
		int status;

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
				SubProcess dead = service_find_subprocess(pid, pids);

				/* one child process is no more */
				--countSubprocesses;

				if (returnCode == 0)
				{
					log_debug("Subprocess %s with pid %d %s [%d]",
							  dead.name, dead.pid, verb, returnCode);
				}
				else
				{
					(void) service_quit_other_subprocesses(pid, status, pids);
				}
			}
		}
	}

	return true;
}


/*
 * service_quit_other_subprocesses sends the QUIT signal to other known
 * sub-processes when on of does is reported dead.
 */
static bool
service_quit_other_subprocesses(pid_t pid, int status, SubProcess pids[])
{
	char *verb = WIFEXITED(status) ? "exited" : "failed";
	int returnCode = WEXITSTATUS(status);

	int idx = 0;
	bool found = false;

	for (idx=0; pids[idx].pid > -1; idx++)
	{
		if (pid == pids[idx].pid)
		{
			found = true;
			log_error("Internal process %s %s [%d]",
					  pids[idx].name, verb, returnCode);
		}
	}

	if (found)
	{
		/*
		 * In case of unexpected stop (bug), we stop the other processes too.
		 * Someone might then notice (such as systemd) and restart the whole
		 * thing again.
		 */
		if (!(asked_to_stop || asked_to_stop_fast))
		{
			for (idx=0; pids[idx].pid > -1; idx++)
			{
				if (pid != pids[idx].pid)
				{
					kill(pids[idx].pid, SIGQUIT);
				}
			}
		}
	}
	else
	{
		/* we certainly don't expect that! */
		log_fatal("BUG: waitpid() returned an unknown PID: %d", pid);
		return false;
	}

	return true;
}


/*
 * service_get_pid_name loops over the SubProcess array to find given pid and
 * return its entry in the array.
 */
static SubProcess
service_find_subprocess(pid_t pid, SubProcess pids[])
{
	SubProcess unknown = {.name = "unknown", .pid = -1};
	int idx = 0;

	for (idx=0; pids[idx].pid > -1; idx++)
	{
		if (pid == pids[idx].pid)
		{
			return pids[idx];
		}
	}

	return unknown;
}

/*
 * create_pidfile writes our pid in a file.
 *
 * When running in a background loop, we need a pidFile to add a command line
 * tool that send signals to the process. The pidfile has a single line
 * containing our PID.
 */
static bool
create_pidfile(const char *pidfile, pid_t pid)
{
	char content[BUFSIZE];

	log_trace("create_pidfile(%d): \"%s\"", pid, pidfile);

	sprintf(content, "%d", pid);

	return write_file(content, strlen(content), pidfile);
}


/*
 * read_pidfile read the keeper's pid from a file, and returns true when we
 * could read a PID that belongs to a currently running process.
 */
bool
read_pidfile(const char *pidfile, pid_t *pid)
{
	long fileSize = 0L;
	char *fileContents = NULL;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	if (sscanf(fileContents, "%d", pid) == 1)
	{
		free(fileContents);

		/* is it a stale file? */
		if (kill(*pid, 0) == 0)
		{
			return true;
		}
		else
		{
			log_debug("Failed to signal pid %d: %s", *pid, strerror(errno));
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
		free(fileContents);

		log_debug("Failed to read the PID file \"%s\", removing it", pidfile);
		(void) remove_pidfile(pidfile);

		return false;
	}

	/* no warning, it's cool that the file doesn't exists. */
	return false;
}


/*
 * remove_pidfile removes the keeper's pidfile.
 */
static bool
remove_pidfile(const char *pidfile)
{
	if (remove(pidfile) != 0)
	{
		log_error("Failed to remove keeper's pid file \"%s\": %s",
				  pidfile, strerror(errno));
		return false;
	}
	return true;
}
