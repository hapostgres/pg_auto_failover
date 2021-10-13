/*
 * src/bin/pg_autoctl/archiving.h
 *     Implement archiving support for Postgres.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVING_H
#define ARCHIVING_H

#include <stdbool.h>

#include "keeper.h"
#include "monitor.h"

bool archive_wal_with_config(Keeper *keeper,
							 const char *archiverConfigPathname,
							 const char *filename);

bool archive_wal_for_policy(Keeper *keeper,
							MonitorArchiverPolicy *policy,
							const char *filename);

#endif  /* ARCHIVING_H */
