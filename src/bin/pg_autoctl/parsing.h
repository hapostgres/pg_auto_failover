/*
 * src/bin/pg_autoctl/parsing.c
 *   API for parsing the output of some PostgreSQL server commands.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef PARSING_H
#define PARSING_H

#include <stdbool.h>

#include "monitor.h"
#include "pgctl.h"

char * regexp_first_match(const char *string, const char *re);
char * parse_version_number(const char *version_string);

bool parse_controldata(PostgresControlData *pgControlData,
					   const char *control_data_string);

bool parse_state_notification_message(StateNotification *notification);

bool parse_bool(const char *value, bool *result);

#define boolToString(value) (value) ? "true" : "false"

typedef struct KeyVal
{
	int count;
	char keywords[64][MAXCONNINFO];
	char values[64][MAXCONNINFO];
} KeyVal;

bool parse_pguri_info_key_vals(const char *pguri,
							   KeyVal *overrides,
							   char *username,
							   char *hostname,
							   char *port,
							   char *dbname,
							   KeyVal *uriParameters);

bool buildPostgresURIfromPieces(KeyVal *uriParams,
								const char *username,
								const char *hostname,
								const char *port,
								const char *dbname,
								char *pguri);

#endif /* PARSING_H */
