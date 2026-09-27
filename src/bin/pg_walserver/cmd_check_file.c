/*
 * src/bin/pg_walserver/cmd_check_file.c
 *   See cmd_check_file.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <string.h>
#include <strings.h>

#include "postgres_fe.h"

#include "cmd_check_file.h"
#include "cmd_fetch_file.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "ws_util.h"


/*
 * cmd_check_file implements CHECK_FILE: validate filename against the same
 * allow-list FETCH_FILE/ARCHIVE_FILE use, then answer whether a file of
 * exactly clientSize/clientCrc32cHex is already on disk under that name --
 * "missing" (ENOENT), "matches" (identical), or "differs" (present but not
 * identical) -- as a single-row RowDescription/DataRow/CommandComplete
 * reply, the same shape SHOW already uses. No file content is ever read or
 * sent either way: this is a cheap, transfer-free round trip, meant to be
 * called before ARCHIVE_FILE actually pushes anything (see cli_archive.c).
 */
void
cmd_check_file(int sock, const WsRoute *route, const char *filename,
			   uint64_t clientSize, const char *clientCrc32cHex)
{
	if (!ws_fetch_filename_is_servable(filename))
	{
		char safeName[64];

		ws_sanitize_for_log(filename, safeName, sizeof(safeName));
		log_warn("Rejecting CHECK_FILE request for filename \"%s\"", safeName);
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

	uint64_t diskSize = 0;
	uint32_t diskCrc = 0;
	const char *status;

	if (!ws_file_crc32c(path, &diskSize, &diskCrc))
	{
		if (errno == ENOENT)
		{
			status = "missing";
		}
		else
		{
			log_error("CHECK_FILE: failed to read \"%s\": %m", path);
			ws_send_error_response(sock, "58030", "failed to read the file "
												  "already on disk");
			return;
		}
	}
	else
	{
		char diskCrcHex[16];

		sformat(diskCrcHex, sizeof(diskCrcHex), "%08X", diskCrc);

		status = (diskSize == clientSize &&
				  strcasecmp(diskCrcHex, clientCrc32cHex) == 0)
				 ? "matches"
				 : "differs";
	}

	WsColumn columns[] = {
		{ "status", WS_TEXTOID, -1 },
	};

	const char *values[] = { status };

	if (!ws_send_row_description(sock, columns, 1) ||
		!ws_send_data_row(sock, values, 1) ||
		!ws_send_command_complete(sock, "CHECK_FILE"))
	{
		return;
	}

	log_info("CHECK_FILE: \"%s\" under \"%s\" is \"%s\"",
			 filename, route->path, status);
}
