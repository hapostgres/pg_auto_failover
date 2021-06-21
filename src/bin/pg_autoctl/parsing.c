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

#include "parson.h"

#include "log.h"
#include "nodestate_utils.h"
#include "parsing.h"
#include "string_utils.h"

static bool parse_controldata_field_dbstate(const char *controlDataString,
											DBState *state);

static bool parse_controldata_field_uint32(const char *controlDataString,
										   const char *fieldName,
										   uint32_t *dest);

static bool parse_controldata_field_uint64(const char *controlDataString,
										   const char *fieldName,
										   uint64_t *dest);

static bool parse_controldata_field_lsn(const char *controlDataString,
										const char *fieldName,
										char lsn[]);

static bool parse_bool_with_len(const char *value, size_t len, bool *result);

static int nodeAddressCmpByNodeId(const void *a, const void *b);

#define RE_MATCH_COUNT 10


/*
 * Simple Regexp matching that returns the first matching element.
 */
char *
regexp_first_match(const char *string, const char *regex)
{
	regex_t compiledRegex;

	regmatch_t m[RE_MATCH_COUNT];

	if (string == NULL)
	{
		return NULL;
	}

	int status = regcomp(&compiledRegex, regex, REG_EXTENDED | REG_NEWLINE);

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
	int matchStatus = regexec(&compiledRegex, string, RE_MATCH_COUNT, m, 0);
	regfree(&compiledRegex);

	/* We're interested into 1. re matches 2. captured at least one group */
	if (matchStatus != 0 || m[0].rm_so == -1 || m[1].rm_so == -1)
	{
		return NULL;
	}
	else
	{
		regoff_t start = m[1].rm_so;
		regoff_t finish = m[1].rm_eo;
		int length = finish - start + 1;
		char *result = (char *) malloc(length * sizeof(char));

		if (result == NULL)
		{
			log_error(ALLOCATION_FAILED_ERROR);
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
bool
parse_version_number(const char *version_string,
					 char *pg_version_string,
					 size_t size,
					 int *pg_version)
{
	char *match = regexp_first_match(version_string, "([0-9.]+)");

	if (match == NULL)
	{
		log_error("Failed to parse Postgres version number \"%s\"",
				  version_string);
		return false;
	}

	/* first, copy the version number in our expected result string buffer */
	strlcpy(pg_version_string, match, size);

	if (!parse_pg_version_string(pg_version_string, pg_version))
	{
		/* errors have already been logged */
		free(match);
		return false;
	}

	free(match);
	return true;
}


/*
 * parse_dotted_version_string parses a major.minor dotted version string such
 * as "12.6" into a single number in the same format as the pg_control_version,
 * such as 1206.
 */
bool
parse_dotted_version_string(const char *pg_version_string, int *pg_version)
{
	/* now, parse the numbers into an integer, ala pg_control_version */
	bool dotFound = false;
	char major[INTSTRING_MAX_DIGITS] = { 0 };
	char minor[INTSTRING_MAX_DIGITS] = { 0 };

	int majorIdx = 0;
	int minorIdx = 0;

	if (pg_version_string == NULL)
	{
		log_debug("BUG: parse_pg_version_string got NULL");
		return false;
	}

	for (int i = 0; pg_version_string[i] != '\0'; i++)
	{
		if (pg_version_string[i] == '.')
		{
			if (dotFound)
			{
				log_error("Failed to parse Postgres version number \"%s\"",
						  pg_version_string);
				return false;
			}

			dotFound = true;
			continue;
		}

		if (dotFound)
		{
			minor[minorIdx++] = pg_version_string[i];
		}
		else
		{
			major[majorIdx++] = pg_version_string[i];
		}
	}

	/* Postgres alpha/beta versions report version "14" instead of "14.0" */
	if (!dotFound)
	{
		strlcpy(minor, "0", INTSTRING_MAX_DIGITS);
	}

	int maj = 0;
	int min = 0;

	if (!stringToInt(major, &maj) ||
		!stringToInt(minor, &min))
	{
		log_error("Failed to parse Postgres version number \"%s\"",
				  pg_version_string);
		return false;
	}

	/* transform "12.6" into 1206, that is 12 * 100 + 6 */
	*pg_version = (maj * 100) + min;

	return true;
}


/*
 * parse_pg_version_string parses a Postgres version string such as "12.6" into
 * a single number in the same format as the pg_control_version, such as 1206.
 */
bool
parse_pg_version_string(const char *pg_version_string, int *pg_version)
{
	return parse_dotted_version_string(pg_version_string, pg_version);
}


/*
 * parse_pgaf_version_string parses a pg_auto_failover version string such as
 * "1.4" into a single number in the same format as the pg_control_version,
 * such as 104.
 */
bool
parse_pgaf_extension_version_string(const char *version_string, int *version)
{
	return parse_dotted_version_string(version_string, version);
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
	if (!parse_controldata_field_dbstate(control_data_string,
										 &(pgControlData->state)) ||

		!parse_controldata_field_uint32(control_data_string,
										"pg_control version number",
										&(pgControlData->pg_control_version)) ||

		!parse_controldata_field_uint32(control_data_string,
										"Catalog version number",
										&(pgControlData->catalog_version_no)) ||

		!parse_controldata_field_uint64(control_data_string,
										"Database system identifier",
										&(pgControlData->system_identifier)) ||

		!parse_controldata_field_lsn(control_data_string,
									 "Latest checkpoint location",
									 pgControlData->latestCheckpointLSN) ||

		!parse_controldata_field_uint32(control_data_string,
										"Latest checkpoint's TimeLineID",
										&(pgControlData->timeline_id)))
	{
		log_error("Failed to parse pg_controldata output");
		return false;
	}
	return true;
}


#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/*
 * parse_controldata_field_dbstate matches pg_controldata output for Database
 * cluster state and fills in the value string as an enum value.
 */
static bool
parse_controldata_field_dbstate(const char *controlDataString, DBState *state)
{
	char regex[BUFSIZE] = { 0 };

	sformat(regex, BUFSIZE, "Database cluster state: *(.*)$");

	char *match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	if (streq(match, "starting up"))
	{
		*state = DB_STARTUP;
	}
	else if (streq(match, "shut down"))
	{
		*state = DB_SHUTDOWNED;
	}
	else if (streq(match, "shut down in recovery"))
	{
		*state = DB_SHUTDOWNED_IN_RECOVERY;
	}
	else if (streq(match, "shutting down"))
	{
		*state = DB_SHUTDOWNING;
	}
	else if (streq(match, "in crash recovery"))
	{
		*state = DB_IN_CRASH_RECOVERY;
	}
	else if (streq(match, "in archive recovery"))
	{
		*state = DB_IN_ARCHIVE_RECOVERY;
	}
	else if (streq(match, "in production"))
	{
		*state = DB_IN_PRODUCTION;
	}
	else
	{
		log_error("Failed to parse database cluster state \"%s\"", match);
		free(match);
		return false;
	}

	free(match);
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

	sformat(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName);
	char *match = regexp_first_match(controlDataString, regex);

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

	sformat(regex, BUFSIZE, "^%s: *([0-9]+)$", fieldName);
	char *match = regexp_first_match(controlDataString, regex);

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
 * parse_controldata_field_lsn matches pg_controldata output for a field name
 * and gets its value as a string, in an area that must be pre-allocated with
 * at least PG_LSN_MAXLENGTH bytes.
 */
static bool
parse_controldata_field_lsn(const char *controlDataString,
							const char *fieldName,
							char lsn[])
{
	char regex[BUFSIZE];

	sformat(regex, BUFSIZE, "^%s: *([0-9A-F]+/[0-9A-F]+)$", fieldName);
	char *match = regexp_first_match(controlDataString, regex);

	if (match == NULL)
	{
		return false;
	}

	strlcpy(lsn, match, PG_LSN_MAXLENGTH);

	free(match);
	return true;
}


/*
 * parse_notification_message parses pgautofailover state change notifications,
 * which are sent in the JSON format.
 */
bool
parse_state_notification_message(CurrentNodeState *nodeState,
								 const char *message)
{
	JSON_Value *json = json_parse_string(message);
	JSON_Object *jsobj = json_value_get_object(json);

	log_trace("parse_state_notification_message: %s", message);

	if (json_type(json) != JSONObject)
	{
		log_error("Failed to parse JSON notification message: \"%s\"", message);
		json_value_free(json);
		return false;
	}

	char *str = (char *) json_object_get_string(jsobj, "type");

	if (strcmp(str, "state") != 0)
	{
		log_error("Failed to parse JSOBJ notification state message: "
				  "jsobj object type is not \"state\" as expected");
		json_value_free(json);
		return false;
	}

	str = (char *) json_object_get_string(jsobj, "formation");

	if (str == NULL)
	{
		log_error("Failed to parse formation in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}
	strlcpy(nodeState->formation, str, sizeof(nodeState->formation));

	double number = json_object_get_number(jsobj, "groupId");
	nodeState->groupId = (int) number;

	number = json_object_get_number(jsobj, "nodeId");
	nodeState->node.nodeId = (int) number;

	str = (char *) json_object_get_string(jsobj, "name");

	if (str == NULL)
	{
		log_error("Failed to parse node name in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}
	strlcpy(nodeState->node.name, str, sizeof(nodeState->node.name));

	str = (char *) json_object_get_string(jsobj, "host");

	if (str == NULL)
	{
		log_error("Failed to parse node host in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}
	strlcpy(nodeState->node.host, str, sizeof(nodeState->node.host));

	number = json_object_get_number(jsobj, "port");
	nodeState->node.port = (int) number;

	str = (char *) json_object_get_string(jsobj, "reportedState");

	if (str == NULL)
	{
		log_error("Failed to parse reportedState in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}
	nodeState->reportedState = NodeStateFromString(str);

	str = (char *) json_object_get_string(jsobj, "goalState");

	if (str == NULL)
	{
		log_error("Failed to parse goalState in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}
	nodeState->goalState = NodeStateFromString(str);

	str = (char *) json_object_get_string(jsobj, "health");

	if (streq(str, "unknown"))
	{
		nodeState->health = -1;
	}
	else if (streq(str, "bad"))
	{
		nodeState->health = 0;
	}
	else if (streq(str, "good"))
	{
		nodeState->health = 1;
	}
	else
	{
		log_error("Failed to parse health in JSON "
				  "notification message \"%s\"", message);
		json_value_free(json);
		return false;
	}

	json_value_free(json);
	return true;
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
						  URIParams *uriParameters,
						  bool checkForCompleteURI)
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

	PQconninfoFree(conninfo);

	/*
	 * Display an error message per missing field, and only then return false
	 * if we're missing any one of those.
	 */
	if (checkForCompleteURI)
	{
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
	else
	{
		return true;
	}
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


/*
 * parse_pguri_ssl_settings parses SSL settings from a Postgres connection
 * string. Given the following connection string
 *
 *  "postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer"
 *
 * we then have an ssl->active = 1, ssl->sslMode = SSL_MODE_PREFER, etc.
 */
bool
parse_pguri_ssl_settings(const char *pguri, SSLOptions *ssl)
{
	URIParams params = { 0 };
	KeyVal overrides = { 0 };

	bool checkForCompleteURI = true;

	/* initialize SSL Params values */
	if (!parse_pguri_info_key_vals(pguri,
								   &overrides,
								   &params,
								   checkForCompleteURI))
	{
		/* errors have already been logged */
		return false;
	}

	for (int index = 0; index < params.parameters.count; index++)
	{
		char *key = params.parameters.keywords[index];
		char *val = params.parameters.values[index];

		if (streq(key, "sslmode"))
		{
			ssl->sslMode = pgsetup_parse_sslmode(val);
			strlcpy(ssl->sslModeStr, val, sizeof(ssl->sslModeStr));

			if (ssl->sslMode > SSL_MODE_DISABLE)
			{
				ssl->active = true;
			}
		}
		else if (streq(key, "sslrootcert"))
		{
			strlcpy(ssl->caFile, val, sizeof(ssl->caFile));
		}
		else if (streq(key, "sslcrl"))
		{
			strlcpy(ssl->crlFile, val, sizeof(ssl->crlFile));
		}
		else if (streq(key, "sslcert"))
		{
			strlcpy(ssl->serverCert, val, sizeof(ssl->serverCert));
		}
		else if (streq(key, "sslkey"))
		{
			strlcpy(ssl->serverKey, val, sizeof(ssl->serverKey));
		}
	}

	/* cook-in defaults when the parsed URL contains no SSL settings */
	if (ssl->sslMode == SSL_MODE_UNKNOWN)
	{
		ssl->active = true;
		ssl->sslMode = SSL_MODE_PREFER;
		strlcpy(ssl->sslModeStr,
				pgsetup_sslmode_to_string(ssl->sslMode),
				sizeof(ssl->sslModeStr));
	}

	return true;
}


/*
 * nodeAddressCmpByNodeId sorts two given nodeAddress by comparing their
 * nodeId. We use this function to be able to pg_qsort() an array of nodes,
 * such as when parsing from a JSON file.
 */
static int
nodeAddressCmpByNodeId(const void *a, const void *b)
{
	NodeAddress *nodeA = (NodeAddress *) a;
	NodeAddress *nodeB = (NodeAddress *) b;

	return nodeA->nodeId - nodeB->nodeId;
}


/*
 * parseLSN is based on the Postgres code for pg_lsn_in_internal found at
 * src/backend/utils/adt/pg_lsn.c in the Postgres source repository. In the
 * pg_auto_failover context we don't need to typedef uint64 XLogRecPtr; so we
 * just use uint64_t internally.
 */
#define MAXPG_LSNCOMPONENT 8

bool
parseLSN(const char *str, uint64_t *lsn)
{
	int len1,
		len2;
	uint32 id,
		   off;

	/* Sanity check input format. */
	len1 = strspn(str, "0123456789abcdefABCDEF");
	if (len1 < 1 || len1 > MAXPG_LSNCOMPONENT || str[len1] != '/')
	{
		return false;
	}

	len2 = strspn(str + len1 + 1, "0123456789abcdefABCDEF");
	if (len2 < 1 || len2 > MAXPG_LSNCOMPONENT || str[len1 + 1 + len2] != '\0')
	{
		return false;
	}

	/* Decode result. */
	id = (uint32) strtoul(str, NULL, 16);
	off = (uint32) strtoul(str + len1 + 1, NULL, 16);
	*lsn = ((uint64) id << 32) | off;

	return true;
}


/*
 * parseNodesArrayFromFile parses a Nodes Array from a JSON file, that contains
 * an array of JSON object with the following properties: node_id, node_lsn,
 * node_host, node_name, node_port, and potentially node_is_primary.
 */
bool
parseNodesArray(const char *nodesJSON,
				NodeAddressArray *nodesArray,
				int64_t nodeId)
{
	JSON_Value *template =
		json_parse_string("[{"
						  "\"node_id\": 0,"
						  "\"node_lsn\": \"\","
						  "\"node_name\": \"\","
						  "\"node_host\": \"\","
						  "\"node_port\": 0,"
						  "\"node_is_primary\": false"
						  "}]");
	int nodesArrayIndex = 0;
	int primaryCount = 0;

	JSON_Value *json = json_parse_string(nodesJSON);

	/* validate the JSON input as an array of object with required fields */
	if (json_validate(template, json) == JSONFailure)
	{
		log_error("Failed to parse nodes array which is expected "
				  "to contain a JSON Array of Objects with properties "
				  "[{node_id:number, node_name:string, "
				  "node_host:string, node_port:number, node_lsn:string, "
				  "node_is_primary:boolean}, ...]");
		json_value_free(template);
		json_value_free(json);
		return false;
	}

	JSON_Array *jsArray = json_value_get_array(json);
	int len = json_array_get_count(jsArray);

	if (NODE_ARRAY_MAX_COUNT < len)
	{
		log_error("Failed to parse nodes array which contains "
				  "%d nodes: pg_autoctl supports up to %d nodes",
				  len,
				  NODE_ARRAY_MAX_COUNT);
		json_value_free(template);
		json_value_free(json);
		return false;
	}

	nodesArray->count = len;

	for (int i = 0; i < len; i++)
	{
		NodeAddress *node = &(nodesArray->nodes[nodesArrayIndex]);
		JSON_Object *jsObj = json_array_get_object(jsArray, i);

		int jsNodeId = (int) json_object_get_number(jsObj, "node_id");
		uint64_t lsn = 0;

		/* we install the keeper.otherNodes array, so skip ourselves */
		if (jsNodeId == nodeId)
		{
			--(nodesArray->count);
			continue;
		}

		node->nodeId = jsNodeId;

		strlcpy(node->name,
				json_object_get_string(jsObj, "node_name"),
				sizeof(node->name));

		strlcpy(node->host,
				json_object_get_string(jsObj, "node_host"),
				sizeof(node->host));

		node->port = (int) json_object_get_number(jsObj, "node_port");

		strlcpy(node->lsn,
				json_object_get_string(jsObj, "node_lsn"),
				sizeof(node->lsn));

		if (!parseLSN(node->lsn, &lsn))
		{
			log_error("Failed to parse nodes array LSN value \"%s\"", node->lsn);
			json_value_free(template);
			json_value_free(json);
			return false;
		}

		node->isPrimary = json_object_get_boolean(jsObj, "node_is_primary");

		if (node->isPrimary)
		{
			++primaryCount;

			if (primaryCount > 1)
			{
				log_error("Failed to parse nodes array: more than one node "
						  "is listed with \"node_is_primary\" true.");
				json_value_free(template);
				json_value_free(json);
				return false;
			}
		}

		++nodesArrayIndex;
	}

	json_value_free(template);
	json_value_free(json);

	/* now ensure the array is sorted by nodeId */
	(void) pg_qsort(nodesArray->nodes,
					nodesArray->count,
					sizeof(NodeAddress),
					nodeAddressCmpByNodeId);

	/* check that every node id is unique in our array */
	for (int i = 0; i < (nodesArray->count - 1); i++)
	{
		int currentNodeId = nodesArray->nodes[i].nodeId;
		int nextNodeId = nodesArray->nodes[i + 1].nodeId;

		if (currentNodeId == nextNodeId)
		{
			log_error("Failed to parse nodes array: more than one node "
					  "is listed with the same nodeId %d",
					  currentNodeId);
			return false;
		}
	}

	return true;
}


/*
 * uri_contains_password takes a Postgres connection string and checks to see
 * if it contains a parameter called password. Returns true if a password
 * keyword is present in the connection string.
 */
static bool
uri_contains_password(const char *pguri)
{
	char *errmsg;
	PQconninfoOption *conninfo, *option;

	conninfo = PQconninfoParse(pguri, &errmsg);
	if (conninfo == NULL)
	{
		log_error("Failed to parse pguri: %s", errmsg);

		PQfreemem(errmsg);
		return false;
	}

	/*
	 * Look for a populated password connection parameter
	 */
	for (option = conninfo; option->keyword != NULL; option++)
	{
		if (strcmp(option->keyword, "password") == 0 &&
			option->val != NULL &&
			!IS_EMPTY_STRING_BUFFER(option->val))
		{
			PQconninfoFree(conninfo);
			return true;
		}
	}

	PQconninfoFree(conninfo);
	return false;
}


/*
 * parse_and_scrub_connection_string takes a Postgres connection string and
 * populates scrubbedPguri with the password replaced with **** for logging.
 * The scrubbedPguri parameter should point to a memory area that has been
 * allocated by the caller and has at least MAXCONNINFO bytes.
 */
bool
parse_and_scrub_connection_string(const char *pguri, char *scrubbedPguri)
{
	URIParams uriParams = { 0 };
	KeyVal overrides = { 0 };

	if (uri_contains_password(pguri))
	{
		overrides = (KeyVal) {
			.count = 1,
			.keywords = { "password" },
			.values = { "****" }
		};
	}

	bool checkForCompleteURI = false;

	if (!parse_pguri_info_key_vals(pguri,
								   &overrides,
								   &uriParams,
								   checkForCompleteURI))
	{
		return false;
	}

	buildPostgresURIfromPieces(&uriParams, scrubbedPguri);

	return true;
}
