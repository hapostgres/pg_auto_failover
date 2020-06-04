
/*
 * src/bin/pg_autoctl/keeper_service.h
 *   Utilities to start the keeper services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_SERVICE_H
#define KEEPER_SERVICE_H

#include <stdbool.h>

#include "keeper.h"
#include "keeper_config.h"

bool start_keeper(Keeper *keeper);
bool service_keeper_start(void *context, pid_t *pid);
void service_keeper_runprogram(Keeper *keeper);
bool service_keeper_node_active_init(Keeper *keeper);
bool keeper_node_active_loop(Keeper *keeper, pid_t start_pid);


#endif /* KEEPER_SERVICE_INIT_H */
