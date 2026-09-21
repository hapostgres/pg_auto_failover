/*
 * src/bin/pg_walsender/cmd_fetch_file.c
 *   See cmd_fetch_file.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
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
 * to walk out of route->path.
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


/* CopyData messages of at most this many bytes, like a real walsender's */
#define WS_FETCH_CHUNK_SIZE (128 * 1024)


void
cmd_fetch_file(int sock, const WsRoute *route, const char *filename)
{
	if (!filename_is_safe(filename))
	{
		log_warn("Rejecting FETCH_FILE request for unsafe filename \"%s\"",
				 filename);
		ws_send_error_response(sock, "22023", "invalid filename");
		return;
	}

	if (route == NULL || route->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/%s", route->path, filename);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		log_info("FETCH_FILE: \"%s\" not found under \"%s\"",
				 filename, route->path);
		ws_send_error_response(sock, "58P01", "requested file not found");
		return;
	}

	bool ok = ws_send_copy_out_response(sock, 0);

	for (long offset = 0; ok && offset < fileSize; offset += WS_FETCH_CHUNK_SIZE)
	{
		long chunk = Min(WS_FETCH_CHUNK_SIZE, fileSize - offset);

		ok = ws_send_copy_data(sock, contents + offset, (int32_t) chunk);
	}

	ok = ok && ws_send_copy_done(sock) && ws_send_command_complete(sock, "FETCH_FILE");

	if (!ok)
	{
		log_error("Failed to send \"%s\" (%ld bytes) to a FETCH_FILE client",
				  filename, fileSize);
	}
	else
	{
		log_info("FETCH_FILE: served \"%s\" (%ld bytes) from \"%s\"",
				 filename, fileSize, route->path);
	}

	free(contents);
}
