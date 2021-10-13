/*
 * src/bin/pg_autoctl/wal-g.h
 *     Implementation of a wrapper around the WAL-G commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WAL_G_H
#define WAL_G_H

#include <errno.h>
#include <stdbool.h>

#define WAL_G_CONFIGURATION_FILENAME "wal-g.json"
#define WAL_G_PREFETCH_DIRNAME "wal-g-prefetch"

bool walg_prepare_config(const char *pgdata, const char *config,
						 char *archiverConfigPathname);

bool walg_wal_push(const char *config, const char *wal);
bool walg_wal_fetch(const char *pgdata, const char *config, const char *wal,
					const char *dest);

#endif  /* WAL_G_H */
