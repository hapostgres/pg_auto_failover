/*
 * src/bin/pg_autoctl/lock_utils.c
 *   Implementations of utility functions for inter-process locking
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <unistd.h>

#include "defaults.h"
#include "file_utils.h"
#include "env_utils.h"
#include "lock_utils.h"
#include "log.h"
#include "string_utils.h"


/*
 * See man semctl(2)
 */
#if !defined(__APPLE__)
union semun
{
	int val;
	struct semid_ds *buf;
	unsigned short *array;
};
#endif

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
	if (env_exists(PG_AUTOCTL_LOG_SEMAPHORE))
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
	if (env_exists(PG_AUTOCTL_LOG_SEMAPHORE))
	{
		/* there's no semaphore closing protocol in SysV */
		return true;
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
	union semun semun;

	semaphore->semId = semget(IPC_PRIVATE, 1, 0600);

	if (semaphore->semId < 0)
	{
		/* the semaphore_log_lock_function has not been set yet */
		log_fatal("Failed to create semaphore: %m\n");
		return false;
	}

	semun.val = 1;
	if (semctl(semaphore->semId, 0, SETVAL, semun) < 0)
	{
		/* the semaphore_log_lock_function has not been set yet */
		log_fatal("Failed to set semaphore %d/%d to value %d : %m\n",
				  semaphore->semId, 0, semun.val);
		return false;
	}

	return true;
}


/*
 * semaphore_open opens our IPC_PRIVATE semaphore.
 *
 * We don't have a key for it, because we asked the kernel to create a new
 * semaphore set with the guarantee that it would not exist already. So we
 * re-use the semaphore identifier directly.
 *
 * We don't even have to call semget(2) here at all, because we share our
 * semaphore identifier in the environment directly.
 */
bool
semaphore_open(Semaphore *semaphore)
{
	char semIdString[BUFSIZE];

	if (!get_env_copy(PG_AUTOCTL_LOG_SEMAPHORE, semIdString, BUFSIZE))
	{
		/* errors have already been logged */
		return false;
	}

	if (!stringToInt(semIdString, &semaphore->semId))
	{
		/* errors have already been logged */
		return false;
	}

	/* we have the semaphore identifier, no need to call semget(2), done */
	return true;
}


/*
 * semaphore_unlink removes an existing named semaphore.
 */
bool
semaphore_unlink(Semaphore *semaphore)
{
	union semun semun;

	semun.val = 0;              /* unused, but keep compiler quiet */

	if (semctl(semaphore->semId, 0, IPC_RMID, semun) < 0)
	{
		fformat(stderr, "Failed to remove semaphore %d: %m", semaphore->semId);
		return false;
	}

	return true;
}


/*
 * semaphore_cleanup is used when we find a stale PID file, to remove a
 * possibly left behind semaphore. The user could also use ipcs and ipcrm to
 * figure that out, if the stale pidfile does not exists anymore.
 */
bool
semaphore_cleanup(const char *pidfile)
{
	Semaphore semaphore;

	long fileSize = 0L;
	char *fileContents = NULL;
	char *fileLines[BUFSIZE] = { 0 };
	int lineCount = 0;

	char semIdString[BUFSIZE] = { 0 };

	if (!file_exists(pidfile))
	{
		return false;
	}

	if (!read_file(pidfile, &fileContents, &fileSize))
	{
		return false;
	}

	lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	if (lineCount < 2)
	{
		return false;
	}

	if (!stringToInt(semIdString, &(semaphore.semId)))
	{
		/* errors have already been logged */
		return false;
	}

	return semaphore_unlink(&semaphore);
}


/*
 * semaphore_lock locks a semaphore (decrement count), blocking if count would
 * be < 0
 */
bool
semaphore_lock(Semaphore *semaphore)
{
	int errStatus;
	struct sembuf sops;

	sops.sem_op = -1;           /* decrement */
	sops.sem_flg = SEM_UNDO;
	sops.sem_num = 0;

	/*
	 * Note: if errStatus is -1 and errno == EINTR then it means we returned
	 * from the operation prematurely because we were sent a signal.  So we
	 * try and lock the semaphore again.
	 *
	 * We used to check interrupts here, but that required servicing
	 * interrupts directly from signal handlers. Which is hard to do safely
	 * and portably.
	 */
	do {
		errStatus = semop(semaphore->semId, &sops, 1);
	} while (errStatus < 0 && errno == EINTR);

	if (errStatus < 0)
	{
		fformat(stderr,
				"Failed to acquire a lock with semaphore %d: %m\n",
				semaphore->semId);
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
	struct sembuf sops;

	sops.sem_op = 1;            /* increment */
	sops.sem_flg = SEM_UNDO;
	sops.sem_num = 0;

	/*
	 * Note: if errStatus is -1 and errno == EINTR then it means we returned
	 * from the operation prematurely because we were sent a signal.  So we
	 * try and unlock the semaphore again. Not clear this can really happen,
	 * but might as well cope.
	 */
	do {
		errStatus = semop(semaphore->semId, &sops, 1);
	} while (errStatus < 0 && errno == EINTR);

	if (errStatus < 0)
	{
		fformat(stderr,
				"Failed to release a lock with semaphore %d: %m\n",
				semaphore->semId);
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
