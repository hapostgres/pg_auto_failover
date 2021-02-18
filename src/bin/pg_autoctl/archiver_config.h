/*
 * src/bin/pg_autoctl/archiver_config.h
 *     Archiver configuration data structure and function definitions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_CONFIG_H
#define ARCHIVER_CONFIG_H

#include <limits.h>
#include <stdbool.h>

#include "config.h"
#include "defaults.h"
#include "pgctl.h"
#include "pgsql.h"

typedef struct ArchiverConfig
{
	char directory[MAXPGPATH];

	char role[NAMEDATALEN];
	char monitor_pguri[MAXCONNINFO];
	char name[_POSIX_HOST_NAME_MAX];
	char hostname[_POSIX_HOST_NAME_MAX];
} ArchiverConfig;

#endif /* ARCHIVER_CONFIG_H */
