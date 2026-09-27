/*
 * src/bin/pg_walserver/cmd_fetch_file.c
 *   See cmd_fetch_file.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cmd_fetch_file.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "ws_util.h"

/* CopyData messages of at most this many bytes, like a real walsender's */
#define WS_FETCH_CHUNK_SIZE (128 * 1024)


/*
 * is_upper_hex returns true when the first n bytes of s are all uppercase
 * hexadecimal digits ('0'-'9', 'A'-'F') -- the alphabet WAL segment names and
 * timeline history filenames both use.
 */
static bool
is_upper_hex(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F')))
		{
			return false;
		}
	}

	return true;
}


/*
 * is_upper_hex_wal_backup_label recognizes a base backup's own
 * "<24-hex>.<8-hex>.backup" history file name (real Postgres's own
 * XLogFileNameP()-plus-".backup" shape, e.g.
 * "000000010000000000000003.00000028.backup", written by the backend the
 * moment any base backup completes -- BASE_BACKUP or the low-level API,
 * per PostgreSQL's own continuous-archiving contract: that file is
 * archived too, exactly like a WAL segment). The
 * fixed ".backup" suffix and the 24-hex/8-hex shape on either side of the
 * middle '.' are exactly what real Postgres itself always produces, so this
 * checks that shape precisely rather than accepting any "*.backup".
 */
static bool
is_wal_backup_label_name(const char *filename, size_t len)
{
	const char *suffix = ".backup";
	size_t suffixLen = strlen(suffix);

	/* "<24 hex>.<8 hex>" + ".backup" = 24 + 1 + 8 + 7 = 40 */
	if (len != 24 + 1 + 8 + suffixLen)
	{
		return false;
	}

	return is_upper_hex(filename, 24) &&
		   filename[24] == '.' &&
		   is_upper_hex(filename + 25, 8) &&
		   strcmp(filename + 25 + 8, suffix) == 0;
}


/*
 * filename_is_servable is an allow-list, not a filter: only a complete WAL
 * segment "^[0-9A-F]{24}$", a timeline history file
 * "^[0-9A-F]{8}\.history$", or a base backup's own history file
 * "^[0-9A-F]{24}\.[0-9A-F]{8}\.backup$" can be fetched or archived -- what a
 * restore_command asks for on the read side (FETCH_FILE, cmd_fetch_file.c),
 * and what an archive_command may legitimately push on the write side
 * (ARCHIVE_FILE, cmd_archive_file.c) -- real Postgres archives ".backup"
 * files exactly like WAL segments, so both directions need to recognize
 * them, which is why this one function
 * is shared by both cmd_fetch_file.c and cmd_archive_file.c rather than each
 * having its own allow-list. Everything else in the route's directory
 * (archiver-hba.conf, archiver-passwd's neighbours, .slot_* files, the
 * basebackups/ tree, ".partial" segments still being written...) is never
 * served or accepted.
 */
bool
ws_fetch_filename_is_servable(const char *filename)
{
	size_t len = strlen(filename);

	if (len == 24)
	{
		return is_upper_hex(filename, 24);
	}

	if (len == 8 + strlen(".history"))
	{
		return is_upper_hex(filename, 8) &&
			   strcmp(filename + 8, ".history") == 0;
	}

	if (is_wal_backup_label_name(filename, len))
	{
		return true;
	}

	return false;
}


/*
 * cmd_fetch_file implements this project's own FETCH_FILE extension: it
 * validates filename against the servable allow-list, opens it under
 * route->path, and streams it back to the client as a CopyOut of
 * WS_FETCH_CHUNK_SIZE-sized CopyData messages (never loading the whole file,
 * which can be up to a 1 GiB WAL segment), followed by CopyDone and a
 * CommandComplete. Any failure mid-stream sends an ErrorResponse (if nothing
 * has been sent yet) or, once inside CopyOut, sets
 * ws_connection_close_after_command since the protocol cannot be
 * resynchronized from there.
 */
void
cmd_fetch_file(int sock, const WsRoute *route, const char *filename)
{
	if (!ws_fetch_filename_is_servable(filename))
	{
		char safeName[64];

		ws_sanitize_for_log(filename, safeName, sizeof(safeName));
		log_warn("Rejecting FETCH_FILE request for filename \"%s\"", safeName);
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

	int fd = ws_open_served_file(path);

	if (fd < 0)
	{
		log_info("FETCH_FILE: \"%s\" not found under \"%s\"",
				 filename, route->path);
		ws_send_error_response(sock, "58P01", "requested file not found");
		return;
	}

	/*
	 * Stream the file in chunks: nothing is allocated for its whole size
	 * (a WAL segment is up to 1 GiB).
	 */
	char *buffer = (char *) malloc(WS_FETCH_CHUNK_SIZE);

	if (buffer == NULL)
	{
		close(fd);
		ws_send_error_response(sock, "53200", "out of memory");
		return;
	}

	bool ok = ws_send_copy_out_response(sock, 0);
	int64_t total = 0;

	while (ok)
	{
		ssize_t got = read(fd, buffer, WS_FETCH_CHUNK_SIZE);

		if (got < 0 && errno == EINTR)
		{
			continue;
		}

		if (got < 0)
		{
			log_error("Failed to read \"%s\": %m", path);
			ok = false;
			break;
		}

		if (got == 0)
		{
			break;
		}

		ok = ws_send_copy_data(sock, buffer, (int32_t) got);
		total += got;
	}

	free(buffer);
	close(fd);

	ok = ok && ws_send_copy_done(sock) &&
		 ws_send_command_complete(sock, "FETCH_FILE");

	if (!ok)
	{
		/* in the middle of a COPY: the connection cannot be reused */
		log_error("Failed to send \"%s\" to a FETCH_FILE client", filename);
		ws_connection_close_after_command = true;
	}
	else
	{
		log_info("FETCH_FILE: served \"%s\" (%lld bytes) from \"%s\"",
				 filename, (long long) total, route->path);
	}
}
