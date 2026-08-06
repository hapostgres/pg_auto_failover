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
#include "nodespec.h"
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

static bool supervisor_have_keeper_service(Supervisor *supervisor);

static int supervisor_stuck_threshold_loops(Supervisor *supervisor);

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

static bool supervisor_wait_for_exit(pid_t pid, int maxWaitMs);


/*
 * supervisor_start starts given services as sub-processes and then supervise
 * them.
 */
bool
supervisor_start(Service services[], int serviceCount, const char *pidfile)
{
	return supervisor_start_with_callback(services, serviceCount, pidfile,
										  NULL, NULL);
}


/*
 * supervisor_start_with_callback is supervisor_start()'s full
 * implementation, with an optional periodic callback -- see
 * Supervisor.periodicCallback's own comment (supervisor.h) for what it's
 * for and the constraints it comes with. supervisor_start() itself is a
 * thin wrapper passing NULL/NULL, so every existing caller is unaffected
 * by this function's existence.
 */
bool
supervisor_start_with_callback(Service services[], int serviceCount,
							   const char *pidfile,
							   void (*periodicCallback)(Supervisor *supervisor,
														void *context),
							   void *periodicCallbackContext)
{
	int serviceIndex = 0;
	bool success = true;

	Supervisor supervisor = { services, serviceCount, { 0 }, -1 };

	supervisor.periodicCallback = periodicCallback;
	supervisor.periodicCallbackContext = periodicCallbackContext;
	supervisor.pendingSubprocessCount = serviceCount;

	/* copy the pidfile over to our supervisor structure */
	strlcpy(supervisor.pidfile, pidfile, MAXPGPATH);

	/*
	 * If we were started by `pg_autoctl node run`, the node spec path is
	 * passed via the PG_AUTOCTL_NODESPEC env var.  Set up the file watcher
	 * so the supervisor can converge mutable settings when the file changes.
	 */
	{
		char specPath[MAXPGPATH] = { 0 };

		if (env_exists("PG_AUTOCTL_NODESPEC") &&
			get_env_copy("PG_AUTOCTL_NODESPEC", specPath, sizeof(specPath)) &&
			!IS_EMPTY_STRING_BUFFER(specPath))
		{
			if (nodespec_read(specPath, &supervisor.watchedSpec) &&
				nodespec_watcher_init(&supervisor.watcher, specPath))
			{
				log_info("Supervisor: watching node spec \"%s\"", specPath);
			}
			else
			{
				log_warn("Supervisor: failed to initialise node spec watcher "
						 "for \"%s\"; changes to the file will be ignored",
						 specPath);
			}
		}
	}

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
	bool firstLoop = true;

	/* wait until all subprocesses are done */
	while (supervisor->pendingSubprocessCount > 0)
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

			/*
			 * Check if the node spec file has changed and apply mutable
			 * settings if so.  Uses inotify on Linux, mtime poll elsewhere.
			 * No-op when watcher.active is false (normal run path).
			 */
			(void) nodespec_watcher_check(&supervisor->watcher,
										  &supervisor->watchedSpec);

			/*
			 * Optional caller-supplied periodic callback -- see
			 * Supervisor.periodicCallback's own comment (supervisor.h).
			 * A no-op for every caller except supervisor_start_with_
			 * callback()'s own explicit users.
			 */
			if (supervisor->periodicCallback != NULL)
			{
				(void) supervisor->periodicCallback(
					supervisor, supervisor->periodicCallbackContext);
			}
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
					/*
					 * When running as PID 1 (inside a container), orphaned
					 * grandchildren are reparented to us by the kernel and
					 * we will reap them here.  This is expected behaviour;
					 * log at INFO rather than ERROR so it doesn't look like
					 * a bug.
					 */
					if (getpid() == 1)
					{
						log_info("Reaped orphaned subprocess with pid %d "
								 "(reparented to PID 1)", pid);
					}
					else
					{
						log_error("Unknown subprocess died with pid %d", pid);
					}
					break;
				}

				/* one child process is no more */
				--supervisor->pendingSubprocessCount;

				/* apply the service restart policy */
				if (supervisor_restart_service(supervisor, dead, status))
				{
					++supervisor->pendingSubprocessCount;
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
 *
 * A plain SIGTERM is forwarded to the node-active (keeper) service only,
 * when one is present in this supervisor's service list. This lets the
 * keeper drive a graceful shutdown through the ordinary FSM transitions
 * (routing a primary through maintenance, see fsm_stop_postgres_for_
 * primary_maintenance) using the existing keeper/postgres-controller
 * state-file protocol to decide when Postgres actually stops, rather than
 * the Postgres controller reacting to its own independently-delivered
 * SIGTERM and racing the keeper's FSM-driven shutdown.
 *
 * SIGINT/SIGQUIT -- whether received directly (e.g. Docker/Kubernetes/
 * systemd escalating to a harder signal after a grace period) or via our
 * own internal escalation in supervisor_shutdown_sequence -- mean "stop
 * now, no graceful handoff", and are forwarded to every service exactly as
 * before.
 *
 * Supervisors with no service named SERVICE_NAME_KEEPER (the monitor's
 * services, or the keeper before `--run` has handed control to the
 * node-active service) have no FSM handoff to drive either way, so this
 * signal-restriction does not apply to them: every service is signalled,
 * same as for SIGINT/SIGQUIT.
 *
 * Once the keeper service has actually exited (supervisor->keeperExited),
 * there is no FSM handoff left to protect: this function is called again
 * from supervisor_restart_service() at that point specifically to cascade
 * the signal to the remaining services right away, rather than waiting on
 * supervisor_shutdown_sequence()'s stuck-process timer.
 *
 * That cascade call happens outside of direct signal reception, so it can't
 * rely on get_current_signal(): the asked_to_stop/asked_to_stop_fast/
 * asked_to_quit flags it reads are reset right after being processed (see
 * supervisor_handle_signals()), so by the time the cascade runs they no
 * longer reflect the signal that actually started this shutdown -- it would
 * silently fall back to the SIGTERM default and downgrade, say, a SIGQUIT-
 * driven shutdown. supervisor->shutdownSignal is the sticky, escalation-
 * aware value that survives that reset, so prefer it whenever a shutdown is
 * genuinely already in flight (it stays 0, its zero-initialized value, for
 * the one synthetic caller that isn't -- the pidfile-write-failure path in
 * supervisor_start() -- which is exactly when falling back to
 * get_current_signal()'s SIGTERM default is still correct).
 */
static void
supervisor_stop_subprocesses(Supervisor *supervisor)
{
	int signal = supervisor->shutdownSignal != 0
				 ? supervisor->shutdownSignal
				 : get_current_signal(SIGTERM);
	int serviceCount = supervisor->serviceCount;
	int serviceIndex = 0;

	bool keeperOnly = signal == SIGTERM &&
					  !supervisor->keeperExited &&
					  supervisor_have_keeper_service(supervisor);

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &(supervisor->services[serviceIndex]);

		if (keeperOnly && strcmp(service->name, SERVICE_NAME_KEEPER) != 0)
		{
			continue;
		}

		if (kill(service->pid, signal) != 0)
		{
			log_error("Failed to send signal %s to service %s with pid %d",
					  strsignal(signal), service->name, service->pid);
		}
	}
}


/*
 * supervisor_have_keeper_service returns true when this supervisor has a
 * service named SERVICE_NAME_KEEPER registered, regardless of whether it is
 * still running.
 */
static bool
supervisor_have_keeper_service(Supervisor *supervisor)
{
	int serviceIndex = 0;

	for (serviceIndex = 0; serviceIndex < supervisor->serviceCount; serviceIndex++)
	{
		if (strcmp(supervisor->services[serviceIndex].name,
				   SERVICE_NAME_KEEPER) == 0)
		{
			return true;
		}
	}

	return false;
}


/*
 * supervisor_stuck_threshold_loops computes the stoppingLoopCounter value at
 * which supervisor_shutdown_sequence() should stop waiting and escalate to
 * the whole process group.
 *
 * When a plain SIGTERM shutdown is relying on the keeper alone to drive a
 * graceful maintenance handoff (see supervisor_stop_subprocesses()), that
 * handoff has its own grace period of up to KEEPER_GRACEFUL_SHUTDOWN_MAX_SECS
 * (see defaults.h): escalating sooner would signal the Postgres controller
 * directly and race the very FSM-driven handoff this is all for. A short
 * margin is added on top for the reporting/logging done in between. In every
 * other case (no keeper service, or the signal has already been escalated to
 * SIGINT/SIGQUIT), fall back to the original, much shorter threshold.
 */
static int
supervisor_stuck_threshold_loops(Supervisor *supervisor)
{
	bool keeperGraceActive = supervisor->shutdownSignal == SIGTERM &&
							 supervisor_have_keeper_service(supervisor);

	if (keeperGraceActive)
	{
		return ((KEEPER_GRACEFUL_SHUTDOWN_MAX_SECS + 5) * 1000) / 100;
	}

	return 50;
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

		/*
		 * Freeze the stuck-process threshold now, based on the shutdown
		 * signal we're starting from. It must not be recomputed on every
		 * loop: shutdownSignal itself gets escalated later on in
		 * supervisor_shutdown_sequence() as part of applying that very
		 * threshold, which would otherwise make it a moving target.
		 */
		supervisor->stuckThresholdLoops =
			supervisor_stuck_threshold_loops(supervisor);
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
 * At stuckThresholdLoops loops (typically we add a 100ms wait per loop, so
 * that's 50 loops / 5s in the ordinary case, see supervisor_stuck_threshold_
 * loops()), send either SIGTERM or SIGINT.
 *
 * At every 100 loops after that, send SIGINT.
 */
static void
supervisor_shutdown_sequence(Supervisor *supervisor)
{
	int stuckThresholdLoops = supervisor->stuckThresholdLoops;

	if (supervisor->stoppingLoopCounter == 1)
	{
		log_info("Waiting for subprocesses to terminate.");
	}

	/*
	 * If we've been waiting for quite a while for sub-processes to terminate.
	 * Let's signal again all our process group ourselves and see what happens
	 * next.
	 */
	if (supervisor->stoppingLoopCounter == stuckThresholdLoops)
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
	if (supervisor->stoppingLoopCounter > stuckThresholdLoops &&
		(supervisor->stoppingLoopCounter - stuckThresholdLoops) % 100 == 0)
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

		/*
		 * A plain SIGTERM only ever signalled the keeper service directly
		 * (see supervisor_stop_subprocesses()), so that it could drive a
		 * graceful maintenance handoff without racing the Postgres
		 * controller. Now that the keeper itself has exited -- one way or
		 * another, its own graceful shutdown is done -- there is no handoff
		 * left to protect, so cascade the signal to whichever other
		 * services are still running right away, rather than waiting on
		 * the stuck-process timer in supervisor_shutdown_sequence().
		 *
		 * Only do that cascade when the shutdown actually started as a
		 * keeper-only SIGTERM: for SIGINT/SIGQUIT, supervisor_stop_
		 * subprocesses() already signalled every service directly up front
		 * (keeperOnly never applied), so the Postgres controller already
		 * has its signal -- re-sending it here would deliver a second,
		 * redundant signal while it may still be mid-shutdown (e.g.
		 * stopping Postgres gracefully in reaction to the first one),
		 * which is exactly the kind of double-delivery this code must not
		 * cause.
		 */
		if (strcmp(service->name, SERVICE_NAME_KEEPER) == 0)
		{
			supervisor->keeperExited = true;

			if (supervisor->shutdownSignal == SIGTERM)
			{
				(void) supervisor_stop_subprocesses(supervisor);
			}
		}

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

	/*
	 * Write atomically via a temp file + rename so that concurrent readers of
	 * the pidfile never see a truncated (empty) file during the update.
	 * POSIX rename(2) is atomic: a reader sees either the old complete file
	 * or the new complete one, never a partially-written version.
	 */
	char tmpfile[MAXPGPATH];
	sformat(tmpfile, MAXPGPATH, "%s.tmp", supervisor->pidfile);

	bool success = write_file(content->data, content->len, tmpfile);
	destroyPQExpBuffer(content);

	if (success && rename(tmpfile, supervisor->pidfile) != 0)
	{
		log_error("Failed to rename \"%s\" to \"%s\": %m", tmpfile,
				  supervisor->pidfile);
		(void) unlink(tmpfile);
		success = false;
	}

	return success;
}


/*
 * supervisor_wait_for_exit waits, up to maxWaitMs, for pid to actually be
 * reaped (waitpid(WNOHANG) returning that exact pid, or ECHILD meaning it
 * was already reaped elsewhere). Polls every 10ms; returns true as soon
 * as the child is gone, false if it's still around once the deadline is
 * reached.
 */
static bool
supervisor_wait_for_exit(pid_t pid, int maxWaitMs)
{
	int elapsedMs = 0;

	while (elapsedMs < maxWaitMs)
	{
		int status = 0;
		pid_t reaped = waitpid(pid, &status, WNOHANG);

		if (reaped == pid)
		{
			return true;
		}

		if (reaped == -1 && errno == ECHILD)
		{
			/* already reaped elsewhere -- fine, treat as done */
			return true;
		}

		pg_usleep(10 * 1000);
		elapsedMs += 10;
	}

	return false;
}


/*
 * supervisor_add_service adds a new service to an already-running
 * supervisor, starts it, and updates the pidfile to include it.
 *
 * Requires supervisor->services to be a heap-allocated array -- see
 * Supervisor.periodicCallback's own comment (supervisor.h) for why: this
 * function reallocs it to grow by one slot. Only ever safe to call from
 * a supervisor started via supervisor_start_with_callback() with its own
 * heap-allocated initial array, never from a plain supervisor_start()
 * caller's stack/static one.
 */
bool
supervisor_add_service(Supervisor *supervisor, Service service)
{
	int newCount = supervisor->serviceCount + 1;
	Service *grown = realloc(supervisor->services, newCount * sizeof(Service));

	if (grown == NULL)
	{
		log_error("Failed to allocate memory to add service \"%s\"",
				  service.name);
		return false;
	}

	supervisor->services = grown;
	supervisor->services[supervisor->serviceCount] = service;

	Service *added = &(supervisor->services[supervisor->serviceCount]);

	log_debug("Starting pg_autoctl %s service", added->name);

	if (!(*added->startFunction)(added->context, &(added->pid)))
	{
		log_error("Failed to start service \"%s\"", added->name);

		/* undo the growth -- this slot never became real */
		Service *shrunk = realloc(supervisor->services,
								  supervisor->serviceCount * sizeof(Service));

		if (shrunk != NULL)
		{
			supervisor->services = shrunk;
		}

		return false;
	}

	uint64_t now = time(NULL);
	RestartCounters *counters = &(added->restartCounters);

	counters->count = 1;
	counters->position = 0;
	counters->startTime[counters->position] = now;

	log_info("Started pg_autoctl %s service with pid %d",
			 added->name, added->pid);

	supervisor->serviceCount = newCount;
	supervisor->pendingSubprocessCount++;

	if (!supervisor_update_pidfile(supervisor))
	{
		log_error("Failed to update pidfile \"%s\" after adding service \"%s\"",
				  supervisor->pidfile, added->name);
		return false;
	}

	return true;
}


/*
 * supervisor_remove_service stops a currently-supervised service (found
 * by pid) and removes it from the supervisor's own array, so it is no
 * longer restarted on exit and no longer written to the pidfile.
 *
 * Sends `signal` (typically SIGTERM) and waits, briefly and boundedly,
 * for the child to actually exit -- reaping it synchronously here rather
 * than via supervisor_loop()'s own waitpid(WNOHANG) path, so the caller
 * knows the removal is complete (and the slot genuinely reusable) by the
 * time this returns, instead of racing the main loop's next iteration.
 * A child still stuck after the wait is removed from supervision anyway
 * (logged as a warning): whatever asked for this removal -- typically a
 * membership that no longer exists -- has already decided this process
 * shouldn't be tracked, stuck or not.
 *
 * Requires supervisor->services to be heap-allocated, same as
 * supervisor_add_service() above.
 */
bool
supervisor_remove_service(Supervisor *supervisor, pid_t pid, int signal)
{
	Service *found = NULL;

	if (!supervisor_find_service(supervisor, pid, &found))
	{
		log_error("Failed to remove service with pid %d: not found", pid);
		return false;
	}

	char name[NAMEDATALEN] = { 0 };

	strlcpy(name, found->name, NAMEDATALEN);
	int foundIndex = found - supervisor->services;

	if (kill(pid, signal) != 0 && errno != ESRCH)
	{
		log_error("Failed to send signal %s to service \"%s\" with pid %d: %m",
				  strsignal(signal), name, pid);
		return false;
	}

	if (!supervisor_wait_for_exit(pid, SUPERVISOR_REMOVE_SERVICE_MAX_WAIT_MS))
	{
		log_warn("Service \"%s\" (pid %d) did not exit within %d ms of "
				 "signal %s; removing it from supervision anyway",
				 name, pid, SUPERVISOR_REMOVE_SERVICE_MAX_WAIT_MS,
				 strsignal(signal));
	}

	/* close the gap in the array, keeping it packed */
	for (int i = foundIndex; i < supervisor->serviceCount - 1; i++)
	{
		supervisor->services[i] = supervisor->services[i + 1];
	}

	supervisor->serviceCount--;
	supervisor->pendingSubprocessCount--;

	if (supervisor->serviceCount > 0)
	{
		Service *shrunk = realloc(supervisor->services,
								  supervisor->serviceCount * sizeof(Service));

		if (shrunk != NULL)
		{
			supervisor->services = shrunk;
		}

		/*
		 * A failed shrink-realloc is harmless: the buffer is still valid
		 * and still holds every remaining service correctly, just larger
		 * than strictly needed -- keep using it as-is rather than fail
		 * the whole removal over it.
		 */
	}

	log_info("Removed service \"%s\" (was pid %d) from supervision", name, pid);

	if (!supervisor_update_pidfile(supervisor))
	{
		log_error("Failed to update pidfile \"%s\" after removing service \"%s\"",
				  supervisor->pidfile, name);
		return false;
	}

	return true;
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
