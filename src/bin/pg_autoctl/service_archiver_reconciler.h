/*
 * src/bin/pg_autoctl/service_archiver_reconciler.h
 *   Archiving & Disaster Recovery: an archiver's own membership
 *   reconciler -- the intermediate supervised process that lets one
 *   archiver manage WAL capture (and, through it, base-backup
 *   scheduling) for every (formation, group) membership it holds, not
 *   just one. See service_archiver_reconciler.c for the full design.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_RECONCILER_H
#define SERVICE_ARCHIVER_RECONCILER_H

#include "keeper.h"

bool service_archiver_reconciler_start(void *context, pid_t *pid);

#endif /* SERVICE_ARCHIVER_RECONCILER_H */
