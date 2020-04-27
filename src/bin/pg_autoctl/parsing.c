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
#include "string_utils.h"

static bool parse_controldata_field_uint32(const char *controlDataString,
										   const char *fieldName,
										   uint32_t *dest);

static bool parse_controldata_field_uint64(const char *controlDataString,
										   const char *fieldName,
										   uint64_t *dest);

static int read_length_delimited_string_at(const char *ptr,
										   char *buffer, int size);

static bool parse_bool_with_len(const char *value, size_t len, bool *result);

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
		if (result == NULL)
		{
			log_error("Failed to allocate memory, probably because it's all used");
			return NULL;
		}

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

	sformat(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName);
	match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	if (!stringToUInt32(match, dest))
	{
		log_error("Failed to parse number \"%s\": %m", match);
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

	sformat(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName);
	match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	if (!stringToUInt64(match, dest))
	{
		log_error("Failed to parse number \"%s\": %m", match);
		free(match);
		return false;
	}

	free(match);
	return true;
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
	ptr++;
	ptr++;

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
	if (!stringToInt(ptr, &notification->groupId))
	{
		log_warn("Failed to parse group id \"%s\"", ptr);
		return false;
	}
	ptr = ++col;

	col = strchr(ptr, FIELD_SEP);
	*col = '\0';

	if (!stringToInt(ptr, &notification->nodeId))
	{
		log_warn("Failed to parse node id \"%s\"", ptr);
		return false;
	}
	ptr = ++col;

	/* read the nodeName, then move past it */
	ptr += read_length_delimited_string_at(ptr,
										   notification->nodeName,
										   NAMEDATALEN);

	/* read the nodeHost, then move past it */
	ptr += read_length_delimited_string_at(ptr,
										   notification->hostName,
										   NAMEDATALEN);

	/* read the nodePort */
	if (!stringToInt(ptr, &notification->nodePort))
	{
		log_warn("Failed to parse node port id \"%s\"", ptr);
		return false;
	}

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
	if (!stringToInt(ptr, &len))
	{
		/* that's a BUG really */
		log_error("Failed to read length of string in notitication message: %s",
				  ptr);
		return 0;
	}

	if (len < size)
	{
		/* advance col past the separator */
		strlcpy(buffer, ++col, len + 1);
	}

	/* col - ptr is the length of the digits plus the separator  */
	return (col - ptr) + len + 1;
}


/*
 * Try to interpret value as boolean value.  Valid values are: true,
 * false, yes, no, on, off, 1, 0; as well as unique prefixes thereof.
 * If the string parses okay, return true, else false.
 * If okay and result is not NULL, return the value in *result.
 *
 * Copied from PostgreSQL sources
 * file : src/backend/utils/adt/bool.c
 */
static bool
parse_bool_with_len(const char *value, size_t len, bool *result)
{
	switch (*value)
	{
		case 't':
		case 'T':
		{
			if (pg_strncasecmp(value, "true", len) == 0)
			{
				if (result)
				{
					*result = true;
				}
				return true;
			}
			break;
		}

		case 'f':
		case 'F':
		{
			if (pg_strncasecmp(value, "false", len) == 0)
			{
				if (result)
				{
					*result = false;
				}
				return true;
			}
			break;
		}

		case 'y':
		case 'Y':
		{
			if (pg_strncasecmp(value, "yes", len) == 0)
			{
				if (result)
				{
					*result = true;
				}
				return true;
			}
			break;
		}

		case 'n':
		case 'N':
		{
			if (pg_strncasecmp(value, "no", len) == 0)
			{
				if (result)
				{
					*result = false;
				}
				return true;
			}
			break;
		}

		case 'o':
		case 'O':
		{
			/* 'o' is not unique enough */
			if (pg_strncasecmp(value, "on", (len > 2 ? len : 2)) == 0)
			{
				if (result)
				{
					*result = true;
				}
				return true;
			}
			else if (pg_strncasecmp(value, "off", (len > 2 ? len : 2)) == 0)
			{
				if (result)
				{
					*result = false;
				}
				return true;
			}
			break;
		}

		case '1':
		{
			if (len == 1)
			{
				if (result)
				{
					*result = true;
				}
				return true;
			}
			break;
		}

		case '0':
		{
			if (len == 1)
			{
				if (result)
				{
					*result = false;
				}
				return true;
			}
			break;
		}

		default:
		{
			break;
		}
	}

	if (result)
	{
		*result = false;        /* suppress compiler warning */
	}
	return false;
}


/*
 * parse_bool parses boolean text value (true/false/on/off/yes/no/1/0) and
 * puts the boolean value back in the result field if it is not NULL.
 * The function returns true on successful parse, returns false if any parse
 * error is encountered.
 */
bool
parse_bool(const char *value, bool *result)
{
	return parse_bool_with_len(value, strlen(value), result);
}


/*
 * parse_pguri_info_key_vals decomposes elements of a Postgres connection
 * string (URI) into separate arrays of keywords and values as expected by
 * PQconnectdbParams.
 */
bool
parse_pguri_info_key_vals(const char *pguri,
						  KeyVal *overrides,
						  URIParams *uriParameters)
{
	char *errmsg;
	PQconninfoOption *conninfo, *option;

	bool foundHost = false;
	bool foundUser = false;
	bool foundPort = false;
	bool foundDBName = false;

	int paramIndex = 0;

	conninfo = PQconninfoParse(pguri, &errmsg);
	if (conninfo == NULL)
	{
		log_error("Failed to parse pguri \"%s\": %s", pguri, errmsg);
		PQfreemem(errmsg);
		return false;
	}

	for (option = conninfo; option->keyword != NULL; option++)
	{
		char *value = NULL;
		int ovIndex = 0;

		/*
		 * If the keyword is in our overrides array, use the value from the
		 * override values. Yeah that's O(n*m) but here m is expected to be
		 * something very small, like 3 (typically: sslmode, sslrootcert,
		 * sslcrl).
		 */
		for (ovIndex = 0; ovIndex < overrides->count; ovIndex++)
		{
			if (strcmp(overrides->keywords[ovIndex], option->keyword) == 0)
			{
				value = overrides->values[ovIndex];
			}
		}

		/* not found in the override, keep the original, or skip */
		if (value == NULL)
		{
			if (option->val == NULL || strcmp(option->val, "") == 0)
			{
				continue;
			}
			else
			{
				value = option->val;
			}
		}

		if (strcmp(option->keyword, "host") == 0 ||
			strcmp(option->keyword, "hostaddr") == 0)
		{
			foundHost = true;
			strlcpy(uriParameters->hostname, option->val, MAXCONNINFO);
		}
		else if (strcmp(option->keyword, "port") == 0)
		{
			foundPort = true;
			strlcpy(uriParameters->port, option->val, MAXCONNINFO);
		}
		else if (strcmp(option->keyword, "user") == 0)
		{
			foundUser = true;
			strlcpy(uriParameters->username, option->val, MAXCONNINFO);
		}
		else if (strcmp(option->keyword, "dbname") == 0)
		{
			foundDBName = true;
			strlcpy(uriParameters->dbname, option->val, MAXCONNINFO);
		}
		else if (!IS_EMPTY_STRING_BUFFER(value))
		{
			/* make a copy in our key/val arrays */
			strlcpy(uriParameters->parameters.keywords[paramIndex],
					option->keyword,
					MAXCONNINFO);

			strlcpy(uriParameters->parameters.values[paramIndex],
					value,
					MAXCONNINFO);

			++uriParameters->parameters.count;
			++paramIndex;
		}
	}

	/*
	 * Display an error message per missing field, and only then return false
	 * if we're missing any one of those.
	 */
	if (!foundHost)
	{
		log_error("Failed to find hostname in the pguri \"%s\"", pguri);
	}

	if (!foundPort)
	{
		log_error("Failed to find port in the pguri \"%s\"", pguri);
	}

	if (!foundUser)
	{
		log_error("Failed to find username in the pguri \"%s\"", pguri);
	}

	if (!foundDBName)
	{
		log_error("Failed to find dbname in the pguri \"%s\"", pguri);
	}

	return foundHost && foundPort && foundUser && foundDBName;
}


/*
 * buildPostgresURIfromPieces builds a Postgres connection string from keywords
 * and values, in a user friendly way. The pguri parameter should point to a
 * memory area that has been allocated by the caller and has at least
 * MAXCONNINFO bytes.
 */
bool
buildPostgresURIfromPieces(URIParams *uriParams, char *pguri)
{
	int index = 0;

	sformat(pguri, MAXCONNINFO,
			"postgres://%s@%s:%s/%s?",
			uriParams->username,
			uriParams->hostname,
			uriParams->port,
			uriParams->dbname);

	for (index = 0; index < uriParams->parameters.count; index++)
	{
		if (index == 0)
		{
			sformat(pguri, MAXCONNINFO,
					"%s%s=%s",
					pguri,
					uriParams->parameters.keywords[index],
					uriParams->parameters.values[index]);
		}
		else
		{
			sformat(pguri, MAXCONNINFO,
					"%s&%s=%s",
					pguri,
					uriParams->parameters.keywords[index],
					uriParams->parameters.values[index]);
		}
	}

	return true;
}
