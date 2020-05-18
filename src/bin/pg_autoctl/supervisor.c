/*
 * src/bin/pg_autoctl/supervisor.c
 *   Supervisor for services run in sub-processes.
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
#include "supervisor.h"
#include "signals.h"
#include "string_utils.h"

/*
 * Supervisor restart strategy.
 *
 * The idea is to restart processes that have failed, so that we can stay
 * available without external intervention. Sometimes though if the
 * configuration is wrong or the data directory damaged beyond repair or for
 * some reasons, the service can't be restarted.
 *
 * There is no magic or heuristic that can help us decide if a failure is
 * transient or permanent, so we implement the simple thing: we restart our
 * dead service up to 5 times in a row, and spend up to 10 seconds retrying,
 * and stop as soon as one of those conditions is reached.
 *
 * This strategy is inspired by http://erlang.org/doc/man/supervisor.html and
 * http://erlang.org/doc/design_principles/sup_princ.html#maximum-restart-intensity
 *
 *    If more than MaxR number of restarts occur in the last MaxT seconds, the
 *    supervisor terminates all the child processes and then itself. The
 *    termination reason for the supervisor itself in that case will be
 *    shutdown.
 *
 * SUPERVISOR_SERVICE_MAX_RETRY is MaxR, SUPERVISOR_SERVICE_MAX_TIME is MaxT.
 *
 * SUPERVISOR_SERVICE_RUNNING_TIME is the time we allow before considering that
 * a restart has been successfull, because we implement async process start: we
 * know that the process has started (fork() succeeds), only to know later that
 * it failed (waitpid() reports a non-zero exit status).
 */
#define SUPERVISOR_SERVICE_MAX_RETRY 5
#define SUPERVISOR_SERVICE_MAX_TIME 10     /* in seconds */
#define SUPERVISOR_SERVICE_RUNNING_TIME 15 /* in seconds */

static bool supervisor_init(Supervisor *supervisor);
static bool supervisor_loop(Supervisor *supervisor);

static bool supervisor_find_service(Supervisor *supervisor, pid_t pid,
									Service **result);

static void supervisor_reset_service_restart_counters(Supervisor *supervisor);

static void supervisor_stop_subprocesses(Supervisor *supervisor);

static void supervisor_stop_other_services(Supervisor *supervisor, pid_t pid);

static bool supervisor_signal_process_group(int signal);

static void supervisor_reload_services(Supervisor *supervisor);

static void supervisor_shutdown_sequence(int stoppingLoopCounter);

static bool supervisor_restart_service(Supervisor *supervisor,
									   Service *service,
									   int status);


/*
 * supervisor_start starts given services as sub-processes and then supervise
 * them.
 */
bool
supervisor_start(Service services[], int serviceCount, const char *pidfile)
{
	int serviceIndex = 0;
	bool success = true;

	Supervisor supervisor = { services, serviceCount, { 0 }, -1 };

	/* copy the pidfile over to our supervisor structure */
	strlcpy(supervisor.pidfile, pidfile, MAXPGPATH);

	/*
	 * Create our PID file, or quit now if another pg_autoctl instance is
	 * runnning.
	 */
	if (!supervisor_init(&supervisor))
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
	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(services[serviceIndex]);
		bool started = false;

		log_debug("Starting pg_autoctl %s service", service->name);

		started = (*service->startFunction)(service->context, &(service->pid));

		if (started)
		{
			service->retries = 0;
			service->startTime = time(NULL);

			log_info("Started pg_autoctl %s service with pid %d",
					 service->name, service->pid);
		}
		else
		{
			int idx = 0;

			log_error("Failed to start service %s, "
					  "stopping already started services and pg_autoctl",
					  service->name);

			for (idx = serviceIndex - 1; idx > 0; idx--)
			{
				if (kill(services[idx].pid, SIGQUIT) != 0)
				{
					log_error("Failed to send SIGQUIT to service %s with pid %d",
							  services[idx].name, services[idx].pid);
				}
			}

			/* we return false always, even if supervisor_stop is successful */
			(void) supervisor_stop(&supervisor);

			return false;
		}
	}

	/* now supervise sub-processes and implement retry strategy */
	if (!supervisor_loop(&supervisor))
	{
		log_fatal("Something went wrong in sub-process supervision, "
				  "stopping now. See above for details.");
		success = false;
	}

	return supervisor_stop(&supervisor) && success;
}


/*
 * service_supervisor calls waitpid() in a loop until the sub processes that
 * implement our main activities have stopped, and then it cleans-up the PID
 * file.
 */
static bool
supervisor_loop(Supervisor *supervisor)
{
	int subprocessCount = supervisor->serviceCount;
	int stoppingLoopCounter = 0;

	/* wait until all subprocesses are done */
	while (subprocessCount > 0)
	{
		pid_t pid;
		int status;

		/* Check that we still own our PID file, or quit now */
		(void) check_pidfile(supervisor->pidfile, supervisor->pid);

		/* If necessary, now is a good time to reload services */
		if (asked_to_reload)
		{
			(void) supervisor_reload_services(supervisor);
		}

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
					log_fatal("Failed to call waitpid(): %m");
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
				if (asked_to_stop || asked_to_stop_fast)
				{
					supervisor->cleanExit = true;
					supervisor->shutdownSequenceInProgress = true;

					/*
					 * Stop all the services.
					 */
					if (stoppingLoopCounter == 0)
					{
						(void) supervisor_stop_subprocesses(supervisor);
					}

					(void) supervisor_shutdown_sequence(stoppingLoopCounter++);
				}

				/*
				 * No stopped or exited children, not asked to stopped.
				 * Everything is good. Time to check if we should reset some
				 * retries counters.
				 */
				else
				{
					(void) supervisor_reset_service_restart_counters(supervisor);
				}
				break;
			}

			default:
			{
				Service *dead = NULL;
				uint64_t now = time(NULL);

				/* map the dead child pid to the known dead internal service */
				if (!supervisor_find_service(supervisor, pid, &dead))
				{
					log_error("Unknown subprocess died with pid %d", pid);
					break;
				}

				/* one child process is no more */
				--subprocessCount;

				/*
				 * One child process has terminated at least once now. Time to
				 * update our restart strategy counters.
				 */
				if (dead->stopTime == 0)
				{
					dead->stopTime = now;
				}

				/* apply the service restart policy */
				if (supervisor_restart_service(supervisor, dead, status))
				{
					++subprocessCount;
				}

				break;
			}
		}

		/* avoid buzy looping on waitpid(WNOHANG) */
		pg_usleep(100 * 1000); /* 100 ms */
	}

	/* we track in the main loop if it's a cleanExit or not */
	return supervisor->cleanExit;
}


/*
 * supervisor_find_service loops over the SubProcess array to find given pid and
 * return its entry in the array.
 */
static bool
supervisor_find_service(Supervisor *supervisor, pid_t pid, Service **result)
{
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		if (pid == supervisor->services[serviceIndex].pid)
		{
			*result = &(supervisor->services[serviceIndex]);
			return true;
		}
	}

	return false;
}


/*
 * supervisor_reset_service_restart_counters loops over known services and
 * reset the retries count and stopTime of services that have been known
 * running for more than 15s (SUPERVISOR_SERVICE_RUNNING_TIME is 15s).
 */
static void
supervisor_reset_service_restart_counters(Supervisor *supervisor)
{
	uint64_t now = time(NULL);
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *target = &(supervisor->services[serviceIndex]);

		if (target->stopTime > 0)
		{
			if ((now - target->startTime) > SUPERVISOR_SERVICE_RUNNING_TIME)
			{
				target->retries = 0;
				target->stopTime = 0;
			}
		}
	}
}


/*
 * supervisor_reload_services sends SIGHUP to all our services.
 */
static void
supervisor_reload_services(Supervisor *supervisor)
{
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(supervisor->services[serviceIndex]);

		log_info("Reloading service \"%s\" by signaling pid %d with SIGHUP",
				 service->name, service->pid);

		if (kill(service->pid, SIGHUP) != 0)
		{
			log_error("Failed to send SIGQUIT to service %s with pid %d",
					  service->name, service->pid);
		}
	}

	/* reset our signal handling facility */
	asked_to_reload = 0;
}


/*
 * supervisor_stop_subprocesses calls the stopFunction for all the registered
 * services to initiate the shutdown sequence.
 */
static void
supervisor_stop_subprocesses(Supervisor *supervisor)
{
	int signal = asked_to_stop_fast ? SIGINT : SIGTERM;
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(supervisor->services[serviceIndex]);

		if (kill(service->pid, signal) != 0)
		{
			log_error("Failed to send signal %s to service %s with pid %d",
					  strsignal(signal), service->name, service->pid);
		}
	}
}


/*
 * supervisor_stop_other_subprocesses sends the QUIT signal to other known
 * sub-processes when on of does is reported dead.
 */
static void
supervisor_stop_other_services(Supervisor *supervisor, pid_t pid)
{
	int signal = asked_to_stop_fast ? SIGINT : SIGTERM;
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	/*
	 * In case of unexpected stop (bug), we stop the other processes too.
	 * Someone might then notice (such as systemd) and restart the whole
	 * thing again.
	 */
	if (!(asked_to_stop || asked_to_stop_fast))
	{
		for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
		{
			Service *service = &(supervisor->services[serviceIndex]);

			if (service->pid != pid)
			{
				if (kill(service->pid, signal) != 0)
				{
					log_error("Failed to send SIGQUIT to service %s with pid %d",
							  service->name, service->pid);
				}
			}
		}
	}
}


/*
 * supervisor_signal_process_group sends a signal (SIGQUIT) to our own process
 * group, which we are the leader of.
 *
 * That's used when we have received a signal already (asked_to_stop ||
 * asked_to_stop_fast) and our sub-processes are still running after a while.
 * It suggests that only the leader process was signaled rather than all the
 * group.
 */
static bool
supervisor_signal_process_group(int signal)
{
	pid_t pid = getpid();
	pid_t pgrp = getpgid(pid);

	if (pgrp == -1)
	{
		log_fatal("Failed to get the process group id of pid %d: %m", pid);
		return false;
	}

	if (killpg(pgrp, signal) != 0)
	{
		log_error("Failed to send %s to the keeper's pid %d: %m",
				  strsignal(signal), pgrp);
		return false;
	}

	return true;
}


/*
 * supervisor_init initializes our PID file and sets our signal handlers.
 */
static bool
supervisor_init(Supervisor *supervisor)
{
	bool exitOnQuit = false;
	log_trace("supervisor_init");

	/* Establish a handler for signals. */
	(void) set_signal_handlers(exitOnQuit);

	/* Check that the keeper service is not already running */
	if (read_pidfile(supervisor->pidfile, &(supervisor->pid)))
	{
		log_fatal("An instance of pg_autoctl is already running with PID %d, "
				  "as seen in pidfile \"%s\"",
				  supervisor->pid,
				  supervisor->pidfile);
		return false;
	}

	/* Ok, we're going to start. Time to create our PID file. */
	supervisor->pid = getpid();

	if (!create_pidfile(supervisor->pidfile, supervisor->pid))
	{
		log_fatal("Failed to write our PID to \"%s\"", supervisor->pidfile);
		return false;
	}

	return true;
}


/*
 * supervisor_stop stops the service and removes the pid file.
 */
bool
supervisor_stop(Supervisor *supervisor)
{
	log_info("Stop pg_autoctl");

	if (!remove_pidfile(supervisor->pidfile))
	{
		log_error("Failed to remove pidfile \"%s\"", supervisor->pidfile);
		return false;
	}
	return true;
}


/*
 * supervisor_shutdown_sequence handles the shutdown sequence of the supervisor
 * and insist towards registered services that now is the time to shutdown when
 * they fail to do so timely.
 *
 * The stoppingLoopCounter is zero on the first loop and we do nothing, when
 * it's 1 we have been waiting once without any child process reported absent
 * by waitpid(), tell the user we are waiting.
 *
 * At 50 loops (typically we add a 100ms wait per loop), send SIGTERM.
 *
 * At every 100 loops, send SIGINT.
 */
static void
supervisor_shutdown_sequence(int stoppingLoopCounter)
{
	if (stoppingLoopCounter == 1)
	{
		log_info("Waiting for subprocesses to terminate.");
	}

	/*
	 * If we've been waiting for quite a while for
	 * sub-processes to terminate. Let's signal again all our
	 * process group ourselves and see what happens next.
	 */
	if (stoppingLoopCounter == 50)
	{
		log_info("pg_autoctl services are still running, "
				 "signaling them with SIGTERM.");

		if (!supervisor_signal_process_group(SIGTERM))
		{
			log_warn("Still waiting for subprocesses to terminate.");
		}
	}

	/*
	 * Wow it's been a very long time now...
	 */
	if (stoppingLoopCounter > 0 &&
		stoppingLoopCounter % 100 == 0)
	{
		log_info("pg_autoctl services are still running, "
				 "signaling them with SIGINT.");

		if (!supervisor_signal_process_group(SIGINT))
		{
			log_warn("Still waiting for subprocesses to terminate.");
		}
	}
}


/*
 * supervisor_restart_service restarts given service and maintains its MaxR and
 * MaxT counters.
 */
static bool
supervisor_restart_service(Supervisor *supervisor, Service *service, int status)
{
	char *verb = WIFEXITED(status) ? "exited" : "failed";
	int returnCode = WEXITSTATUS(status);
	uint64_t now = time(NULL);
	int logLevel = LOG_ERROR;
	bool restarted = false;

	/*
	 * If we're in the middle of a shutdown sequence, we won't have to restart
	 * services and apply any restart strategy etc.
	 */
	if (supervisor->shutdownSequenceInProgress)
	{
		log_trace("supervisor_restart_service: shutdownSequenceInProgress");
		return false;
	}

	/* refrain from an ERROR message for a TEMPORARY service */
	if (service->policy == RP_TEMPORARY || returnCode == EXIT_CODE_QUIT)
	{
		logLevel = LOG_INFO;
	}

	log_level(logLevel, "pg_autoctl service %s %s with exit status %d",
			  service->name, verb, returnCode);

	/*
	 * We don't restart temporary processes at all: we're done already.
	 */
	if (service->policy == RP_TEMPORARY)
	{
		return true;
	}

	/*
	 * Update our restart strategy counters. Well only the count of retries, we
	 * want to keep our stopTime as it is so as to know that we've been trying
	 * to restart 4 times in 10s. It's not 10s each time, it's 10s total.
	 */
	service->retries += 1;

	if (service->retries >= SUPERVISOR_SERVICE_MAX_RETRY ||
		(now - service->stopTime) >= SUPERVISOR_SERVICE_MAX_TIME)
	{
		log_fatal("pg_autoctl service %s has already been "
				  "restarted %d times in the last %d seconds, "
				  "stopping now",
				  service->name,
				  service->retries,
				  (int) (now - service->startTime));

		/* exit with a non-zero exit code, and process with shutdown sequence */
		supervisor->cleanExit = false;
		supervisor->shutdownSequenceInProgress = true;

		(void) supervisor_stop_other_services(supervisor, service->pid);

		return false;
	}

	/*
	 * When a transient service has quit happily (with a zero exit status), we
	 * just shutdown the whole pg_autoctl. We consider this a clean shutdown.
	 *
	 * The main use case here is with the initialization of a node: unless
	 * using the --run option, we want to shutdown as soon as the
	 * initialisation is done.
	 *
	 * That's when using the "create" subcommand as in:
	 *
	 *  pg_autoctl create monitor
	 *  pg_autoctl create postgres
	 */
	if (service->policy == RP_TRANSIENT && returnCode == EXIT_CODE_QUIT)
	{
		/* exit with a happy exit code, and process with shutdown sequence */
		supervisor->cleanExit = true;
		supervisor->shutdownSequenceInProgress = true;

		(void) supervisor_stop_other_services(supervisor, service->pid);

		return false;
	}

	/*
	 * Now the service RestartPolicy is either RP_PERMANENT, and we need to
	 * restart it no matter what, or RP_TRANSIENT with a failure status
	 * (non-zero return code), and we need to start the service in that case
	 * too.
	 */
	restarted = (*service->startFunction)(service->context, &(service->pid));

	if (!restarted)
	{
		log_fatal("Failed to restart service %s", service->name);

		/* exit with a non-zero exit code, and process with shutdown sequence */
		supervisor->cleanExit = false;
		supervisor->shutdownSequenceInProgress = true;

		(void) supervisor_stop_other_services(supervisor, service->pid);

		return false;
	}

	/* we could restart, update our service start time */
	service->startTime = now;

	return true;
}
