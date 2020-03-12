/*
 * src/bin/pg_autoctl/env_utils.c
 *   Utility functions for interacting with environment settings.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "env_utils.h"
#include "log.h"

/*
 * get_env_variable checks for the environment variable and copies
 * its value into provided buffer if the buffer is not null.
 *
 * Function returns the length of the environment variable.
 *
 * Return values
 * - GETENV_ERROR (-2)  : incorrect variable name is provided
 * - GETENV_NOT_FOUND (-1) : variable is not found
 * - GETENV_EMPTY (0)  : variable is found but no value is set
 * - (1+) length of the environment variable
 *
 * The function returns the length of the variable even, not the copied
 * size. Therefore caller is responsible to compare the returned length
 * with the provided buffer size in case buffer is not large enough.
 */
int
get_env_variable(const char *name, char *result, int maxLength)
{
	char *envvalue = NULL;

	if (name == NULL || strlen(name) == 0)
	{
		log_error("Failed to get environment setting. NULL or empty variable name is provided");
		GETENV_ERROR;
	}

	/*
	 * Explanation of IGNORE-BANNED
	 * getenv is safe here because we never provide null argument,
	 * and copy out the result immediately.
	 */
	envvalue = getenv(name); /* IGNORE-BANNED */

	if (envvalue == NULL)
	{
		return GETENV_NOT_FOUND;
	}

	if (result == NULL)
	{
		return strlen(envvalue);;
	}

	return strlcpy(result, envvalue, maxLength);
}
