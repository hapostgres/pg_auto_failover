/*
 * src/bin/pg_autoctl/lock_utils.c
 *   Implementations of utility functions for inter-process locking
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

#include "defaults.h"
#include "file_utils.h"
#include "env_utils.h"
#include "lock_utils.h"

/*
 * semaphore_init creates or opens a named semaphore for the current process.
 *
 * We use the environment variable PG_AUTOCTL_SERVICE to signal when a process
 * is a child process of the main pg_autoctl supervisor so that we are able to
 * initialise our locking strategy before parsing the command line. After all,
 * we might have to log some output during the parsing itself.
 */
bool
semaphore_init(Semaphore *semaphore)
{
	if (env_exists(PG_AUTOCTL_SERVICE))
	{
		return semaphore_open(semaphore);
	}
	else
	{
		return semaphore_create(semaphore);
	}
}


/*
 * semaphore_finish closes or unlinks given semaphore.
 */
bool
semaphore_finish(Semaphore *semaphore)
{
	if (env_exists(PG_AUTOCTL_SERVICE))
	{
		return semaphore_close(semaphore);
	}
	else
	{
		return semaphore_unlink(semaphore);
	}
}


/*
 * semaphore_create creates a new semaphore with the value 1.
 */
bool
semaphore_create(Semaphore *semaphore)
{
	pid_t pid = getpid();

	semaphore->pid = pid;
	sformat(semaphore->name, SEM_NAME_MAX, "/pg_autoctl.%u", pid);

	semaphore->sema = sem_open(semaphore->name,
							   O_CREAT | O_EXCL,
							   (mode_t) 0600,
							   (unsigned) 1);

	if (semaphore->sema == SEM_FAILED)
	{
		fformat(stderr,
				"Failed to create semaphore \"%s\": %m\n", semaphore->name);
		return false;
	}

	return true;
}


/*
 * semaphore_open opens an already existing semaphore.
 */
bool
semaphore_open(Semaphore *semaphore)
{
	pid_t ppid = getppid();

	semaphore->pid = ppid;
	sformat(semaphore->name, SEM_NAME_MAX, "/pg_autoctl.%u", ppid);

	semaphore->sema = sem_open(semaphore->name, 0);

	if (semaphore->sema == SEM_FAILED)
	{
		fformat(stderr,
				"Failed to open semaphore \"%s\": %m\n", semaphore->name);
		return false;
	}

	return true;
}


/*
 * semaphore_close closes given semaphore.
 */
bool
semaphore_close(Semaphore *semaphore)
{
	if (sem_close(semaphore->sema) == 0)
	{
		return true;
	}

	fformat(stderr, "Failed to close semaphore \"%s\": %m\n", semaphore->name);
	return false;
}


/*
 * semaphore_unlink removes an existing named semaphore.
 */
bool
semaphore_unlink(Semaphore *semaphore)
{
	if (sem_unlink(semaphore->name) == 0)
	{
		return true;
	}

	fformat(stderr, "Failed to unlink semaphore \"%s\": %m\n", semaphore->name);
	return false;
}


/*
 * semaphore_lock locks a semaphore (decrement count), blocking if count would
 * be < 0
 */
bool
semaphore_lock(Semaphore *semaphore)
{
	int errStatus;

	do {
		errStatus = sem_wait(semaphore->sema);
	} while (errStatus < 0 && errno == EINTR);

	if (errStatus < 0)
	{
		fformat(stderr,
				"Failed to acquire a lock with semaphore \"%s\": %m\n",
				semaphore->name);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	return true;
}


/*
 * semaphore_unlock unlocks a semaphore (increment count)
 */
bool
semaphore_unlock(Semaphore *semaphore)
{
	int errStatus;

	/*
	 * Note: if errStatus is -1 and errno == EINTR then it means we returned
	 * from the operation prematurely because we were sent a signal.  So we
	 * try and unlock the semaphore again. Not clear this can really happen,
	 * but might as well cope.
	 */
	do {
		errStatus = sem_post(semaphore->sema);
	} while (errStatus < 0 && errno == EINTR);

	if (errStatus < 0)
	{
		fformat(stderr,
				"Failed to release a lock with semaphore \"%s\": %m\n",
				semaphore->name);
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	return true;
}


/*
 * semaphore_log_lock_function integrates our semaphore facility with the
 * logging tool in use in this project.
 */
void
semaphore_log_lock_function(void *udata, int mode)
{
	Semaphore *semaphore = (Semaphore *) udata;

	switch (mode)
	{
		/* unlock */
		case 0:
		{
			(void) semaphore_unlock(semaphore);
			break;
		}

		/* lock */
		case 1:
		{
			(void) semaphore_lock(semaphore);
			break;
		}

		default:
		{
			fformat(stderr,
					"BUG: semaphore_log_lock_function called with mode %d",
					mode);
			exit(EXIT_CODE_INTERNAL_ERROR);
		}
	}
}
