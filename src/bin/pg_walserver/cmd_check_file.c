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
#include <time.h>

#include "postgres_fe.h"

#include "cmd_check_file.h"
#include "cmd_fetch_file.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "wal_dir_scan.h"
#include "ws_util.h"


/*
 * check_file_receivewal_has_passed answers this file's own header
 * comment's "fallback" column: has route's own embedded receivewal
 * worker already streamed past filename -- a later segment, or a later
 * timeline -- while filename itself never showed up? That is a hole a
 * streaming worker can never retroactively fill (a timeline switch left
 * a segment behind on the old timeline is the common real case), unlike
 * the ordinary "the worker just hasn't caught up to filename yet", which
 * is not a hole at all, only a matter of waiting a little longer.
 *
 * Deliberately reads "<path>/receivewal-progress" (wal_dir_scan.h), not
 * a fresh directory scan: CHECK_FILE is called once per WAL segment on
 * every archive_command invocation, so it has to stay the cheap,
 * transfer-free round trip this file's own header comment already
 * promises -- one small file read, not an readdir() pass over
 * potentially thousands of files. That file's own "lsn" is explicitly a
 * display-only approximation (not guaranteed to land on a real WAL
 * record boundary, see its own comment), which is fine here: the
 * decision below only ever needs a segment-granularity comparison, not
 * record-level precision.
 *
 * Returns false (the safe "keep waiting" default) whenever there isn't
 * enough information to call it a hole with confidence: no embedded
 * receivewal worker on this route at all, no live progress reading yet
 * (never ticked, or not running), or filename isn't one of the LSN-
 * positioned shapes (WS_WAL_FILE_SEGMENT/PARTIAL/BACKUP) this comparison
 * applies to -- a bare ".history" file, most notably, carries no LSN of
 * its own to compare against.
 */
static bool
check_file_receivewal_has_passed(const WsRoute *route, const char *filename)
{
	if (!route->receivewalPull)
	{
		return false;
	}

	char segmentPrefix[32] = { 0 };
	WsWalFileKind kind = ws_wal_dir_classify_filename(filename, segmentPrefix);

	if (kind != WS_WAL_FILE_SEGMENT && kind != WS_WAL_FILE_PARTIAL &&
		kind != WS_WAL_FILE_BACKUP)
	{
		return false;
	}

	char progressLsn[32] = { 0 };
	uint32_t progressTimeline = 0;
	time_t observedAt = 0;

	if (!ws_receivewal_progress_read(route->path, progressLsn,
									 sizeof(progressLsn), &progressTimeline,
									 &observedAt))
	{
		return false;
	}

	uint64_t segSize = ws_route_wal_segment_size(route);
	uint32_t fileTimeline;
	uint64_t fileSegNo;

	wal_segment_name_parse(segmentPrefix, segSize, &fileTimeline,
						   &fileSegNo);

	uint64_t progressSegNo;

	if (!wal_lsn_to_segno(progressLsn, segSize, &progressSegNo))
	{
		return false;
	}

	if (progressTimeline > fileTimeline)
	{
		/* the stream has already moved to a later timeline than the one
		 * filename belongs to: it is never coming from this worker */
		return true;
	}

	/* same timeline: a hole only if the stream has moved past filename's
	 * own segment already -- still being written (progressSegNo ==
	 * fileSegNo) is the ordinary, still-catching-up case, not a hole */
	return progressTimeline == fileTimeline && progressSegNo > fileSegNo;
}


/*
 * cmd_check_file implements CHECK_FILE: validate filename against the same
 * allow-list FETCH_FILE/ARCHIVE_FILE use, then answer whether a file of
 * exactly clientSize/clientCrc32cHex is already on disk under that name --
 * "missing" (ENOENT), "matches" (identical), or "differs" (present but not
 * identical) -- plus this file's own header comment's "fallback" column,
 * as a single-row RowDescription/DataRow/CommandComplete reply, the same
 * shape SHOW already uses. No file content is ever read or sent either
 * way: this is a cheap, transfer-free round trip, meant to be called
 * before ARCHIVE_FILE actually pushes anything (see cli_archive.c).
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

	bool fallback = strcmp(status, "matches") != 0 &&
					check_file_receivewal_has_passed(route, filename);

	WsColumn columns[] = {
		{ "status", WS_TEXTOID, -1 },
		{ "fallback", WS_TEXTOID, -1 },
	};

	const char *values[] = { status, fallback ? "yes" : "no" };

	if (!ws_send_row_description(sock, columns, 2) ||
		!ws_send_data_row(sock, values, 2) ||
		!ws_send_command_complete(sock, "CHECK_FILE"))
	{
		return;
	}

	if (fallback)
	{
		log_info("CHECK_FILE: \"%s\" under \"%s\" is \"%s\", and this "
				 "route's own embedded receivewal worker has already "
				 "streamed past it: recommending an ARCHIVE_FILE fallback",
				 filename, route->path, status);
	}
	else
	{
		log_info("CHECK_FILE: \"%s\" under \"%s\" is \"%s\"",
				 filename, route->path, status);
	}
}
