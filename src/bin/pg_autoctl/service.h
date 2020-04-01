/*
 * src/bin/pg_autoctl/service.h
 *   Utilities to start/stop the pg_autoctl service.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SERVICE_H
#define SERVICE_H

#include <inttypes.h>
#include <signal.h>

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "monitor_config.h"

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
 */
typedef bool (*ServiceStartFunction)(void *context, pid_t *pid);
typedef bool (*ServiceStopFunction)(void *context);

typedef struct Service
{
	char name[NAMEDATALEN];				/* Service name for the user */
	pid_t pid;							/* Service PID */
	ServiceStartFunction startFunction;	/* how to re-start the service */
	ServiceStopFunction stopFunction;	/* how to stop the service */
	void *context;			   /* Service Context (Monitor or Keeper struct) */
} Service;

bool service_start(Service services[], int serviceCount, const char *pidfile);

bool service_stop(const char *pidfile);

bool create_pidfile(const char *pidfile, pid_t pid);
bool read_pidfile(const char *pidfile, pid_t *pid);
bool remove_pidfile(const char *pidfile);
void check_pidfile(const char *pidfile, pid_t start_pid);


#endif /* SERVICE_H */
