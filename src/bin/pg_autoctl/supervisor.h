/*
 * src/bin/pg_autoctl/supervisor.h
 *   Utilities to start/stop the pg_autoctl services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include <inttypes.h>
#include <signal.h>

/*
 * The supervisor works with an array of Service entries. Each service defines
 * its behavior thanks to a start function, a stop function, and a reload
 * function. Those are called at different points to adjust to the situation as
 * seen by the supervisor.
 *
 * In particular, services may be started more than once when they fail.
 */
typedef struct Service
{
	char name[NAMEDATALEN];             /* Service name for the user */
	pid_t pid;                          /* Service PID */
	bool (*startFunction)(void *context, pid_t *pid);
	bool (*stopFunction)(struct Service *service);
	void (*reloadFunction)(struct Service *service);
	void *context;             /* Service Context (Monitor or Keeper struct) */
	int retries;
	uint64_t startTime;
	uint64_t stopTime;
} Service;

bool supervisor_start(Service services[], int serviceCount, const char *pidfile);

bool supervisor_stop(const char *pidfile);


#endif /* SUPERVISOR_H */
