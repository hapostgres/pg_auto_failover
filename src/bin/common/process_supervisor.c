/*
 * src/bin/common/process_supervisor.c
 *   See process_supervisor.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "process_supervisor.h"

#include "log.h"

static bool process_supervisor_find_service(ProcessSupervisor *supervisor,
											pid_t pid, ProcessService **result);


/* binds services[0..serviceCount) to supervisor; does not start them yet */
void
process_supervisor_init(ProcessSupervisor *supervisor,
						ProcessService *services, int serviceCount)
{
	supervisor->services = services;
	supervisor->serviceCount = serviceCount;

	for (int i = 0; i < serviceCount; i++)
	{
		services[i].pid = -1;
		services[i].gaveUp = false;
		memset(&(services[i].restartCounters), 0,
			   sizeof(ProcessRestartCounters));
	}
}


/*
 * process_supervisor_start_all calls every service's own startFunction()
 * once, in order, stopping and returning false at the first failure (with
 * every service started so far left running -- the caller decides what to
 * do next, exactly like ws_receivewal_start_all()'s own per-cluster "log and
 * skip this one" callers already do for a single misconfigured cluster).
 */
bool
process_supervisor_start_all(ProcessSupervisor *supervisor)
{
	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		ProcessService *service = &(supervisor->services[i]);

		if (!(*service->startFunction)(service->context, &(service->pid)))
		{
			log_error("Failed to start service \"%s\"", service->name);
			return false;
		}

		process_restart_counters_start(&(service->restartCounters),
									   (uint64_t) time(NULL));
	}

	return true;
}


/*
 * process_supervisor_find_service finds supervisor's own service whose
 * pid matches, if any -- a plain linear scan, never called on a hot
 * path (once per exited child, in process_supervisor_tick() below, not
 * once per tick).
 */
static bool
process_supervisor_find_service(ProcessSupervisor *supervisor, pid_t pid,
								ProcessService **result)
{
	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		if (supervisor->services[i].pid == pid)
		{
			*result = &(supervisor->services[i]);
			return true;
		}
	}

	return false;
}


/*
 * process_restart_counters_start, process_restart_counters_record and
 * process_restart_counters_may_restart are the shared MaxR/MaxT
 * restart-backoff ring buffer: pg_autoctl/supervisor.c calls these same
 * three functions directly for its own Service/RestartCounters, rather
 * than keeping an independently-maintained copy of the same algorithm.
 *
 * process_restart_counters_start records a service's very first start
 * (count = 1, position = 0, startTime[0] = now). Both this file's own
 * process_supervisor_start_all() and pg_autoctl's supervisor_start() call
 * this for every service they start.
 */
void
process_restart_counters_start(ProcessRestartCounters *counters, uint64_t now)
{
	counters->count = 1;
	counters->position = 0;
	counters->startTime[0] = now;
}


/*
 * process_restart_counters_record advances the ring buffer to record one
 * more restart at time now. Call this only after
 * process_restart_counters_may_restart() has returned true for the same
 * counters.
 */
void
process_restart_counters_record(ProcessRestartCounters *counters, uint64_t now)
{
	int position = (counters->position + 1) % PROCESS_SUPERVISOR_MAX_RETRY;

	counters->count += 1;
	counters->position = position;
	counters->startTime[position] = now;
}


/*
 * process_restart_counters_may_restart applies the MaxR/MaxT policy:
 * returns true when another restart is allowed, false when this service
 * has already been restarted PROCESS_SUPERVISOR_MAX_RETRY times within
 * the last PROCESS_SUPERVISOR_MAX_TIME seconds. Purely a computation on
 * counters: it does not log anything itself, so that each caller can keep
 * its own wording (and log level) for "giving up" -- pg_autoctl's own
 * supervisor.c and this file's own process_supervisor_restart_service()
 * each log a different message on a false result.
 */
bool
process_restart_counters_may_restart(ProcessRestartCounters *counters)
{
	uint64_t now = (uint64_t) time(NULL);
	int position = counters->position;

	if (counters->count <= PROCESS_SUPERVISOR_MAX_RETRY)
	{
		return true;
	}

	position = (position + 1) % PROCESS_SUPERVISOR_MAX_RETRY;

	uint64_t oldestRestartTime = counters->startTime[position];

	return (now - oldestRestartTime) > PROCESS_SUPERVISOR_MAX_TIME;
}


/*
 * process_supervisor_log_unknown_pid classifies and logs a dead child
 * pid that matches none of a supervisor's own known services: when
 * running as PID 1 inside a container, the kernel reparents orphaned
 * grandchildren to us and this is expected behaviour (logged at INFO);
 * otherwise it is logged at ERROR, since some subprocess-tracking
 * bookkeeping is missing a case.
 */
void
process_supervisor_log_unknown_pid(pid_t pid)
{
	if (getpid() == 1)
	{
		log_info("Reaped orphaned subprocess with pid %d "
				 "(reparented to PID 1)", pid);
	}
	else
	{
		log_error("Unknown subprocess died with pid %d", pid);
	}
}


/*
 * process_supervisor_restart_service reaps and, per policy, restarts one
 * service whose child just exited (status already collected by the
 * caller's own waitpid()).
 */
static void
process_supervisor_restart_service(ProcessService *service, int status)
{
	if (WIFEXITED(status))
	{
		log_warn("Service \"%s\" (pid %d) exited with status %d",
				 service->name, service->pid, WEXITSTATUS(status));
	}
	else if (WIFSIGNALED(status))
	{
		log_warn("Service \"%s\" (pid %d) exited after receiving signal %s",
				 service->name, service->pid, strsignal(WTERMSIG(status)));
	}

	service->pid = -1;

	if (service->policy == PROCESS_RP_TEMPORARY)
	{
		return;
	}

	if (service->gaveUp)
	{
		/* already logged once when we first gave up; stay stopped */
		return;
	}

	if (!process_restart_counters_may_restart(&(service->restartCounters)))
	{
		uint64_t now = (uint64_t) time(NULL);
		ProcessRestartCounters *counters = &(service->restartCounters);
		int oldestPosition = (counters->position + 1) % PROCESS_SUPERVISOR_MAX_RETRY;

		log_error("Service \"%s\" has already been restarted %d times in "
				  "the last %d seconds, giving up on restarting it "
				  "(other services are unaffected)",
				  service->name, PROCESS_SUPERVISOR_MAX_RETRY,
				  (int) (now - counters->startTime[oldestPosition]));

		service->gaveUp = true;
		return;
	}

	process_restart_counters_record(&(service->restartCounters),
									(uint64_t) time(NULL));

	log_info("Restarting service \"%s\"", service->name);

	if (!(*service->startFunction)(service->context, &(service->pid)))
	{
		log_error("Failed to restart service \"%s\"", service->name);
		service->pid = -1;
	}
}


/*
 * process_supervisor_tick drains every currently-exited child with a
 * single wildcard waitpid(-1, WNOHANG) loop -- this MUST be the only
 * waitpid(-1, ...) call site in a process that uses this supervisor:
 * two independent wildcard reapers racing for the same exited child's
 * status is exactly the bug this project has already hit twice (pg_
 * autoctl's own supervisor.c vs. a service's own sibling process, and
 * pg_walserver's own accept_loop.c reap_children() vs. this file, before
 * this fix) -- whichever reaper's waitpid() call happens to run first
 * silently consumes the zombie, and the other sees nothing (a second
 * waitpid() on an already-reaped pid returns -1/ECHILD, not the exit
 * status it needed), permanently hiding that child's death.
 *
 * A pid matching one of supervisor's own services is reaped and, per its
 * RestartPolicy and MaxR/MaxT backoff, restarted (or given up on, logged,
 * left stopped). A pid matching neither is handed to otherChildExited
 * (may be NULL) with its raw wait status -- the caller's own, separately
 * tracked children (e.g. accept_loop.c's per-connection children).
 * Anything left over (matching nothing at all) is an orphaned, reparented
 * grandchild -- only possible when running as PID 1 inside a container --
 * logged at INFO, not ERROR, and otherwise ignored, the same PID-1
 * hygiene pg_autoctl's own supervisor.c already provides.
 */
void
process_supervisor_tick(ProcessSupervisor *supervisor,
						bool (*otherChildExited)(void *ctx, pid_t pid,
												 int status),
						void *otherCtx)
{
	for (;;)
	{
		int status = 0;
		pid_t pid = waitpid(-1, &status, WNOHANG);

		if (pid <= 0)
		{
			/* 0: nothing more exited right now; -1: no children at all (ECHILD) */
			return;
		}

		ProcessService *service = NULL;

		if (process_supervisor_find_service(supervisor, pid, &service))
		{
			process_supervisor_restart_service(service, status);
			continue;
		}

		if (otherChildExited != NULL && otherChildExited(otherCtx, pid, status))
		{
			continue;
		}

		/*
		 * Unknown to both this supervisor and the caller: only possible
		 * when running as PID 1 inside a container, where the kernel
		 * reparents orphaned grandchildren to us. Expected, not a bug.
		 */
		process_supervisor_log_unknown_pid(pid);
	}
}


/*
 * process_supervisor_stop_all signals every still-running service with
 * signal (SIGINT/SIGTERM, never SIGKILL as the first try), waits up to
 * timeoutMs for all of them to exit, then escalates to SIGKILL for
 * anything still alive past that.
 */
void
process_supervisor_stop_all(ProcessSupervisor *supervisor, int signal,
							int timeoutMs)
{
	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		ProcessService *service = &(supervisor->services[i]);

		if (service->pid <= 0)
		{
			continue;
		}

		log_info("Stopping service \"%s\" (pid %d)",
				 service->name, service->pid);

		if (kill(service->pid, signal) != 0 && errno != ESRCH)
		{
			log_error("Failed to signal service \"%s\" (pid %d): %m",
					  service->name, service->pid);
		}
	}

	int waitedMs = 0;

	while (waitedMs < timeoutMs)
	{
		bool anyAlive = false;

		for (int i = 0; i < supervisor->serviceCount; i++)
		{
			ProcessService *service = &(supervisor->services[i]);

			if (service->pid <= 0)
			{
				continue;
			}

			int status = 0;
			pid_t ret = waitpid(service->pid, &status, WNOHANG);

			if (ret == service->pid || (ret == -1 && errno != EINTR))
			{
				service->pid = -1;
			}
			else
			{
				anyAlive = true;
			}
		}

		if (!anyAlive)
		{
			return;
		}

		pg_usleep(100 * 1000);
		waitedMs += 100;
	}

	for (int i = 0; i < supervisor->serviceCount; i++)
	{
		ProcessService *service = &(supervisor->services[i]);

		if (service->pid <= 0)
		{
			continue;
		}

		log_warn("Service \"%s\" (pid %d) did not stop within %dms, "
				 "sending SIGKILL", service->name, service->pid, timeoutMs);

		(void) kill(service->pid, SIGKILL);

		int status = 0;

		while (waitpid(service->pid, &status, 0) == -1 && errno == EINTR)
		{
			/* retry */
		}

		service->pid = -1;
	}
}
