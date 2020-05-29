/*
 * src/bin/pg_autoctl/monitor_service_init.h
 *   Utilities to start the monitor init services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef MONITOR_SERVICE_INIT_H
#define MONITOR_SERVICE_INIT_H

#include <stdbool.h>

#include "pgsql.h"
#include "monitor_config.h"

bool service_monitor_init(Monitor *monitor);

#endif /* MONITOR_SERVICE_INIT_H */
