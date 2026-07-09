
/*
 * src/bin/pg_autoctl/service_run_hooks.h
 *   Utilities to start the keeper services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_RUN_HOOKS_H
#define SERVICE_RUN_HOOKS_H

#include <stdbool.h>

#include "keeper.h"
#include "keeper_config.h"

bool service_run_hooks_start(void *context, pid_t *pid);
void service_run_hooks_runprogram(Keeper *keeper);
bool service_run_hooks_init(Keeper *keeper);
bool service_run_hooks_loop(Keeper *keeper, pid_t start_pid);


#endif /* SERVICE_RUN_HOOKS_H */
