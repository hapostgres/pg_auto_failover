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

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
#include "fsm.h"
#include "keeper.h"
#include "keeper_config.h"
#include "keeper_pg_init.h"
#include "log.h"
#include "monitor.h"
#include "pgctl.h"
#include "pidfile.h"
#include "state.h"
#include "supervisor.h"
#include "signals.h"
#include "string_utils.h"

static bool supervisor_init(Supervisor *supervisor);
static SupervisorExitMode supervisor_loop(Supervisor *supervisor);

static bool supervisor_find_service(Supervisor *supervisor, pid_t pid,
									Service **result);

static void supervisor_stop_subprocesses(Supervisor *supervisor);

static void supervisor_stop_other_services(Supervisor *supervisor, pid_t pid);

static bool supervisor_signal_process_group(int signal);

static void supervisor_reload_services(Supervisor *supervisor);

static void supervisor_handle_signals(Supervisor *supervisor);

static void supervisor_shutdown_sequence(Supervisor *supervisor);

static bool supervisor_restart_service(Supervisor *supervisor,
									   Service *service,
									   int status);

static bool supervisor_may_restart(Service *service);

static bool supervisor_update_pidfile(Supervisor *supervisor);


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

		log_debug("Starting pg_autoctl %s service", service->name);

		bool started = (*service->startFunction)(service->context, &(service->pid));

		if (started)
		{
			uint64_t now = time(NULL);
			RestartCounters *counters = &(service->restartCounters);

			counters->count = 1;
			counters->position = 0;
			counters->startTime[counters->position] = now;

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

	/*
	 * We need to update our pid file with the PID for every service.
	 */
	if (!supervisor_update_pidfile(&supervisor))
	{
		log_fatal("Failed to update pidfile \"%s\", stopping all services now",
				  supervisor.pidfile);

		supervisor.exitMode = SUPERVISOR_EXIT_ERROR;
		supervisor.shutdownSequenceInProgress = true;

		(void) supervisor_stop_subprocesses(&supervisor);

		return false;
	}

	/* now supervise sub-processes and implement retry strategy */
	switch (supervisor_loop(&supervisor))
	{
		case SUPERVISOR_EXIT_FATAL:
		{
			log_fatal("A subprocess has reported a fatal error, stopping now. "
					  "See above for details.");
			success = false;
			break;
		}

		case SUPERVISOR_EXIT_ERROR:
		{
			log_fatal("Something went wrong in sub-process supervision, "
					  "stopping now. See above for details.");
			success = false;
			break;
		}

		case SUPERVISOR_EXIT_CLEAN:
		{
			success = true;
			break;
		}
	}

	return supervisor_stop(&supervisor) && success;
}


/*
 * service_supervisor calls waitpid() in a loop until the sub processes that
 * implement our main activities have stopped, and then it cleans-up the PID
 * file.
 */
static SupervisorExitMode
supervisor_loop(Supervisor *supervisor)
{
	int subprocessCount = supervisor->serviceCount;
	bool firstLoop = true;

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
			log_info("pg_autoctl received a SIGHUP signal, "
					 "reloading configuration");
			(void) supervisor_reload_services(supervisor);
		}

		if (firstLoop)
		{
			firstLoop = false;
		}
		else
		{
			/* avoid busy looping on waitpid(WNOHANG) */
			pg_usleep(100 * 1000); /* 100 ms */
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
					if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
					{
						/* off we go */
						log_info("Internal subprocesses are done, stopping");
						return true;
					}

					log_fatal("Unexpected ECHILD error from waitpid()");
					return false;
				}
				else
				{
					log_debug("Failed to call waitpid(): %m");
				}

				break;
			}

			case 0:
			{
				/*
				 * We're using WNOHANG, 0 means there are no stopped or exited
				 * children, it's all good. It's the expected case when
				 * everything is running smoothly, so enjoy and sleep for
				 * awhile.
				 */

				/* handle SIGTERM and SIGINT if we've received them */
				(void) supervisor_handle_signals(supervisor);

				/* if we're in a shutdown sequence, make sure we terminate */
				if (supervisor->shutdownSequenceInProgress)
				{
					(void) supervisor_shutdown_sequence(supervisor);
				}

				break;
			}

			default:
			{
				Service *dead = NULL;

				/* map the dead child pid to the known dead internal service */
				if (!supervisor_find_service(supervisor, pid, &dead))
				{
					log_error("Unknown subprocess died with pid %d", pid);
					break;
				}

				/* one child process is no more */
				--subprocessCount;

				/* apply the service restart policy */
				if (supervisor_restart_service(supervisor, dead, status))
				{
					++subprocessCount;
				}

				break;
			}
		}
	}

	/* we track in the main loop if it's a cleanExit or not */
	return supervisor->exitMode;
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
			log_error("Failed to send SIGHUP to service %s with pid %d",
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
	int signal = get_current_signal(SIGTERM);
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
	int signal = get_current_signal(SIGTERM);
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
					log_error("Failed to send signal %s to service %s with pid %d",
							  signal_to_string(signal),
							  service->name,
							  service->pid);
				}
			}
		}
	}
}


/*
 * supervisor_signal_process_group sends a signal to our own process group,
 * which we are the leader of.
 *
 * That's used when we have received a signal already (asked_to_stop ||
 * asked_to_stop_fast) and our sub-processes are still running after a while.
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
				  signal_to_string(signal), pgrp);
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
 * If we have received a signal that instructs a shutdown, such as SIGTERM or
 * SIGINT, then we need to do one of these things:
 *
 * - first time we receive the signal, begin a shutdown sequence for all
 *   services and the main supervisor itself,
 *
 * - when receiving the signal again, if it's a SIGTERM continue the shutdown
 *   sequence,
 *
 * - when receiving a SIGINT forward it to our services so as to finish as fast
 *   as we can, and from then on always use SIGINT (to that end we use
 *   supervisor->shutdownSignal)
 *
 * Sending SIGTERM and then later SIGINT if the process is still running is a
 * classic way to handle service shutdown.
 */
static void
supervisor_handle_signals(Supervisor *supervisor)
{
	int signal = get_current_signal(SIGTERM);
	const char *signalStr = signal_to_string(signal);

	/* if no signal has been received, we have nothing to do here */
	if (!(asked_to_stop || asked_to_stop_fast || asked_to_quit))
	{
		return;
	}

	/*
	 * Once we have received and processed SIGQUIT we want to stay at this
	 * signal level. Once we have received SIGINT we may upgrade to SIGQUIT,
	 * but we won't downgrade to SIGTERM.
	 */
	supervisor->shutdownSignal =
		pick_stronger_signal(supervisor->shutdownSignal, signal);

	log_info("pg_autoctl received signal %s, terminating", signalStr);

	/* the first time we receive a signal, set the shutdown properties */
	if (!supervisor->shutdownSequenceInProgress)
	{
		supervisor->exitMode = SUPERVISOR_EXIT_CLEAN;
		supervisor->shutdownSequenceInProgress = true;
	}

	/* forward the signal to all our service to terminate them */
	(void) supervisor_stop_subprocesses(supervisor);

	/* allow for processing signals again: reset signal variables */
	switch (signal)
	{
		case SIGINT:
		{
			asked_to_stop_fast = 0;
			break;
		}

		case SIGTERM:
		{
			asked_to_stop = 0;
			break;
		}

		case SIGQUIT:
		{
			asked_to_quit = 0;
			break;
		}
	}
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
 * At 50 loops (typically we add a 100ms wait per loop), send either SIGTERM or
 * SIGINT.
 *
 * At every 100 loops, send SIGINT.
 */
static void
supervisor_shutdown_sequence(Supervisor *supervisor)
{
	if (supervisor->stoppingLoopCounter == 1)
	{
		log_info("Waiting for subprocesses to terminate.");
	}

	/*
	 * If we've been waiting for quite a while for sub-processes to terminate.
	 * Let's signal again all our process group ourselves and see what happens
	 * next.
	 */
	if (supervisor->stoppingLoopCounter == 50)
	{
		log_info("pg_autoctl services are still running, "
				 "signaling them with %s.",
				 signal_to_string(supervisor->shutdownSignal));

		if (!supervisor_signal_process_group(supervisor->shutdownSignal))
		{
			log_warn("Still waiting for subprocesses to terminate.");
		}
	}

	/*
	 * Wow it's been a very long time now...
	 */
	if (supervisor->stoppingLoopCounter > 0 &&
		supervisor->stoppingLoopCounter % 100 == 0)
	{
		log_info("pg_autoctl services are still running, "
				 "signaling them with SIGINT.");

		/* raise the signal from SIGTERM to SIGINT now */
		supervisor->shutdownSignal =
			pick_stronger_signal(supervisor->shutdownSignal, SIGINT);

		if (!supervisor_signal_process_group(supervisor->shutdownSignal))
		{
			log_warn("Still waiting for subprocesses to terminate.");
		}
	}

	/* increment our counter */
	supervisor->stoppingLoopCounter++;
}


/*
 * supervisor_restart_service restarts given service and maintains its MaxR and
 * MaxT counters.
 */
static bool
supervisor_restart_service(Supervisor *supervisor, Service *service, int status)
{
	uint64_t now = time(NULL);
	int logLevel = LOG_ERROR;

	RestartCounters *counters = &(service->restartCounters);

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
	if (service->policy == RP_TEMPORARY)
	{
		logLevel = LOG_INFO;
	}

	/* when a sub-process has quit and we're not shutting down, warn about it */
	else if (WIFEXITED(status) && WEXITSTATUS(status) == EXIT_CODE_QUIT)
	{
		logLevel = LOG_WARN;
	}

	if (WIFEXITED(status))
	{
		int returnCode = WEXITSTATUS(status);

		/* sometimes we don't want to restart even a PERMANENT service */
		if (returnCode == EXIT_CODE_DROPPED)
		{
			supervisor->exitMode = SUPERVISOR_EXIT_CLEAN;
			supervisor->shutdownSequenceInProgress = true;

			(void) supervisor_stop_other_services(supervisor, service->pid);

			return false;
		}
		else if (returnCode == EXIT_CODE_FATAL)
		{
			supervisor->exitMode = SUPERVISOR_EXIT_FATAL;
			supervisor->shutdownSequenceInProgress = true;

			(void) supervisor_stop_other_services(supervisor, service->pid);

			return false;
		}

		/* general case, log and continue to restart the service */
		log_level(logLevel, "pg_autoctl service %s exited with exit status %d",
				  service->name, returnCode);
	}
	else if (WIFSIGNALED(status))
	{
		int signal = WTERMSIG(status);

		log_level(logLevel,
				  "pg_autoctl service %s exited after receiving signal %s",
				  service->name, strsignal(signal));
	}
	else if (WIFSTOPPED(status))
	{
		/* well that's unexpected, we're not using WUNTRACED */
		log_level(logLevel,
				  "pg_autoctl service %s has been stopped and can be restarted",
				  service->name);
		return false;
	}

	/*
	 * We don't restart temporary processes at all: we're done already.
	 */
	if (service->policy == RP_TEMPORARY)
	{
		return true;
	}

	/*
	 * Check that we are allowed to restart: apply MaxR/MaxT as per the
	 * tracking we do in the counters ring buffer.
	 */
	if (supervisor_may_restart(service))
	{
		/* update our ring buffer: move our clock hand */
		int position = (counters->position + 1) % SUPERVISOR_SERVICE_MAX_RETRY;

		/* we have restarted once more */
		counters->count += 1;
		counters->position = position;
		counters->startTime[counters->position] = now;
	}
	else
	{
		/* exit with a non-zero exit code, and process with shutdown sequence */
		supervisor->exitMode = SUPERVISOR_EXIT_ERROR;
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
	if (service->policy == RP_TRANSIENT &&
		WIFEXITED(status) &&
		WEXITSTATUS(status) == EXIT_CODE_QUIT)
	{
		/* exit with a happy exit code, and process with shutdown sequence */
		supervisor->exitMode = SUPERVISOR_EXIT_CLEAN;
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
	log_info("Restarting service %s", service->name);
	bool restarted = (*service->startFunction)(service->context, &(service->pid));

	if (!restarted)
	{
		log_fatal("Failed to restart service %s", service->name);

		/* exit with a non-zero exit code, and process with shutdown sequence */
		supervisor->exitMode = SUPERVISOR_EXIT_ERROR;
		supervisor->shutdownSequenceInProgress = true;

		(void) supervisor_stop_other_services(supervisor, service->pid);

		return false;
	}

	/*
	 * Now we have restarted the service, it has a new PID and we need to
	 * update our PID file with the new information. Failing to update the PID
	 * file is a fatal error: the `pg_autoctl restart` command can't work then.
	 */
	if (!supervisor_update_pidfile(supervisor))
	{
		log_fatal("Failed to update pidfile \"%s\", stopping all services now",
				  supervisor->pidfile);

		supervisor->exitMode = SUPERVISOR_EXIT_ERROR;
		supervisor->shutdownSequenceInProgress = true;

		(void) supervisor_stop_subprocesses(supervisor);

		return false;
	}

	return true;
}


/*
 * supervisor_count_restarts returns true when we have restarted more than
 * SUPERVISOR_SERVICE_MAX_RETRY in the last SUPERVISOR_SERVICE_MAX_TIME period
 * of time.
 */
static bool
supervisor_may_restart(Service *service)
{
	uint64_t now = time(NULL);
	RestartCounters *counters = &(service->restartCounters);
	int position = counters->position;

	char timestring[BUFSIZE] = { 0 };

	log_debug("supervisor_may_restart: service \"%s\" restarted %d times, "
			  "most recently at %s, %d seconds ago",
			  service->name,
			  counters->count,
			  epoch_to_string(counters->startTime[position], timestring),
			  (int) (now - counters->startTime[position]));

	/* until we have restarted MaxR times, we know we can restart */
	if (counters->count <= SUPERVISOR_SERVICE_MAX_RETRY)
	{
		return true;
	}

	/*
	 * When we have restarted more than MaxR times, the only case when we can't
	 * restart again is if the oldest entry in the counters startTime array is
	 * older than our MaxT.
	 *
	 * The oldest entry in the ring buffer is the one just after the current
	 * one:
	 */
	position = (position + 1) % SUPERVISOR_SERVICE_MAX_RETRY;
	uint64_t oldestRestartTime = counters->startTime[position];

	if ((now - oldestRestartTime) <= SUPERVISOR_SERVICE_MAX_TIME)
	{
		log_fatal("pg_autoctl service %s has already been "
				  "restarted %d times in the last %d seconds, "
				  "stopping now",
				  service->name,
				  SUPERVISOR_SERVICE_MAX_RETRY,
				  (int) (now - oldestRestartTime));

		return false;
	}

	return true;
}


/*
 * supervisor_update_pidfile creates a pidfile with all our PIDs in there.
 */
static bool
supervisor_update_pidfile(Supervisor *supervisor)
{
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;
	PQExpBuffer content = createPQExpBuffer();


	if (content == NULL)
	{
		log_error("Failed to allocate memory to update our PID file");
		return false;
	}

	if (!prepare_pidfile_buffer(content, supervisor->pid))
	{
		/* errors have already been logged */
		destroyPQExpBuffer(content);
		return false;
	}

	/* now add a line per service  */
	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(supervisor->services[serviceIndex]);

		/* one line per service, pid space name */
		appendPQExpBuffer(content, "%d %s\n", service->pid, service->name);
	}

	bool success = write_file(content->data, content->len, supervisor->pidfile);
	destroyPQExpBuffer(content);

	return success;
}


/*
 * supervisor_find_service_pid reads the pidfile contents and process it line
 * by line to find the pid of the given service name.
 */
bool
supervisor_find_service_pid(const char *pidfile,
							const char *serviceName,
							pid_t *pid)
{
	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineNumber;

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	int lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		char *separator = NULL;

		/* skip first lines, see pidfile.h (where we count from 1) */
		if ((lineNumber + 1) < PIDFILE_LINE_FIRST_SERVICE)
		{
			continue;
		}

		if ((separator = strchr(fileLines[lineNumber], ' ')) == NULL)
		{
			log_error("Failed to find first space separator in line: \"%s\"",
					  fileLines[lineNumber]);
			continue;
		}

		if (streq(serviceName, separator + 1))
		{
			*separator = '\0';
			stringToInt(fileLines[lineNumber], pid);
			free(fileContents);
			return true;
		}
	}

	free(fileContents);

	return false;
}
