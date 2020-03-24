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

typedef bool (*ServiceStartFunction)(void *context, pid_t *pid);

typedef struct Service
{
	char name[NAMEDATALEN];				/* Service name for the user */
	pid_t pid;							/* Service PID */
	ServiceStartFunction startFunction; /* how to re-start the service */
	void *context;			   /* Service Context (Monitor or Keeper struct) */
} Service;

bool service_start(Service services[], int serviceCount, const char *pidfile);

bool service_stop(const char *pidfile);

bool create_pidfile(const char *pidfile, pid_t pid);
bool read_pidfile(const char *pidfile, pid_t *pid);
bool remove_pidfile(const char *pidfile);
void check_pidfile(const char *pidfile, pid_t start_pid);


#endif /* SERVICE_H */
