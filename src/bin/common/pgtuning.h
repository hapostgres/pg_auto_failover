/*
 * src/bin/pg_autoctl/pgtuning.h
 *     Adjust some very basic Postgres tuning to the system properties.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PGTUNING_H
#define PGTUNING_H

#include <stdbool.h>

extern GUC postgres_tuning[];

bool pgtuning_prepare_guc_settings(GUC *settings, char *config, size_t size);

#endif /* PGTUNING_H */
