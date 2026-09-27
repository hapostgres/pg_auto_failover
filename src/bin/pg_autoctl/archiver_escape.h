/*
 * src/bin/pg_autoctl/archiver_escape.h
 *   Quoting and validation helpers for values the archiver interpolates
 *   into conninfo strings, shell-run commands, line-oriented files and
 *   Postgres array literals.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef ARCHIVER_ESCAPE_H
#define ARCHIVER_ESCAPE_H

#include <stdbool.h>
#include <stddef.h>

#include "postgres_fe.h"
#include "pqexpbuffer.h"

/* true when value has no control character (newline, CR, tab, ...) */
bool archiver_value_is_single_line(const char *value);

/* true when value is safe inside a double-quoted shell word in a GUC string */
bool archiver_path_is_shell_safe(const char *path);

/* append value to buffer as a libpq conninfo single-quoted value */
void archiver_append_conninfo_value(PQExpBuffer buffer, const char *value);

/* append value to buffer as a double-quoted Postgres array element */
void archiver_append_array_element(PQExpBuffer buffer, const char *value);

/*
 * format "host=.. port=.. user=.. dbname=.. application_name=.." with every
 * value quoted; false when it does not fit in dest
 */
bool archiver_format_conninfo(char *dest, size_t destSize,
							  const char *host, int port, const char *user,
							  const char *dbname, const char *applicationName);

#endif                          /* ARCHIVER_ESCAPE_H */
