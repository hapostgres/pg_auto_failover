/*
 * src/bin/pg_autoctl/env_utils.h
 *   Utility functions for interacting with environment settings.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ENV_UTILS_H
#define ENV_UTILS_H

#include "postgres_fe.h"

#define GETENV_ERROR_INVALID_NAME -3
#define GETENV_ERROR_BUFFER_SIZE -2
#define GETENV_ERROR_NOT_FOUND -1
#define GETENV_EMPTY 0

int get_env_variable(const char *name, char *outbuffer, int maxLength);
bool get_env_pgdata(char *pgdata, int size);
#endif /* ENV_UTILS_H */
