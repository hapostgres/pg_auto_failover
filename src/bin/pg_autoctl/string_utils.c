/*
 * src/bin/pg_autoctl/string_utils.c
 *   Implementations of utility functions for string handling
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "parsing.h"
#include "string_utils.h"

/*
 * intToString converts an int to an IntString, which contains a decimal string
 * representation of the integer.
 */
IntString
intToString(int64_t number)
{
	IntString intString;

	intString.intValue = number;

	sformat(intString.strValue, INTSTRING_MAX_DIGITS, "%" PRId64, number);

	return intString;
}


/*
 * converts given string to 64 bit integer value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToInt(const char *str, int *number)
{
	char *endptr;
	long long int n;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;

	n = strtoll(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n < INT_MIN || n > INT_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to 64 bit integer value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToInt64(const char *str, int64_t *number)
{
	char *endptr;
	long long int n;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;

	n = strtoll(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n < INT64_MIN || n > INT64_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to 64 bit unsigned integer value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToUInt(const char *str, unsigned int *number)
{
	char *endptr;
	unsigned long long n = 0;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;
	n = strtoull(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n > UINT_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to 64 bit unsigned integer value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToUInt64(const char *str, uint64_t *number)
{
	char *endptr;
	unsigned long long n = 0;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;
	n = strtoull(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n > UINT64_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to short value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToShort(const char *str, short *number)
{
	char *endptr;
	long long int n;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;

	n = strtoll(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n < SHRT_MIN || n > SHRT_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to unsigned short value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToUShort(const char *str, unsigned short *number)
{
	char *endptr;
	unsigned long long n = 0;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;
	n = strtoull(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n > USHRT_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to 32 bit integer value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToInt32(const char *str, int32_t *number)
{
	char *endptr;
	long long int n;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;

	n = strtoll(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n < INT32_MIN || n > INT32_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * converts given string to 32 bit unsigned int value.
 * returns 0 upon failure and sets error flag
 */
bool
stringToUInt32(const char *str, uint32_t *number)
{
	char *endptr;
	unsigned long long n = 0;

	if (str == NULL)
	{
		return false;
	}

	if (number == NULL)
	{
		return false;
	}

	errno = 0;
	n = strtoull(str, &endptr, 10);

	if (str == endptr)
	{
		return false;
	}
	else if (errno != 0)
	{
		return false;
	}
	else if (*endptr != '\0')
	{
		return false;
	}
	else if (n > UINT32_MAX)
	{
		return false;
	}

	*number = n;

	return true;
}


/*
 * splitLines prepares a multi-line error message in a way that calling code
 * can loop around one line at a time and call log_error() or log_warn() on
 * individual lines.
 */
int
splitLines(char *errorMessage, char **linesArray, int size)
{
	int lineNumber = 0;
	char *currentLine = errorMessage;

	do {
		char *newLinePtr = strchr(currentLine, '\n');

		if (newLinePtr == NULL && strlen(currentLine) > 0)
		{
			linesArray[lineNumber++] = currentLine;
			currentLine = NULL;
		}
		else
		{
			*newLinePtr = '\0';

			linesArray[lineNumber++] = currentLine;

			currentLine = ++newLinePtr;
		}
	} while (currentLine != NULL && *currentLine != '\0' && lineNumber < size);

	return lineNumber;
}


/*
 * rewrite_file_skipping_lines_matching reads filename contents in memory and
 * then write the same content to the filename again, but skipping lines that
 * match with the given regex.
 */
bool
rewrite_file_skipping_lines_matching(const char *filename, const char *regex)
{
	char *fileContents = NULL;
	long fileSize = 0L;

	char *fileLines[BUFSIZE];
	int lineCount = 0;
	int lineNumber = 0;

	PQExpBuffer newFileContents = createPQExpBuffer();

	if (newFileContents == NULL)
	{
		log_error("Failed to allocate memory");
		return false;
	}

	if (!read_file(filename, &fileContents, &fileSize))
	{
		/* errors have already been logged */
		return false;
	}

	log_debug("rewrite_file_skipping_lines_matching: %s", regex);
	log_debug("rewrite_file_skipping_lines_matching: read \n%s", fileContents);

	/* split the file contents in lines and filter them */
	lineCount = splitLines(fileContents, fileLines, BUFSIZE);

	for (lineNumber = 0; lineNumber < lineCount; lineNumber++)
	{
		char *currentLine = fileLines[lineNumber];

		/* copy lines that are NOT matching the regex */
		if (regexp_first_match(currentLine, regex) == NULL)
		{
			appendPQExpBuffer(newFileContents, "%s\n", currentLine);
		}
	}

	if (!write_file(newFileContents->data, newFileContents->len, filename))
	{
		/* errors have already been logged */
		destroyPQExpBuffer(newFileContents);
		return false;
	}

	log_debug("rewrite_file_skipping_lines_matching: wrote \n%s",
			  newFileContents->data);

	destroyPQExpBuffer(newFileContents);

	return true;
}
