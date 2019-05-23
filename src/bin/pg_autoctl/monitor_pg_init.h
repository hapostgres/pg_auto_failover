/*
 * src/bin/pg_autoctl/monitor_pg_init.h
 *     Monitor configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef MONITOR_PG_INIT_H
#define MONITOR_PG_INIT_H

#include <stdbool.h>

#include "monitor.h"
#include "monitor_config.h"

bool monitor_pg_init(Monitor *monitor, MonitorConfig *config);

#endif /* MONITOR_PG_INIT_H */
