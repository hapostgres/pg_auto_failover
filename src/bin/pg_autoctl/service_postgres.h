/*
 * src/bin/pg_autoctl/postgres_service.h
 *   Utilities to start/stop the pg_autoctl service on a monitor node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef POSTGRES_SERVICE_H
#define POSTGRES_SERVICE_H

#include <inttypes.h>
#include <signal.h>

#include "keeper.h"
#include "keeper_config.h"

bool service_postgres_start(void *context, pid_t *pid);

#endif /* POSTGRES_SERVICE_H */
