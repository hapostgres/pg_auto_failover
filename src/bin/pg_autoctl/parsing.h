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

typedef struct IntString
{
	int64_t intValue;

	/* maximum decimal int64 length with minus and NUL */
	char strValue[21];
} IntString;


char * regexp_first_match(const char *string, const char *re);
char * parse_version_number(const char *version_string);

bool parse_controldata(PostgresControlData *pgControlData,
					   const char *control_data_string);

IntString intToString(int64_t number);

bool parse_state_notification_message(StateNotification *notification);


#endif /* PARSING_H */
