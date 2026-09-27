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
 * restart-backoff ring buffer -- see process_supervisor.h's own comment.
 * pg_autoctl/supervisor.c calls these same three functions directly for
 * its own Service/RestartCounters.
 */
void
process_restart_counters_start(ProcessRestartCounters *counters, uint64_t now)
{
	counters->count = 1;
	counters->position = 0;
	counters->startTime[0] = now;
}


void
process_restart_counters_record(ProcessRestartCounters *counters, uint64_t now)
{
	int position = (counters->position + 1) % PROCESS_SUPERVISOR_MAX_RETRY;

	counters->count += 1;
	counters->position = position;
	counters->startTime[position] = now;
}


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
 * process_supervisor_log_unknown_pid -- see process_supervisor.h.
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
