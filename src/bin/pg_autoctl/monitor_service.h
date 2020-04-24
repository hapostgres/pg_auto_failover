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

bool ensure_monitor_pg_running(Monitor *monitor);

bool monitor_service_run(Monitor *monitor, pid_t start_pid);

#endif /* MONITOR_SERVICE_H */
