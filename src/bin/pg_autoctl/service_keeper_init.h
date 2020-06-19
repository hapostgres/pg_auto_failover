
/*
 * src/bin/pg_autoctl/keeper_service_init.h
 *   Utilities to start the keeper init services.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_SERVICE_INIT_H
#define KEEPER_SERVICE_INIT_H

#include <stdbool.h>

#include "pgsql.h"
#include "keeper_config.h"

bool service_keeper_init(Keeper *keeper);
bool service_keeper_init_start(void *context, pid_t *pid);


#endif /* KEEPER_SERVICE_INIT_H */
