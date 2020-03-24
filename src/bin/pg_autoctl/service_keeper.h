/*
 * src/bin/pg_autoctl/monitor_service.h
 *   Utilities to start/stop the pg_autoctl service on a monitor node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_SERVICE_H
#define KEEPER_SERVICE_H

#include <stdbool.h>

#include "pgsql.h"
#include "keeper_config.h"

bool keep_service_start(Keeper *keeper);
bool keeper_start_node_active_process(Keeper *keeper, pid_t *nodeActivePid);
bool keeper_node_active_loop(Keeper *keeper, pid_t start_pid);
bool keeper_service_init(Keeper *keeper, pid_t *pid);

#endif /* KEEPER_SERVICE_H */
