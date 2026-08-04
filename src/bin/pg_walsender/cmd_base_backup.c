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
#include <string.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "cmd_base_backup.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
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

		memcpy(key, keyStart, keyLen);
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

			memcpy(value, valStart, valLen);
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

	snprintf(path, sizeof(path), "%s/backup_label", basebackupDir);

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

			memcpy(lsnOut, value, len);
			lsnOut[len] = '\0';
			foundLsn = true;
		}
		else if (strncmp(line, tliPrefix, strlen(tliPrefix)) == 0)
		{
			*timelineOut = atoi(line + strlen(tliPrefix));
			foundTimeline = true;
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

	snprintf(tliStr, sizeof(tliStr), "%d", timeline);

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

	if (!send_position_row(sock, lsn, tliStr))
	{
		return;
	}

	ws_send_command_complete(sock, "BASE_BACKUP");
}
