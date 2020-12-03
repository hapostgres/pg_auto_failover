/*
 * src/bin/pg_autoctl/service_pgbouncer.h
 *   Utilities to start/stop a pgbouncer service in a node.
 *
 * Copyright (c) XXX.
 * Licensed under the PostgreSQL License.
 *
 */
#ifndef SERVICE_PGBOUNCER_H
#define SERVICE_PPGBOUNCER_H

#include "keeper.h"
#include "keeper_config.h"

bool service_pgbouncer_start(void *context, pid_t *pid);

#endif /* SERVICE_POSTGRES_CTL_H */
