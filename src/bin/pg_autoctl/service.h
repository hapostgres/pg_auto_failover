/*
 * src/bin/pg_autoctl/service.h
 *   Manage sub-processes for pg_autoctl run.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_H
#define SERVICE_H

#include <stdbool.h>

#include "keeper.h"

bool service_init(Keeper *keeper, pid_t *pid);
bool service_stop(Keeper *keeper);
bool service_start(Keeper *keeper);

bool read_pidfile(const char *pidfile, pid_t *pid);

#endif /* SERVICE_H */
