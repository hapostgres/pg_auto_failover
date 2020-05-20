/*
 * src/bin/pg_autoctl/lock_utils.h
 *   Utility functions for inter-process locking
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef LOCK_UTILS_H
#define LOCK_UTILS_H

#include <stdbool.h>
#include <semaphore.h>

/* man sem_overview(7) for details */
#define SEM_NAME_MAX 251

typedef struct Semaphore
{
	pid_t pid;
	char name[SEM_NAME_MAX];
	sem_t *sema;
} Semaphore;


bool semaphore_init(Semaphore *semaphore);
bool semaphore_finish(Semaphore *semaphore);

bool semaphore_create(Semaphore *semaphore);
bool semaphore_open(Semaphore *semaphore);
bool semaphore_close(Semaphore *semaphore);
bool semaphore_unlink(Semaphore *semaphore);

bool semaphore_lock(Semaphore *semaphore);
bool semaphore_unlock(Semaphore *semaphore);

void semaphore_log_lock_function(void *udata, int mode);

#endif /* LOCK_UTILS_H */
