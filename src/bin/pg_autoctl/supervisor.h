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
 * start and stop function used in the struct Service.
 *
 * start functions get passed a context that is manually provided in the
 * structure, typically a Monitor struct pointer for the monitor service and a
 * Keeper struct pointer for a keeper service.
 *
 * stop functions get passed a context that is a pointer to the Service struct
 * definition of the service being asked to stop, and we use a void * data type
 * here to break out of a mutual recursive definition.
 *
 * reload functions get passed a context that is a pointer to the Service
 * struct definition of the service being asked to stop.
 */
typedef bool (*ServiceStartFunction)(void *context, pid_t *pid);
typedef bool (*ServiceStopFunction)(void *context);
typedef void (*ServiceReloadFunction)(void *context);

typedef struct Service
{
	char name[NAMEDATALEN];             /* Service name for the user */
	pid_t pid;                          /* Service PID */
	ServiceStartFunction startFunction; /* how to re-start the service */
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
