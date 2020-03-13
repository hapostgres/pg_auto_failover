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

#define boolToString(value) (value)?"true":"false"

int splitLines(char *errorMessage, char **linesArray, int size);


#endif /* PARSING_H */
