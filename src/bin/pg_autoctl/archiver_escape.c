/*
 * src/bin/pg_autoctl/archiver_escape.c
 *   Quoting and validation helpers for values the archiver interpolates
 *   into conninfo strings, shell-run commands, line-oriented files and
 *   Postgres array literals. See archiver_escape.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "archiver_escape.h"


bool
archiver_value_is_single_line(const char *value)
{
	for (const char *p = value; *p != '\0'; p++)
	{
		if ((unsigned char) *p < 0x20 || *p == 0x7f)
		{
			return false;
		}
	}

	return true;
}


/*
 * A path ends up inside restore_command = '... "<path>/%f" ...', a
 * single-quoted GUC string whose value is then run by a shell, so every
 * character that is special in either layer is rejected outright rather
 * than escaped: there is no legitimate reason for an archiver storage path
 * to contain any of them.
 */
bool
archiver_path_is_shell_safe(const char *path)
{
	return archiver_value_is_single_line(path) &&
		   strpbrk(path, "'\"\\$`%") == NULL;
}


void
archiver_append_conninfo_value(PQExpBuffer buffer, const char *value)
{
	appendPQExpBufferChar(buffer, '\'');

	for (const char *p = value; *p != '\0'; p++)
	{
		if (*p == '\'' || *p == '\\')
		{
			appendPQExpBufferChar(buffer, '\\');
		}
		appendPQExpBufferChar(buffer, *p);
	}

	appendPQExpBufferChar(buffer, '\'');
}


void
archiver_append_array_element(PQExpBuffer buffer, const char *value)
{
	appendPQExpBufferChar(buffer, '"');

	for (const char *p = value; *p != '\0'; p++)
	{
		if (*p == '"' || *p == '\\')
		{
			appendPQExpBufferChar(buffer, '\\');
		}
		appendPQExpBufferChar(buffer, *p);
	}

	appendPQExpBufferChar(buffer, '"');
}


bool
archiver_format_conninfo(char *dest, size_t destSize,
						 const char *host, int port, const char *user,
						 const char *dbname, const char *applicationName)
{
	PQExpBuffer buffer = createPQExpBuffer();

	if (buffer == NULL)
	{
		return false;
	}

	appendPQExpBufferStr(buffer, "host=");
	archiver_append_conninfo_value(buffer, host);
	appendPQExpBuffer(buffer, " port=%d user=", port);
	archiver_append_conninfo_value(buffer, user);
	appendPQExpBufferStr(buffer, " dbname=");
	archiver_append_conninfo_value(buffer, dbname);
	appendPQExpBufferStr(buffer, " application_name=");
	archiver_append_conninfo_value(buffer, applicationName);

	bool ok = !PQExpBufferBroken(buffer) && buffer->len < destSize;

	if (ok)
	{
		memcpy(dest, buffer->data, buffer->len + 1); /* IGNORE-BANNED */
	}

	destroyPQExpBuffer(buffer);

	return ok;
}
