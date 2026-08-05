/*
 * src/bin/pg_walsender/cmd_base_backup.c
 *   See cmd_base_backup.h.
 *
 *   Wire sequence for a successful, synchronous BASE_BACKUP (traced from
 *   basebackup_copy.c's bbsink_copystream_* callbacks and cross-checked
 *   against the exact PQgetResult() loop in pg_basebackup.c around its own
 *   "BASE_BACKUP" psprintf call -- both in
 *   /Users/dim/dev/PostgreSQL/postgresql):
 *
 *     1. RowDescription(recptr text, tli int8) + DataRow + CommandComplete
 *        "SELECT"                                   -- the start position
 *     2. RowDescription(spcoid oid, spclocation text, size int8) +
 *        DataRow(NULL, NULL, NULL) + CommandComplete "SELECT" -- one row,
 *        the base directory itself (path NULL means "not a tablespace")
 *     3. CopyOutResponse(format 0, natts 0)
 *     4. CopyData['n', "base.tar\0", "\0"]           -- PqBackupMsg_NewArchive
 *     5. CopyData['d', <tar bytes>] x N               -- PqMsg_CopyData
 *     6. CopyDone
 *     7. RowDescription(recptr text, tli int8) + DataRow + CommandComplete
 *        "SELECT"                                     -- the end position
 *     8. CommandComplete "BASE_BACKUP"                 -- EndReplicationCommand
 *
 *   pg_basebackup.c calls PQgetResult() exactly four times for this (steps
 *   1, 2, [3-6 consumed internally by ReceiveArchiveStream], 7, 8), and
 *   explicitly checks step 8's PQresultStatus() == PGRES_COMMAND_OK.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <string.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "cmd_base_backup.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "string_utils.h"
#include "tar_stream.h"

typedef struct BaseBackupOptions
{
	char label[256];
	bool sendWal;
	bool manifestRequested;
	bool compressionRequested;
	char target[64];
} BaseBackupOptions;


/*
 * scan_options tolerantly parses the BASE_BACKUP option list real
 * pg_basebackup sends, e.g.:
 *   LABEL 'pg_basebackup base backup', CHECKPOINT 'fast', TARGET 'client'
 * Options this MVP doesn't act on (PROGRESS, CHECKPOINT, WAIT, MAX_RATE,
 * TABLESPACE_MAP, VERIFY_CHECKSUMS, MANIFEST_CHECKSUMS) are recognized and
 * ignored rather than rejected -- only WAL/MANIFEST/COMPRESSION/a non-
 * "client" TARGET actually change behavior (see cmd_base_backup()'s own
 * validation right after calling this).
 */
static void
scan_options(const char *raw, BaseBackupOptions *opts)
{
	memset(opts, 0, sizeof(BaseBackupOptions));

	const char *p = raw;

	while (*p)
	{
		while (isspace((unsigned char) *p) || *p == ',' || *p == '(' || *p == ')')
		{
			p++;
		}

		if (*p == '\0')
		{
			break;
		}

		const char *keyStart = p;

		while (*p && !isspace((unsigned char) *p) && *p != ',' && *p != ')')
		{
			p++;
		}

		char key[64];
		size_t keyLen = Min((size_t) (p - keyStart), sizeof(key) - 1);

		memcpy(key, keyStart, keyLen); /* IGNORE-BANNED */
		key[keyLen] = '\0';

		while (isspace((unsigned char) *p))
		{
			p++;
		}

		char value[512] = { 0 };

		if (*p == '\'')
		{
			p++;

			char *out = value;
			char *outEnd = value + sizeof(value) - 1;

			while (*p && !(*p == '\'' && p[1] != '\''))
			{
				if (*p == '\'' && p[1] == '\'')
				{
					if (out < outEnd)
					{
						*out++ = '\'';
					}
					p += 2;
					continue;
				}

				if (out < outEnd)
				{
					*out++ = *p;
				}

				p++;
			}

			*out = '\0';

			if (*p == '\'')
			{
				p++;
			}
		}
		else if (*p && *p != ',' && *p != ')')
		{
			const char *valStart = p;

			while (*p && *p != ',' && *p != ')' && !isspace((unsigned char) *p))
			{
				p++;
			}

			size_t valLen = Min((size_t) (p - valStart), sizeof(value) - 1);

			memcpy(value, valStart, valLen); /* IGNORE-BANNED */
			value[valLen] = '\0';
		}

		if (strcasecmp(key, "LABEL") == 0)
		{
			strlcpy(opts->label, value, sizeof(opts->label));
		}
		else if (strcasecmp(key, "WAL") == 0)
		{
			opts->sendWal = true;
		}
		else if (strcasecmp(key, "MANIFEST") == 0)
		{
			/* pg_basebackup only ever sends this key when it wants one
			 * ("yes"/"force-encode"); --no-manifest omits it entirely */
			opts->manifestRequested = true;
		}
		else if (strcasecmp(key, "TARGET") == 0)
		{
			strlcpy(opts->target, value, sizeof(opts->target));
		}
		else if (strcasecmp(key, "COMPRESSION") == 0)
		{
			opts->compressionRequested = true;
		}

		while (isspace((unsigned char) *p) || *p == ',')
		{
			p++;
		}
	}
}


/*
 * read_backup_label extracts the "START WAL LOCATION" and "START TIMELINE"
 * fields real pg_basebackup already wrote into basebackupDir/backup_label
 * when the archiver originally took this backup (see cmd_base_backup.h's
 * own header comment: do_pg_backup_start() is never called here, this file
 * already exists on disk). Returns false (caller falls back to the
 * route's own systemid/timeline, "0/0" for the LSN) if the file is
 * missing or doesn't parse -- a base backup taken by a later milestone's
 * own machinery is expected to always have one.
 */
static bool
read_backup_label(const char *basebackupDir, char *lsnOut, size_t lsnOutSize,
				  int *timelineOut)
{
	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/backup_label", basebackupDir);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool foundLsn = false;
	bool foundTimeline = false;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		const char *lsnPrefix = "START WAL LOCATION: ";
		const char *tliPrefix = "START TIMELINE: ";

		if (strncmp(line, lsnPrefix, strlen(lsnPrefix)) == 0)
		{
			const char *value = line + strlen(lsnPrefix);
			const char *end = value;

			while (*end && !isspace((unsigned char) *end))
			{
				end++;
			}

			size_t len = Min((size_t) (end - value), lsnOutSize - 1);

			memcpy(lsnOut, value, len); /* IGNORE-BANNED */
			lsnOut[len] = '\0';
			foundLsn = true;
		}
		else if (strncmp(line, tliPrefix, strlen(tliPrefix)) == 0)
		{
			foundTimeline = stringToInt(line + strlen(tliPrefix), timelineOut);
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return foundLsn && foundTimeline;
}


typedef struct TarStreamCbContext
{
	int sock;
	bool ok;
} TarStreamCbContext;


static bool
tar_chunk_cb(void *context, const char *data, size_t len)
{
	TarStreamCbContext *ctx = (TarStreamCbContext *) context;
	PQExpBuffer buf = createPQExpBuffer();

	appendPQExpBufferChar(buf, 'd');   /* PqMsg_CopyData content tag */
	appendBinaryPQExpBuffer(buf, data, len);

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_copy_data(ctx->sock, buf->data, buf->len);

	destroyPQExpBuffer(buf);

	if (!ok)
	{
		ctx->ok = false;
	}

	return ok;
}


static bool
send_position_row(int sock, const char *lsn, const char *tli)
{
	WsColumn columns[] = {
		{ "recptr", WS_TEXTOID, -1 },
		{ "tli", WS_INT8OID, 8 },
	};

	const char *values[] = { lsn, tli };

	return ws_send_row_description(sock, columns, 2) &&
		   ws_send_data_row(sock, values, 2) &&
		   ws_send_command_complete(sock, "SELECT");
}


/*
 * find_reachable_end_position and its helpers below compute a base
 * backup's "end of backup" position -- see this file's own header comment
 * for where that fits in the wire sequence, and cmd_base_backup()'s own
 * call site for why it must be a real, currently-reachable target rather
 * than a stale re-send of the start position.
 *
 * Deliberately not pg_walsender/wal_dir_scan.c's own wal_dir_find_latest()
 * (this project doesn't share code across its own binaries, see this
 * file's own precedent of small, self-contained helpers): that function
 * only ever considers a *complete* (non-".partial") segment, which is the
 * right, conservative choice for IDENTIFY_SYSTEM/CREATE_REPLICATION_SLOT's
 * own "confirmed durable" needs, but wrong here -- an archiver whose only
 * WAL activity so far is still sitting in the current ".partial" segment
 * (a real, common case: nothing has forced a segment switch yet) would
 * make wal_dir_find_latest() report "nothing captured", sending BASE_
 * BACKUP straight back to the same stale start-of-backup fallback this
 * whole mechanism exists to avoid. The archiver's walcache always has
 * *something* real captured by the time a base backup exists at all
 * (pg_receivewal streams from the moment archiving starts); the position
 * within the current in-progress segment is exactly as reachable via
 * START_REPLICATION as a completed one, once its zero-padded unwritten
 * tail (pg_receivewal's own pre-allocation, matching real Postgres's
 * XLogFileInitInternal) is trimmed off -- the same trim_trailing_zeros()
 * logic cmd_start_replication.c already applies when actually serving it,
 * applied here once, up front, to find where its real content ends.
 */
#define CBB_WAL_SEGMENT_SIZE UINT64CONST(0x1000000)
#define CBB_XLOG_SEGMENTS_PER_XLOGID (UINT64CONST(0x100000000) / CBB_WAL_SEGMENT_SIZE)
#define CBB_WAL_FNAME_LEN 24


static bool
is_wal_segment_filename(const char *name)
{
	size_t len = strlen(name);

	if (len != CBB_WAL_FNAME_LEN)
	{
		return false;
	}

	for (size_t i = 0; i < len; i++)
	{
		if (!isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


static bool
partial_segment_real_length(const char *path, uint64_t *length)
{
	FILE *file = fopen(path, "rb"); /* IGNORE-BANNED */

	if (file == NULL)
	{
		return false;
	}

	char *buffer = malloc(CBB_WAL_SEGMENT_SIZE);

	if (buffer == NULL)
	{
		fclose(file);
		return false;
	}

	size_t got = fread(buffer, 1, CBB_WAL_SEGMENT_SIZE, file);

	fclose(file);

	while (got > 0 && buffer[got - 1] == 0)
	{
		got--;
	}

	free(buffer);

	*length = (uint64_t) got;

	return true;
}


static bool
find_reachable_end_position(const char *walcacheDir, uint32_t *timeline,
							char *endLsn, size_t endLsnSize)
{
	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		return false;
	}

	char bestComplete[CBB_WAL_FNAME_LEN + 1] = { 0 };
	char bestPartial[CBB_WAL_FNAME_LEN + 1] = { 0 };
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (is_wal_segment_filename(entry->d_name))
		{
			if (bestComplete[0] == '\0' || strcmp(entry->d_name, bestComplete) > 0)
			{
				strlcpy(bestComplete, entry->d_name, sizeof(bestComplete));
			}

			continue;
		}

		const char *partialSuffix = ".partial";
		size_t nameLen = strlen(entry->d_name);
		size_t suffixLen = strlen(partialSuffix);

		if (nameLen == CBB_WAL_FNAME_LEN + suffixLen &&
			strcmp(entry->d_name + CBB_WAL_FNAME_LEN, partialSuffix) == 0)
		{
			char segPart[CBB_WAL_FNAME_LEN + 1] = { 0 };

			memcpy(segPart, entry->d_name, CBB_WAL_FNAME_LEN); /* IGNORE-BANNED */

			if (is_wal_segment_filename(segPart) &&
				(bestPartial[0] == '\0' || strcmp(segPart, bestPartial) > 0))
			{
				strlcpy(bestPartial, segPart, sizeof(bestPartial));
			}
		}
	}

	closedir(dir);

	/*
	 * The current frontier is whichever of the two is numerically later --
	 * a ".partial" file only ever exists for the segment actively being
	 * written, always the same as or newer than the newest complete one.
	 */
	bool usePartial = bestPartial[0] != '\0' &&
					  (bestComplete[0] == '\0' ||
					   strcmp(bestPartial, bestComplete) >= 0);

	const char *chosen = usePartial ? bestPartial : bestComplete;

	if (chosen[0] == '\0')
	{
		return false;
	}

	char tliHex[9] = { 0 };
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(tliHex, chosen, 8); /* IGNORE-BANNED */
	memcpy(logIdHex, chosen + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, chosen + 16, 8); /* IGNORE-BANNED */

	uint32_t tli = (uint32_t) strtoul(tliHex, NULL, 16);
	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * CBB_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t segStart = segno * CBB_WAL_SEGMENT_SIZE;
	uint64_t position;

	if (usePartial)
	{
		char path[MAXPGPATH];
		uint64_t realLength = 0;

		sformat(path, sizeof(path), "%s/%s.partial", walcacheDir, bestPartial);

		if (!partial_segment_real_length(path, &realLength))
		{
			return false;
		}

		position = segStart + realLength;
	}
	else
	{
		position = segStart + CBB_WAL_SEGMENT_SIZE;
	}

	*timeline = tli;
	sformat(endLsn, endLsnSize, "%X/%08X",
			(uint32_t) (position >> 32), (uint32_t) (position & 0xFFFFFFFF));

	return true;
}


void
cmd_base_backup(int sock, const WsRoute *route, const char *rawOptions)
{
	if (route == NULL || route->basebackupDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no base backup configured for this route "
							   "(the archiver hasn't taken one yet, or this "
							   "route wasn't given a basebackup directory)");
		return;
	}

	BaseBackupOptions opts;

	scan_options(rawOptions, &opts);

	if (opts.sendWal)
	{
		ws_send_error_response(sock, "0A000",
							   "WAL-inclusive BASE_BACKUP is not supported "
							   "yet -- retry with pg_basebackup's -X none");
		return;
	}

	if (opts.manifestRequested)
	{
		ws_send_error_response(sock, "0A000",
							   "backup manifests are not supported yet -- "
							   "retry with pg_basebackup's --no-manifest");
		return;
	}

	if (opts.compressionRequested)
	{
		ws_send_error_response(sock, "0A000",
							   "server-side compression is not supported yet");
		return;
	}

	if (opts.target[0] != '\0' && strcasecmp(opts.target, "client") != 0)
	{
		ws_send_error_response(sock, "0A000",
							   "only the default client-streaming BASE_BACKUP "
							   "target is supported");
		return;
	}

	char lsn[32] = "0/0";
	int timeline = (route->timeline > 0) ? route->timeline : 1;

	if (!read_backup_label(route->basebackupDir, lsn, sizeof(lsn), &timeline))
	{
		log_warn("No parseable backup_label under \"%s\"; reporting a "
				 "placeholder start position", route->basebackupDir);
	}

	char tliStr[16];

	sformat(tliStr, sizeof(tliStr), "%d", timeline);

	if (!send_position_row(sock, lsn, tliStr))
	{
		return;
	}

	WsColumn tsColumns[] = {
		{ "spcoid", WS_INT4OID, 4 },
		{ "spclocation", WS_TEXTOID, -1 },
		{ "size", WS_INT8OID, 8 },
	};

	const char *tsValues[] = { NULL, NULL, NULL };

	if (!ws_send_row_description(sock, tsColumns, 3) ||
		!ws_send_data_row(sock, tsValues, 3) ||
		!ws_send_command_complete(sock, "SELECT"))
	{
		return;
	}

	if (!ws_send_copy_out_response(sock, 0))
	{
		return;
	}

	{
		PQExpBuffer buf = createPQExpBuffer();

		appendPQExpBufferChar(buf, 'n');   /* PqBackupMsg_NewArchive */
		appendBinaryPQExpBuffer(buf, "base.tar", strlen("base.tar") + 1);
		appendBinaryPQExpBuffer(buf, "", 1);   /* empty path: not a tablespace */

		bool ok = !PQExpBufferBroken(buf) &&
				  ws_send_copy_data(sock, buf->data, buf->len);

		destroyPQExpBuffer(buf);

		if (!ok)
		{
			return;
		}
	}

	TarStreamCbContext ctx = { sock, true };

	if (!tar_stream_directory(route->basebackupDir, tar_chunk_cb, &ctx) || !ctx.ok)
	{
		log_error("Failed to stream base backup tar contents from \"%s\"",
				  route->basebackupDir);
		return;
	}

	if (!ws_send_copy_done(sock))
	{
		return;
	}

	/*
	 * The end-of-backup position must be a real, currently-reachable target
	 * -- re-sending the same (potentially long-stale) start position here
	 * would tell a real pg_basebackup's own background WAL streamer
	 * (--wal-method=stream) to wait for a target it may have already
	 * passed hours ago, or, worse, one from a since-pruned segment it can
	 * never reach; either way its background thread hangs the whole
	 * command forever waiting on a position that will never legitimately
	 * arrive as "new" data.
	 *
	 * route->position is the canonical, out-of-band-maintained value --
	 * see service_archiver_update_current_lsn()'s own comment (pg_autoctl's
	 * service_archiver.c) for why the archiver-serve supervisor computes
	 * this once, itself, and writes it into the routes file, rather than
	 * every reader (this one included) independently re-deriving it by
	 * scanning WAL file content on its own. find_reachable_end_position()
	 * (this file's own comment) is the fallback for a route that doesn't
	 * carry one yet (an older archiver-serve binary against a newer pg_
	 * walsender, during a rolling upgrade) -- still a real, reachable
	 * position, just independently re-derived. Falls back further still to
	 * the start position only if the walcache is completely empty (no base
	 * backup should exist at all in that case).
	 */
	char endLsn[32];
	uint32_t endTimeline;
	const char *endLsnPtr = lsn;
	const char *endTliStr = tliStr;
	char endTliBuf[16];

	if (route->position[0] != '\0')
	{
		endLsnPtr = route->position;
		endTliStr = tliStr;
	}
	else if (find_reachable_end_position(route->walcacheDir, &endTimeline, endLsn,
										 sizeof(endLsn)))
	{
		sformat(endTliBuf, sizeof(endTliBuf), "%u", endTimeline);
		endLsnPtr = endLsn;
		endTliStr = endTliBuf;
	}

	if (!send_position_row(sock, endLsnPtr, endTliStr))
	{
		return;
	}

	ws_send_command_complete(sock, "BASE_BACKUP");
}
