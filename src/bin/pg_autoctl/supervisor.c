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

/*
 * A static struct to loop seamlessly through the active services in a
 * supervisor struct. Those can be either 'static' or 'dynamic'.
 */
typedef struct Supervisor_it
{
	Supervisor *supervisor;
	Service *services;
	int count;
	int index;
} Supervisor_it;

static bool supervisor_init(Supervisor *supervisor);
static bool supervisor_loop(Supervisor *supervisor);

static bool supervisor_find_service(Supervisor *supervisor, pid_t pid,
									Service **result);

static void supervisor_dynamic_handle(Supervisor *supervisor, int *diffCount);

static bool supervisor_dynamic_remove_terminated_service(Supervisor *supervisor,
														 pid_t pid,
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

static void supervisor_handle_failed_restart(Supervisor *supervisor,
											 Service *service,
											 int status);

static bool supervisor_may_restart(Service *service);

static bool supervisor_update_pidfile(Supervisor *supervisor);


/*
 * supervisor_it_* implement a helper to loop through the active services in a
 * supervisor struct. They can be used either on their own, or via the helper
 * macro foreach_supervised_service().
 * Consult the macro for an example of use.
 */
static inline Service *
supervisor_it_get(Supervisor_it *sit)
{
	if (sit->index < sit->count)
	{
		return &sit->services[sit->index];
	}

	return NULL;
}


static inline Service *
supervisor_it_first(Supervisor_it *sit, Supervisor *supervisor)
{
	*sit = (Supervisor_it) {
		.supervisor = supervisor,
		.services = supervisor->services,
		.count = supervisor->serviceCount,
		.index = 0,
	};

	return supervisor_it_get(sit);
}


static inline Service *
supervisor_it_next(Supervisor_it *sit)
{
	sit->index++;

	/* if we are done with the static services, move to the dynamic */
	if (sit->index >= sit->count &&
		sit->services == sit->supervisor->services)
	{
		sit->services = sit->supervisor->dynamicServices.services;
		sit->count = sit->supervisor->dynamicServices.serviceCount;
		sit->index = 0;
	}

	return supervisor_it_get(sit);
}


#define foreach_supervised_service(it, supervisor, service) \
	for ((service) = supervisor_it_first((it), (supervisor)); \
		 (service) != NULL; \
		 (service) = supervisor_it_next(it))


/*
 * supervisor_dynamic_service_enable starts the service and if successfull, adds
 * it to the dynamicService array.
 *
 * Since the services are largely discoverable by name, the name has to be
 * unique. If it is not, then the service is not started and the function
 * returns false.
 * Also since the service runs under supervision, there must be space for it to
 * be added to the supervisor's dynamic services array. If there is not, then
 * the service is not started and the function returns false.
 * The service will not be added if a signal other than "dynamic service" has
 * been received.
 *
 * Returns true if successfully starts the service.
 */
bool
supervisor_dynamic_service_enable(Supervisor *supervisor, Service *service)
{
	Service *dynamicService;
	sigset_t sig_mask;
	sigset_t sig_mask_orig;
	int serviceIndex = supervisor->dynamicServices.serviceCount;
	bool started;

	if (supervisor_service_exists(supervisor, service->name))
	{
		log_error("Service %s already exists under supervision",
				  service->name);
		return false;
	}

	/* 0-based */
	if (serviceIndex >= (MAXDYNSERVICES - 1))
	{
		log_error("Reached maximum permitted number of dynamic services, "
				  "service %s is not added",
				  service->name);
		return false;
	}

	/*
	 * Block signals until the service has been added in the dynamic array
	 * unless failed to start. Failing to block signals, opens a race window.
	 */
	if (!block_signals(&sig_mask, &sig_mask_orig))
	{
		return false;
	}

	/*
	 * In case there is an active signal to stop, set prior to blocking signals,
	 * do not add the service. Our caller should be able to handle this case.
	 */
	if (asked_to_stop || asked_to_stop_fast || asked_to_reload || asked_to_quit)
	{
		/* restore signal masks (un block them) now */
		(void) unblock_signals(&sig_mask_orig);
		return false;
	}

	started = (*service->startFunction)(service->context, &(service->pid));

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
		log_error("Failed to start pg_autoctl %s service",
				  service->name);
		(void) unblock_signals(&sig_mask_orig);
		return false;
	}

	/*
	 * IGNORE-BANNED fits here because we want to take ownership of the service.
	 * We copy to an address that we own from a non overlapping address.
	 */
	dynamicService = &supervisor->dynamicServices.services[serviceIndex];
	memcpy(dynamicService, service, sizeof(*service)); /* IGNORE-BANNED */
	supervisor->dynamicServices.serviceCount++;

	(void) unblock_signals(&sig_mask_orig);

	return true;
}


/*
 * supervisor_dynamic_service_disable stops the running dynamic service with
 * serviceName.
 * Once the service is stopped, it is also removed from the dynamicService array
 * and supervisor_loop can largely forget about it.
 *
 * However, it will get notified during waitpid, so we add this service to the
 * recentlyRemoved array and we will hold it there until it is requested via
 * supervisor_dynamic_remove_terminated_service() which subsequently removes it.
 *
 * Returns true if the service is stopped and removed from the dynamicService
 * array, false otherwise.
 */
bool
supervisor_dynamic_service_disable(Supervisor *supervisor,
								   const char *serviceName)
{
	Service *dynamicService = NULL;
	int serviceCount = supervisor->dynamicServices.serviceCount;
	int serviceIndex;

	/* Find it in the dynamic array */
	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &supervisor->dynamicServices.services[serviceIndex];

		if (!strncmp(service->name, serviceName, strlen(serviceName)))
		{
			dynamicService = service;
			break;
		}
	}

	/* It was not found, nothing to do. */
	if (!dynamicService)
	{
		return false;
	}

	/*
	 * Since we are going to signal the service, we have to make certain that we
	 * will not error out in the supervisor_loop on the waitpid() response. So
	 * after killing the service and removing it from the dynamicService array,
	 * we add it to the recentlyRemoved array to find it again later.
	 */
	if (!kill(dynamicService->pid, SIGTERM))
	{
		log_info("Service %s with %d did not receive signal",
				 dynamicService->name, dynamicService->pid);
	}

	log_info("Stopped pg_autoctl %s service with pid %d",
			 dynamicService->name, dynamicService->pid);

	/*
	 * Remove the dynamic Service from the array by swapping it out with the
	 * last element in the array and bringing the array count down by one
	 */
	supervisor->dynamicServices.services[serviceIndex] =
		supervisor->dynamicServices.services[serviceCount - 1];
	supervisor->dynamicServices.serviceCount--;

	/*
	 * Finally add the dynamic service to the recently removed array.
	 *
	 * If we reached the end of this array, start again at the beginning. This
	 * is a loosely held array and we do not fuss too much.
	 */
	if (supervisor->dynamicRecentlyRemoved.serviceCount == MAXDYNSERVICES)
	{
		supervisor->dynamicRecentlyRemoved.serviceCount = 0;
	}

	/*
	 * IGNORE-BANNED fits here because we want to take ownership of the service.
	 * We copy to an address that we own from a non overlapping address.
	 */
	memcpy(&supervisor->dynamicRecentlyRemoved.services[    /* IGNORE-BANNED */
			   supervisor->dynamicRecentlyRemoved.serviceCount],
		   dynamicService,
		   sizeof(*dynamicService));
	supervisor->dynamicRecentlyRemoved.serviceCount++;

	return true;
}


/*
 * supervisor_start starts given services as sub-processes and then supervise
 * them.
 */
bool
supervisor_start(Service services[], int serviceCount, const char *pidfile,
				 void (*dynamicHandler)(Supervisor *, void *, int *),
				 void *dynamicHandlerArg)
{
	int serviceIndex = 0;
	bool success = true;

	Supervisor supervisor = {
		.services = services,
		.serviceCount = serviceCount,
		.pidfile = { 0 },
		.pid = -1,
		.dynamicServices = { 0 },
		.dynamicRecentlyRemoved = { 0 },
		.dynamicHandler = dynamicHandler,
		.dynamicHandlerArg = dynamicHandlerArg,
	};


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

		supervisor.cleanExit = false;
		supervisor.shutdownSequenceInProgress = true;

		(void) supervisor_stop_subprocesses(&supervisor);

		return false;
	}

	/*
	 * Let the dynamic handler decide about dynamic services
	 */
	if (dynamicHandler)
	{
		/*
		 * Here we do not need to track how many dynamic services were added,
		 * supervisor_loop() will keep track of them.
		 */
		int ignoreDiff;
		dynamicHandler(&supervisor, dynamicHandlerArg, &ignoreDiff);
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
	bool firstLoop = true;
	int subprocessCount = supervisor->serviceCount +
						  supervisor->dynamicServices.serviceCount;

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

		if (asked_to_handle_dynamic)
		{
			int diffCount = 0;
			log_info("pg_autoctl received a signal to, "
					 "handle dynamic services");

			supervisor_dynamic_handle(supervisor, &diffCount);
			if (diffCount != 0)
			{
				/*
				 * We do not know if services where started or stopped. If
				 * services where stopped we need to substract the number of
				 * toggled from the running one. In that case we have to be
				 * certain that we do not substract more than the subprocesses
				 * already running. IF we do, then this is a dev error and we
				 * should fail.
				 */
				if ((subprocessCount + diffCount) < 0)
				{
					log_fatal("BUG: dev error, toggled off more subprocess than"
							  " running at the moment");

					supervisor->cleanExit = false;
					supervisor->shutdownSequenceInProgress = true;
					supervisor_stop_subprocesses(supervisor);
					break;
				}
				subprocessCount += diffCount;
			}
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
					if (supervisor->shutdownSequenceInProgress)
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

				/* if this is a dynamic service which just exited, do nothing */
				if (supervisor_dynamic_remove_terminated_service(supervisor,
																 pid,
																&dead))
				{
					log_info("Removed service %s exited", dead->name);
					break;
				}

				/* map the dead child pid to the known dead internal service */
				if (!supervisor_find_service(supervisor, pid, &dead))
				{
					log_error("Unknown subprocess died with pid %d", pid);
					break;
				}

				/* one child process is no more */
				--subprocessCount;

				/* apply the service restart policy */
				if (!supervisor_restart_service(supervisor, dead, status))
				{
					supervisor_handle_failed_restart(supervisor, dead, status);
					break;
				}

				++subprocessCount;
				break;
			}
		}
	}

	/* we track in the main loop if it's a cleanExit or not */
	return supervisor->cleanExit;
}


/*
 * supervisor_dynamic_remove_terminated_service is responsible for removing
 * a terminated dynamic service from the dynamicRecentlyRemoved array.
 * If the service is found, it is recorded in the **result argument.
 *
 * Returns true if the service is removed, false otherwise.
 */
static bool
supervisor_dynamic_remove_terminated_service(Supervisor *supervisor, pid_t pid,
											 Service **result)
{
	Service *recentlyRemoved = supervisor->dynamicRecentlyRemoved.services;
	int serviceCount = supervisor->dynamicRecentlyRemoved.serviceCount;
	int serviceIndex;

	for (serviceIndex = 0; serviceIndex < serviceCount; serviceIndex++)
	{
		Service *service = &recentlyRemoved[serviceIndex];
		if (service->pid == pid)
		{
			*result = service;

			/* Now that the service is found, remove it from the array */
			recentlyRemoved[serviceIndex] = recentlyRemoved[serviceCount - 1];
			supervisor->dynamicRecentlyRemoved.serviceCount--;
			return true;
		}
	}

	return false;
}


/*
 * supervisor_find_service loops over the SubProcess array to find given pid and
 * return its entry in the array.
 */
static bool
supervisor_find_service(Supervisor *supervisor, pid_t pid, Service **result)
{
	Supervisor_it sit;
	Service *service;

	foreach_supervised_service(&sit, supervisor, service)
	{
		if (pid == service->pid)
		{
			*result = service;
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
	Supervisor_it sit;
	Service *service;

	foreach_supervised_service(&sit, supervisor, service)
	{
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
	Supervisor_it sit;
	Service *service;
	int signal = get_current_signal(SIGTERM);

	foreach_supervised_service(&sit, supervisor, service)
	{
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
	Supervisor_it sit;
	Service *service;
	int signal = get_current_signal(SIGTERM);

	/*
	 * In case of unexpected stop (bug), we stop the other processes too.
	 * Someone might then notice (such as systemd) and restart the whole
	 * thing again.
	 */
	if (!(asked_to_stop || asked_to_stop_fast))
	{
		foreach_supervised_service(&sit, supervisor, service)
		{
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
		supervisor->cleanExit = true;
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
 * supervisor_dynamic_handle calls the dynamic handler if exits. It is the
 * handler's responsibility to add or remove dynamic services using the provided
 * api. The last argument, passed from our caller down to the dynamicHandler
 * function, will contain the net difference of succefully added and removed
 * services.
 *
 * Returns void.
 */
static void
supervisor_dynamic_handle(Supervisor *supervisor, int *diffCount)
{
	/*
	 * Call the dynamic function if defined. The last argument, diffCount
	 * contains the of count of services affected, e.g. 1 started => 1,
	 * 1 stopped => -1, 2 started and 3 stopped => -1 etc
	 */
	if (supervisor->dynamicHandler)
	{
		supervisor->dynamicHandler(supervisor, supervisor->dynamicHandlerArg,
								   diffCount);
	}

	asked_to_handle_dynamic = 0;
}


/*
 * supervisor_handle_failed_restart decides what action to take when a service
 * has failed to restart. Currently one of the three can happen:
 *	* continue running the rest of the services
 *	* exit the whole program with a happy code
 *	* exit the whole program with failed code
 *
 * The action is depended on whether the service is held in the dynamic array.
 *
 * The function can be greatly simplified. It is intentionally left a bit
 * verbose for the benefit of readability.
 */
static void
supervisor_handle_failed_restart(Supervisor *supervisor, Service *service,
								 int status)
{
	int returnCode = WEXITSTATUS(status);
	bool isDynamic = false;


	/*
	 * If we are already going handling a shutdown, then do not update it
	 */
	if (supervisor->shutdownSequenceInProgress)
	{
		log_trace("supervisor_handle_failed_restart: shutdownSequenceInProgress");
		return;
	}

	/*
	 * Loop through the dynamic service array and if it found, then mark it
	 */
	for (int serviceIndex = 0;
		 serviceIndex < supervisor->dynamicServices.serviceCount;
		 serviceIndex++)
	{
		Service *dynamic = &supervisor->dynamicServices.services[serviceIndex];

		if (!strncmp(dynamic->name, service->name, strlen(service->name)))
		{
			isDynamic = true;
			break;
		}
	}

	/*
	 * No need to act, the service should not have restarted.
	 *
	 * If a dynamic service, then ask its handler to disable it.
	 */
	if (service->policy == RP_TEMPORARY)
	{
		if (isDynamic)
		{
			(void) supervisor_dynamic_service_disable(supervisor,
													  service->name);
		}
		return;
	}

	/*
	 * If a transient service and exited normally then we are happy. If it was
	 * a dyanmic service, then remove it but continue running the rest of the
	 * services, otherwise, shutdown all the services with a happy code.
	 */
	if (service->policy == RP_TRANSIENT && returnCode == EXIT_CODE_QUIT)
	{
		if (isDynamic)
		{
			(void) supervisor_dynamic_service_disable(supervisor,
													  service->name);
		}
		else
		{
			supervisor->cleanExit = true;
			supervisor->shutdownSequenceInProgress = true;
			(void) supervisor_stop_other_services(supervisor, service->pid);
		}
		return;
	}

	/*
	 * There are no more happy exit code scenarios. Either disable the failed
	 * service or shutdown everything.
	 */
	if (isDynamic)
	{
		log_error("Failed to restart service %s, disabling it",
				  service->name);
		(void) supervisor_dynamic_service_disable(supervisor, service->name);
	}
	else
	{
		/* exit with a non-zero exit code, and process with shutdown sequence */
		supervisor->cleanExit = false;
		supervisor->shutdownSequenceInProgress = true;
		(void) supervisor_stop_other_services(supervisor, service->pid);
	}
}


/*
 * supervisor_restart_service restarts given service and maintains its MaxR and
 * MaxT counters.
 *
 * Returns true when the service has successfully restarted or false.
 */
static bool
supervisor_restart_service(Supervisor *supervisor, Service *service, int status)
{
	char *verb = WIFEXITED(status) ? "exited" : "failed";
	int returnCode = WEXITSTATUS(status);
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
	else if (returnCode == EXIT_CODE_QUIT)
	{
		logLevel = LOG_WARN;
	}

	log_level(logLevel, "pg_autoctl service %s %s with exit status %d",
			  service->name, verb, returnCode);

	/*
	 * We don't restart temporary processes at all: we're done already.
	 */
	if (service->policy == RP_TEMPORARY)
	{
		return false;
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

		return false;
	}

	/*
	 * Now we have restarted the service, it has a new PID and we need to
	 * update our PID file with the new information. Failing to update the PID
	 * file is a fatal error: the `pg_autoctl restart` command can't work then.
	 *
	 * This is the only case where we decide to start a shutdown sequence since
	 * this is unrecoverabled error independed of the service.
	 */
	if (!supervisor_update_pidfile(supervisor))
	{
		log_fatal("Failed to update pidfile \"%s\", stopping all services now",
				  supervisor->pidfile);

		supervisor->cleanExit = false;
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
	Supervisor_it sit;
	Service *service;
	PQExpBuffer content = createPQExpBuffer();


	if (content == NULL)
	{
		log_error("Failed to allocate memory to update our PID file");
		return false;
	}

	if (!prepare_pidfile_buffer(content, supervisor->pid))
	{
		/* errors have already been logged */
		return false;
	}

	/* now add a line per service  */

	foreach_supervised_service(&sit, supervisor, service)
	{
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


bool
supervisor_service_exists(Supervisor *supervisor, const char *serviceName)
{
	Supervisor_it sit;
	Service *service;

	foreach_supervised_service(&sit, supervisor, service)
	{
		if (!strncmp(service->name, serviceName, strlen(serviceName)))
		{
			return true;
		}
	}

	return false;
}
