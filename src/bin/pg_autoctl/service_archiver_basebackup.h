/*
 * src/bin/pg_autoctl/service_archiver_basebackup.h
 *   Archiving & Disaster Recovery: base backup generation, `live` source
 *   only (Milestone 5's own first half). See service_archiver_basebackup.c
 *   for the full scope note.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef SERVICE_ARCHIVER_BASEBACKUP_H
#define SERVICE_ARCHIVER_BASEBACKUP_H

#include "keeper.h"

bool service_archiver_maybe_generate_basebackup(Keeper *keeper);

#endif /* SERVICE_ARCHIVER_BASEBACKUP_H */
