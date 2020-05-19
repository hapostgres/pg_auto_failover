/*
 * src/bin/pg_autoctl/service_postgres_ctl.h
 *   Utilities to start/stop the pg_autoctl service on a keeper node.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SERVICE_POSTGRES_CTL_H
#define SERVICE_POSTGRES_CTL_H

#include <inttypes.h>
#include <signal.h>

#include "keeper.h"
#include "keeper_config.h"

bool service_postgres_ctl_start(void *context, pid_t *pid);
void service_postgres_ctl_runprogram(void);
void service_postgres_ctl_loop(LocalPostgresServer *postgres);

#endif /* SERVICE_POSTGRES_CTL_H */
