/*
 * src/bin/pg_autoctl/monitor_service.h
 *   Utilities to start/stop the pg_autoctl service on a monitor node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef MONITOR_SERVICE_H
#define MONITOR_SERVICE_H

#include <stdbool.h>

#include "pgsql.h"
#include "monitor_config.h"

bool start_monitor(Monitor *monitor);
bool service_monitor_start(void *context, pid_t *pid);
bool service_monitor_stop(void *context);
bool monitor_service_run(Monitor *monitor);
void service_monitor_runprogram(Monitor *monitor);
void service_monitor_reload(void *context);

#endif /* MONITOR_SERVICE_H */
