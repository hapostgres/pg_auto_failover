/*
 * src/bin/pg_autoctl/parsing.c
 *   API for parsing the output of some PostgreSQL server commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <inttypes.h>
#include <regex.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "parsing.h"


static bool parse_controldata_field_uint32(const char *controlDataString,
										   const char *fieldName,
										   uint32_t *dest);

static bool parse_controldata_field_uint64(const char *controlDataString,
										   const char *fieldName,
										   uint64_t *dest);

static int read_length_delimited_string_at(const char *ptr,
										   char *buffer, int size);

#define RE_MATCH_COUNT 10


/*
 * Simple Regexp matching that returns the first matching element.
 */
char *
regexp_first_match(const char *string, const char *regex)
{
	regex_t compiledRegex;
	int status = 0;

	regmatch_t m[RE_MATCH_COUNT];
	int matchStatus;

	if (string == NULL)
	{
		return NULL;
	}

	status = regcomp(&compiledRegex, regex, REG_EXTENDED | REG_NEWLINE);

	if (status != 0)
	{
		/*
		 * regerror() returns how many bytes are actually needed to host the
		 * error message, and truncates the error message when it doesn't fit
		 * in given size. If the message has been truncated, then we add an
		 * ellispis to our log entry.
		 *
		 * We could also dynamically allocate memory for the error message, but
		 * the error might be "out of memory" already...
		 */
		char message[BUFSIZE];
		size_t bytes = regerror(status, &compiledRegex, message, BUFSIZE);

		log_error("Failed to compile regex \"%s\": %s%s",
				  regex, message, bytes < BUFSIZE ? "..." : "");

		regfree(&compiledRegex);

		return NULL;
	}

	/*
	 * regexec returns 0 if the regular expression matches; otherwise, it
	 * returns a nonzero value.
	 */
	matchStatus = regexec(&compiledRegex, string, RE_MATCH_COUNT, m, 0);
	regfree(&compiledRegex);

	/* We're interested into 1. re matches 2. captured at least one group */
	if (matchStatus != 0 || m[0].rm_so == -1 || m[1].rm_so == -1)
	{
		return NULL;
	}
	else
	{
		int start = m[1].rm_so;
		int finish = m[1].rm_eo;
		int length = finish - start + 1;
		char *result = (char *) malloc(length * sizeof(char));

		strlcpy(result, string + start, length);

		return result;
	}
	return NULL;
}


/*
 * Parse the version number output from pg_ctl --version:
 *    pg_ctl (PostgreSQL) 10.3
 */
char *
parse_version_number(const char *version_string)
{
	return regexp_first_match(version_string, "([[:digit:].]+)");
}


/*
 * Parse the first 3 lines of output from pg_controldata:
 *
 *    pg_control version number:            1002
 *    Catalog version number:               201707211
 *    Database system identifier:           6534312872085436521
 *
 */
bool
parse_controldata(PostgresControlData *pgControlData,
				  const char *control_data_string)
{
	if (!parse_controldata_field_uint32(control_data_string,
										"pg_control version number",
										&(pgControlData->pg_control_version)) ||

		!parse_controldata_field_uint32(control_data_string,
										"Catalog version number",
										&(pgControlData->catalog_version_no)) ||

		!parse_controldata_field_uint64(control_data_string,
										"Database system identifier",
										&(pgControlData->system_identifier)))
	{
		log_error("Failed to parse pg_controldata output");
		return false;
	}
	return true;
}


/*
 * parse_controldata_field_uint32 matches pg_controldata output for a field
 * name and gets its value as an uint64_t. It returns false when something went
 * wrong, and true when the value can be used.
 */
static bool
parse_controldata_field_uint32(const char *controlDataString,
							   const char *fieldName,
							   uint32_t *dest)
{
	char regex[BUFSIZE];
	char *match;

	/*
	 * Explanation of IGNORE-BANNED:
	 * snprintf is safe here, we never write beyond the buffer,
	 * parameter fieldName is given internally, it would never
	 * cause target string to exceed the buffer.
	 */
	if (snprintf(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName) >= BUFSIZE) /* IGNORE-BANNED */
	{
		return false;
	}

	match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	*dest = strtol(match, NULL, 10);

	if (*dest == 0 && errno == EINVAL)
	{
		log_error("Failed to parse number \"%s\": %s", match, strerror(errno));
		free(match);
		return false;
	}

	free(match);
	return true;
}


/*
 * parse_controldata_field_uint64 matches pg_controldata output for a field
 * name and gets its value as an uint64_t. It returns false when something went
 * wrong, and true when the value can be used.
 */
static bool
parse_controldata_field_uint64(const char *controlDataString,
							   const char *fieldName,
							   uint64_t *dest)
{
	char regex[BUFSIZE];
	char *match;

	/*
	 * Explanation of IGNORE-BANNED:
	 * snprintf is safe here, we never write beyond the buffer,
	 * parameter fieldName is given internally, it would never
	 * cause target string to exceed the buffer.
	 */
	if (snprintf(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName) >= BUFSIZE) /* IGNORE-BANNED */
	{
		return false;
	}

	match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	*dest = strtoll(match, NULL, 10);

	if (*dest == 0 && errno == EINVAL)
	{
		log_error("Failed to parse number \"%s\": %s", match, strerror(errno));
		free(match);
		return false;
	}

	free(match);
	return true;
}


/*
 * intToString converts an int to an IntString, which contains a decimal string
 * representation of the integer.
 */
IntString
intToString(int64_t number)
{
	IntString intString;

	intString.intValue = number;

	/*
	 * Explanation of IGNORE-BANNED:
	 * snprintf is safe here, we never write beyond the buffer,
	 * INTSTRING_MAX_DIGITS is large enough to contain 64 bit
	 * decimal number with minus sign.
	 */
	snprintf(intString.strValue, INTSTRING_MAX_DIGITS, "%" PRId64, number); /*  IGNORE-BANNED */

	return intString;
}


/*
 * Parse a State message from the monitor.
 *
 *  S:catchingup:secondary:7.default:0:3:9.localhost:6020
 *
 *  S:<state>:<state>:<len.formationId>:groupId:nodeId:<len.nodeName>:nodePort
 *
 * We trust the input to a degree, so we don't check everything that could go
 * wrong. We might want to revisit that choice later.
 */
#define FIELD_SEP ':'
#define STRLEN_SEP '.'

bool
parse_state_notification_message(StateNotification *notification)
{
	char *ptr = (char *) notification->message;
	char *col = NULL;

	/* 10 is the amount of character S, colons (:) and dots (.) */
	if (ptr == NULL || strlen(ptr) < 10 || *ptr != 'S')
	{
		log_warn("Failed to parse notification \"%s\"", notification->message);
		return false;
	}

	/* skip S: */
	ptr++; ptr++;

	/* read the states */
	col = strchr(ptr, FIELD_SEP);
	*col = '\0';

	notification->reportedState = NodeStateFromString(ptr);

	ptr = ++col;

	col = strchr(ptr, FIELD_SEP);
	*col = '\0';

	notification->goalState = NodeStateFromString(ptr);

	ptr = ++col;

	/* read the formationId */
	ptr += read_length_delimited_string_at(ptr,
										   notification->formationId,
										   NAMEDATALEN);

	/* read the groupId and nodeId */
	col = strchr(ptr, FIELD_SEP);
	*col = '\0';
	notification->groupId = atoi(ptr);
	ptr = ++col;

	col = strchr(ptr, FIELD_SEP);
	*col = '\0';
	notification->nodeId = atoi(ptr);
	ptr = ++col;

	/* read the nodeName, then move past it */
	ptr += read_length_delimited_string_at(ptr,
										   notification->nodeName,
										   NAMEDATALEN);

	/* read the nodePort */
	notification->nodePort = atoi(ptr);

	return true;
}


/*
 * read_length_delimited_string_at reads a length delimited string such as
 * 12.abcdefghijkl found at given ptr.
 */
static int
read_length_delimited_string_at(const char *ptr, char *buffer, int size)
{
	char *col = NULL;
	int len = 0;

	col = strchr(ptr, STRLEN_SEP);
	*col = '\0';
	len = atoi(ptr);

	if (len < size)
	{
		/* advance col past the separator */
		strlcpy(buffer, ++col, len+1);
	}

	/* col - ptr is the length of the digits plus the separator  */
	return (col - ptr) + len + 1;
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

	do
	{
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
	}
	while (currentLine != NULL && *currentLine != '\0' && lineNumber < size);

	return lineNumber;
}
