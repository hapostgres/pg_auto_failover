/*
 * src/bin/pg_walsender/cmd_fetch_file.c
 *   See cmd_fetch_file.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cmd_fetch_file.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"

#define WS_FETCH_FILENAME_MAX 256


/*
 * filename_is_safe rejects anything that isn't a bare filename: no path
 * separators, no leading dot (rules out "." / ".." / hidden files), not
 * empty. WAL segment names and ".history" files are both plain
 * [0-9A-F.history]-shaped basenames, never nested paths, so this is not a
 * meaningful restriction for real callers -- only for a hostile one trying
 * to walk out of walcacheDir.
 */
static bool
filename_is_safe(const char *filename)
{
	if (filename[0] == '\0' || filename[0] == '.')
	{
		return false;
	}

	if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL)
	{
		return false;
	}

	return true;
}


void
cmd_fetch_file(int sock, const WsRoute *route)
{
	if (!ws_send_authentication_ok(sock))
	{
		return;
	}

	char filename[WS_FETCH_FILENAME_MAX];

	if (!ws_read_line(sock, filename, sizeof(filename)))
	{
		ws_send_error_response(sock, "08P01",
							   "expected a single filename line after "
							   "authentication");
		return;
	}

	if (!filename_is_safe(filename))
	{
		log_warn("Rejecting FETCH_FILE request for unsafe filename \"%s\"",
				 filename);
		ws_send_error_response(sock, "22023", "invalid filename");
		return;
	}

	if (route == NULL || route->walcacheDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	char path[MAXPGPATH];

	snprintf(path, sizeof(path), "%s/%s", route->walcacheDir, filename);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		log_info("FETCH_FILE: \"%s\" not found under \"%s\"",
				 filename, route->walcacheDir);
		ws_send_error_response(sock, "58P01", "requested file not found");
		return;
	}

	if (!ws_send_copy_data(sock, contents, (int32_t) fileSize))
	{
		log_error("Failed to send \"%s\" (%ld bytes) to a FETCH_FILE client",
				  filename, fileSize);
	}
	else
	{
		log_info("FETCH_FILE: served \"%s\" (%ld bytes) from \"%s\"",
				 filename, fileSize, route->walcacheDir);
	}

	free(contents);
}
