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

int get_env_variable(const char *name, char *outbuffer, int maxLength);

#endif /* ENV_UTILS_H */
