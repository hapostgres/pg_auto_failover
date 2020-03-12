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

#include "env_utils.h"
#include "log.h"

/*
 * get_env_variable checks for the environment variable and copies
 * its value into provided buffer if the buffer is not null.
 *
 * Function returns the length of the environment variable.
 *
 * Return values
 *  - GETENV_ERROR_INVALID_NAME : incorrect variable name is provided
 *  - GETENV_ERROR_BUFFER_SIZE  : buffer is not large enough
 *  - GETENV_ERROR_NOT_FOUND    : variable is not found
 *  - GETENV_EMPTY              : variable is found but no value is set
 *  - (1+)                      : length of the environment variable
 *
 * The function returns the length of the variable even, not the copied
 * size. Therefore caller is responsible to compare the returned length
 * with the provided buffer size in case buffer is not large enough.
 */
int
get_env_variable(const char *name, char *result, int maxLength)
{
	char *envvalue = NULL;
	int actualLength = -1;

	if (name == NULL || strlen(name) == 0)
	{
		log_error("Failed to get environment setting. NULL or empty variable name is provided");
		return GETENV_ERROR_INVALID_NAME;
	}

	/*
	 * Explanation of IGNORE-BANNED
	 * getenv is safe here because we never provide null argument,
	 * and copy out the result immediately.
	 */
	envvalue = getenv(name); /* IGNORE-BANNED */

	actualLength = strlen(envvalue);
	if (envvalue == NULL)
	{
		return GETENV_ERROR_NOT_FOUND;
	}

	if (result == NULL)
	{
		return actualLength;
	}

	*result = '\0';

	if (actualLength >= maxLength)
	{
		return GETENV_ERROR_BUFFER_SIZE;
	}

	return strlcpy(result, envvalue, maxLength);
}


/*
 * get_env_pgdata checks for environment value PGDATA
 * and copy its value into provided buffer.
 *
 * function returns true on successful run. returns false
 * if it can't find PGDATA or its value is larger than
 * the provided buffer
 */
bool
get_env_pgdata(char *pgdata, int size)
{
	return get_env_variable("PGDATA", pgdata, size) > 0;
}
