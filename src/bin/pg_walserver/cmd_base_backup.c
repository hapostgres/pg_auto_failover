/*
 * src/bin/pg_walserver/cmd_base_backup.c
 *   See cmd_base_backup.h.
 *
 *   Wire sequence for a successful, synchronous BASE_BACKUP (traced from
 *   basebackup_copy.c's bbsink_copystream_* callbacks and cross-checked
 *   against the exact PQgetResult() loop in pg_basebackup.c around its own
 *   "BASE_BACKUP" psprintf call):
 *
 *     1. RowDescription(recptr text, tli int8) + DataRow + CommandComplete
 *        "SELECT"                                   -- the start position
 *     2. RowDescription(spcoid oid, spclocation text, size int8) +
 *        DataRow(NULL, NULL, NULL) + CommandComplete "SELECT" -- one row,
 *        the base directory itself (path NULL means "not a tablespace")
 *   PG15+ (CBB_USE_ARCHIVE_FRAMING true -- see that macro below for how this
 *   file picks a branch at compile time, driven by which real Postgres this
 *   project's own pg_walserver itself was built against):
 *
 *     3. CopyOutResponse(format 0, natts 0)           -- exactly one, covers
 *                                                         every archive AND
 *                                                         the manifest below
 *     4. CopyData['n', "base.tar\0", "\0"]           -- PqBackupMsg_NewArchive
 *     5. CopyData['d', <tar bytes>] x N               -- PqMsg_CopyData
 *     5a. CopyData['m']                               -- PqBackupMsg_Manifest,
 *                                                         no payload -- only
 *                                                         when the backup on
 *                                                         disk has a backup_
 *                                                         manifest (see
 *                                                         stream_manifest_as_
 *                                                         copy_data()'s
 *                                                         comment)
 *     5b. CopyData['d', <manifest bytes>] x N         -- same 'd' content
 *                                                         tag as step 5, the
 *                                                         'm' marker above is
 *                                                         what tells the
 *                                                         client these bytes
 *                                                         are manifest, not
 *                                                         more tar
 *     6. CopyDone                                     -- ends the ONE CopyOut
 *                                                         from step 3, after
 *                                                         every archive and
 *                                                         the manifest
 *
 *   Pre-PG15 (CBB_USE_ARCHIVE_FRAMING false): the client's own receiving
 *   code predates the typed/tagged single-stream framing above entirely --
 *   it reads step 3's CopyOut as nothing but plain, untagged tar bytes
 *   until CopyDone, with no notion that a manifest could share that same
 *   stream. So instead of steps 4-6 above, this branch sends step 3's
 *   CopyOut as bare tar bytes only (no 'n'/'d' tags at all), a CopyDone to
 *   end it, and then -- only if a manifest was requested -- a completely
 *   independent second CopyOutResponse/CopyData(raw, untagged manifest
 *   bytes)/CopyDone trio, matching this same client's own two-phase
 *   BASE_BACKUP handling for that case:
 *
 *     3. CopyOutResponse(format 0, natts 0)             -- tar only
 *     4. CopyData[<raw tar bytes>] x N                   -- no 'n'/'d' tags
 *     5. CopyDone                                        -- ends the tar's
 *                                                            own CopyOut
 *     5a. CopyOutResponse(format 0, natts 0)              -- only if a
 *     5b. CopyData[<raw manifest bytes>] x N                 manifest was
 *     5c. CopyDone                                           requested
 *
 *   Both branches converge again after this point:
 *
 *     7. RowDescription(recptr text, tli int8) + DataRow + CommandComplete
 *        "SELECT"                                     -- the end position
 *     8. CommandComplete "BASE_BACKUP"                 -- EndReplicationCommand
 *
 *   pg_basebackup.c calls PQgetResult() exactly four times for this (steps
 *   1, 2, [everything from step 3 up to but not including step 7, consumed
 *   internally by ReceiveArchiveStream regardless of which branch was
 *   actually sent], 7, 8), and explicitly checks step 8's PQresultStatus()
 *   == PGRES_COMMAND_OK.
 *
 *   History: an earlier version of this file sent the manifest as its own
 *   second CopyOutResponse/CopyDone pair *unconditionally*, including
 *   against PG15+ clients -- structurally wrong there relative to real
 *   Postgres's own bbsink_copystream_* callbacks (basebackup_copy.c), and
 *   the cause of a real "pg_basebackup: error: backup failed:" (empty
 *   message) bug. Fixing that by switching unconditionally to the single-
 *   combined-stream design then broke the pre-PG15 branch the exact
 *   opposite way -- folding manifest bytes into a stream a pre-PG15 client
 *   reads as tar-only corrupts its tar parse ("invalid tar block header
 *   size"), a second real bug. Both branches above are independently
 *   correct for the client they're built for; getting this file's own
 *   CBB_USE_ARCHIVE_FRAMING gate to consistently apply to *every* step that
 *   differs between them (not just the tag bytes within the shared code
 *   path) is what both fixes actually needed.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
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
#include "wal_dir_scan.h"

typedef struct BaseBackupOptions
{
	char label[256];
	bool sendWal;
	bool manifestRequested;
	bool compressionRequested;
	char target[64];
} BaseBackupOptions;


/*
 * collect_options reads the BASE_BACKUP option list repl_gram.y's grammar
 * already parsed (see repl_command.h's WsCommandOption array) into this
 * file's own BaseBackupOptions, e.g. for:
 *   LABEL 'pg_basebackup base backup', CHECKPOINT 'fast', TARGET 'client'
 * Options this MVP doesn't act on (PROGRESS, CHECKPOINT, WAIT, MAX_RATE,
 * TABLESPACE_MAP, VERIFY_CHECKSUMS, MANIFEST_CHECKSUMS) are recognized and
 * ignored rather than rejected -- only WAL/MANIFEST/COMPRESSION/a non-
 * "client" TARGET actually change behavior (see cmd_base_backup()'s own
 * validation right after calling this). Replaces this file's own former
 * ad hoc scan_options() text scanner, now that the option list arrives
 * already tokenized by the real replication grammar.
 */
static void
collect_options(const WsCommandOption *options, int nOptions, BaseBackupOptions *opts)
{
	memset(opts, 0, sizeof(BaseBackupOptions));

	for (int i = 0; i < nOptions; i++)
	{
		const WsCommandOption *opt = &options[i];

		if (strcasecmp(opt->name, "label") == 0)
		{
			strlcpy(opts->label, opt->value, sizeof(opts->label));
		}
		else if (strcasecmp(opt->name, "wal") == 0)
		{
			opts->sendWal = true;
		}
		else if (strcasecmp(opt->name, "manifest") == 0)
		{
			/* pg_basebackup only ever sends this key when it wants one
			 * ("yes"/"force-encode"); --no-manifest omits it entirely */
			opts->manifestRequested = true;
		}
		else if (strcasecmp(opt->name, "target") == 0)
		{
			strlcpy(opts->target, opt->value, sizeof(opts->target));
		}
		else if (strcasecmp(opt->name, "compression") == 0)
		{
			opts->compressionRequested = true;
		}
	}
}


/*
 * read_backup_label extracts the "START WAL LOCATION" and "START TIMELINE"
 * fields real pg_basebackup already wrote into basebackupDir/backup_label
 * when the archiver originally took this backup (see cmd_base_backup.h's
 * own header comment: do_pg_backup_start() is never called here, this file
 * already exists on disk).
 *
 * Ported from PostgreSQL's own read_backup_label() in
 * src/backend/access/transam/xlogrecovery.c: same fixed format-string
 * scanning (via fscanf, IGNORE-BANNED below) of the two mandatory lines
 * ("this code is pretty
 * crude, but we are not expecting any variability in the file format", to
 * quote the original). Unlike the backend version, a parse failure here is
 * not FATAL: it's logged and the function returns false, and the caller
 * falls back to the route's own systemid/timeline, "0/0" for the LSN --
 * this project never expects a backup_label to fail to parse (the archiver
 * itself wrote it), but a corrupt/missing file must not crash the server.
 * Fields the backend also extracts but no caller here needs (BACKUP METHOD,
 * BACKUP FROM, START TIME, LABEL, INCREMENTAL FROM LSN) are skipped.
 *
 * Not static: cli_archive_cleanup.c's own basebackups/ enumeration reuses
 * this exact parser to learn each backup's own required starting WAL
 * segment, rather than re-deriving backup_label parsing from scratch.
 */
bool
read_backup_label(const char *basebackupDir, char *lsnOut, size_t lsnOutSize,
				  int *timelineOut)
{
	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/backup_label", basebackupDir);

	FILE *lfp = fopen(path, "r"); /* IGNORE-BANNED */

	if (lfp == NULL)
	{
		/* not there, or unreadable: not an error, caller has a fallback */
		return false;
	}

	uint32_t hi, lo;
	uint32_t tliFromWalSeg;
	char startXlogFileName[64]; /* matches xlog_internal.h's MAXFNAMELEN,
	                             * see cmd_timeline_history.c's WS_MAXFNAMELEN */
	char ch;

	/* same fixed format string as PostgreSQL's own read_backup_label() */
	if (fscanf(lfp, "START WAL LOCATION: %X/%08X (file %08X%16s)%c", /* IGNORE-BANNED */
			   &hi, &lo, &tliFromWalSeg, startXlogFileName, &ch) != 5 ||
		ch != '\n')
	{
		log_error("Invalid data in file \"%s\": could not parse "
				  "\"START WAL LOCATION\"", path);
		fclose(lfp); /* IGNORE-BANNED */
		return false;
	}

	sformat(lsnOut, lsnOutSize, "%X/%08X", hi, lo);

	uint32_t tliFromFile;

	/*
	 * "START TIMELINE" is new as of PG11; PostgreSQL itself only uses it as
	 * a sanity check against the timeline embedded in the WAL segment file
	 * name parsed above (tliFromWalSeg), which is what actually feeds
	 * RedoStartTLI. Do the same here: prefer tliFromWalSeg, and only worry
	 * about the newer field if a mismatch would matter.
	 */
	if (fscanf(lfp, "START TIMELINE: %u\n", &tliFromFile) == 1 && /* IGNORE-BANNED */
		tliFromFile != tliFromWalSeg)
	{
		log_error("Invalid data in file \"%s\": timeline ID parsed is %u, "
				  "but expected %u", path, tliFromFile, tliFromWalSeg);
		fclose(lfp); /* IGNORE-BANNED */
		return false;
	}

	fclose(lfp); /* IGNORE-BANNED */

	*timelineOut = (int) tliFromWalSeg;

	return true;
}


typedef struct TarStreamCbContext
{
	int sock;
	bool ok;
} TarStreamCbContext;


/*
 * Whether to use the typed, multiplexed archive-streaming framing this
 * file's own header comment traces from PG15+'s basebackup_copy.c (the
 * "CopyData['n', ...]"/"CopyData['d', ...]" tagged messages) versus the
 * older, untagged "CopyData[<raw tar bytes>]" framing every pre-15
 * pg_basebackup client's receiving code was built against. pg_walserver is
 * built once per PGVERSION, against that version's own server headers (see
 * defaults.h's own WS_SERVER_VERSION comment) -- so PG_VERSION_NUM here is
 * already the archived group's real Postgres major version, and this
 * connection's client is always that same version's own pg_basebackup
 * (this project's Docker images are single-PG-version; there is no
 * cross-version client/server mixing to account for). A pre-15 pg_
 * basebackup binary has no code path for the tagged framing at all -- it
 * was added to the client in the same release as the server -- so sending
 * it unconditionally broke every PG14 base backup ("invalid tar block
 * header size: 11", the tag byte plus archive-name payload misparsed as
 * tar content) even though the reported server_version correctly said 14.
 */
#define CBB_USE_ARCHIVE_FRAMING (PG_VERSION_NUM >= 150000)


static bool
tar_chunk_cb(void *context, const char *data, size_t len)
{
	TarStreamCbContext *ctx = (TarStreamCbContext *) context;

#if CBB_USE_ARCHIVE_FRAMING
	PQExpBuffer buf = createPQExpBuffer();

	appendPQExpBufferChar(buf, 'd');   /* PqMsg_CopyData content tag */
	appendBinaryPQExpBuffer(buf, data, len);

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_copy_data(ctx->sock, buf->data, buf->len);

	destroyPQExpBuffer(buf);
#else
	bool ok = ws_send_copy_data(ctx->sock, data, len);
#endif

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
 * Deliberately not pg_walserver/wal_dir_scan.c's own wal_dir_find_latest()
 * -- that is a *business-logic* function this file intentionally doesn't
 * reuse (as opposed to the plain filename-shape primitives below,
 * wal_segment_name_is_valid()/_is_partial()/_parse(), src/bin/common/
 * wal_segment.h, which this file does share with wal_dir_scan.c -- there
 * is nothing route- or archiver-specific about recognizing a WAL segment
 * filename's own shape): wal_dir_find_latest() only ever considers a
 * *complete* (non-".partial") segment, which is the right, conservative
 * choice for IDENTIFY_SYSTEM/CREATE_REPLICATION_SLOT's own
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
 *
 * Tries wal_position_cache_read() first (wal_dir_scan.h) -- unlike wal_
 * dir_find_latest(), that cache is fed by pg_autoctl's own archiver-
 * capture loop (service_archiver_update_current_lsn(), service_archiver.
 * c), which already accounts for a live ".partial" segment the same way
 * this function's own scan below does, so it carries none of wal_dir_
 * find_latest()'s "complete segments only" limitation -- reading it
 * avoids a full directory scan on every BASE_BACKUP connection, which
 * matters once an archiver retains thousands of segments. The scan below
 * remains the fallback for a connection arriving before that cache's
 * first tick has landed.
 */
#define CBB_WAL_FNAME_LEN 24


/*
 * partial_segment_real_length finds where the real content of a ".partial"
 * segment ends: everything after the last non-zero byte is pg_receivewal's
 * pre-allocated tail. The file is scanned backwards in chunks, never read
 * whole (a segment can be up to 1 GiB).
 */
static bool
partial_segment_real_length(const char *path, uint64_t *length)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	struct stat st;

	if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
	{
		log_error("Failed to open \"%s\": %m", path);

		if (fd >= 0)
		{
			close(fd);
		}

		return false;
	}

	char buffer[64 * 1024];
	off_t end = st.st_size;

	while (end > 0)
	{
		off_t start = end > (off_t) sizeof(buffer) ?
					  end - (off_t) sizeof(buffer) : 0;
		ssize_t got = pread(fd, buffer, (size_t) (end - start), start);

		if (got != (ssize_t) (end - start))
		{
			log_error("Failed to read \"%s\": %m", path);
			close(fd);
			return false;
		}

		while (got > 0 && buffer[got - 1] == 0)
		{
			got--;
		}

		if (got > 0)
		{
			end = start + got;
			break;
		}

		end = start;
	}

	close(fd);

	*length = (uint64_t) end;

	return true;
}


static bool
find_reachable_end_position(const WsRoute *route, uint32_t *timeline,
							char *endLsn, size_t endLsnSize)
{
	const char *walcacheDir = route->path;
	uint64_t segSize = ws_route_wal_segment_size(route);

	if (wal_position_cache_read(walcacheDir, timeline, endLsn, endLsnSize))
	{
		return true;
	}

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
		if (wal_segment_name_is_valid(entry->d_name))
		{
			if (bestComplete[0] == '\0' || strcmp(entry->d_name, bestComplete) > 0)
			{
				strlcpy(bestComplete, entry->d_name, sizeof(bestComplete));
			}

			continue;
		}

		if (wal_segment_name_is_partial(entry->d_name))
		{
			char segPart[CBB_WAL_FNAME_LEN + 1] = { 0 };

			memcpy(segPart, entry->d_name, CBB_WAL_FNAME_LEN); /* IGNORE-BANNED */

			if (bestPartial[0] == '\0' || strcmp(segPart, bestPartial) > 0)
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

	uint32_t tli;
	uint64_t segno;

	wal_segment_name_parse(chosen, segSize, &tli, &segno);

	uint64_t segStart = segno * segSize;
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
		position = segStart + segSize;
	}

	*timeline = tli;
	sformat(endLsn, endLsnSize, "%X/%08X",
			(uint32_t) (position >> 32), (uint32_t) (position & 0xFFFFFFFF));

	return true;
}


/*
 * read_latest_basebackup_label reads the small pointer file pg_autoctl's
 * own service_archiver_basebackup.c writes the instant a live base backup
 * completes (basebackup_write_latest_pointer()) -- a single line naming
 * that backup's own label/subdirectory under "<path>/basebackups/". This
 * is the *only* place BASE_BACKUP learns which backup is current: no
 * caching, no monitor round trip, just whatever this file says right now
 * -- see routes.h's own header comment for the full rationale. Returns
 * false (labelOut untouched) when the file doesn't exist yet: no live base
 * backup has ever completed for this membership.
 */
static bool
read_latest_basebackup_label(const char *path, char *labelOut, size_t labelOutSize)
{
	char pointerPath[MAXPGPATH] = { 0 };

	sformat(pointerPath, sizeof(pointerPath), "%s/basebackups/.latest", path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(pointerPath, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	char *nl = strchr(contents, '\n');

	if (nl != NULL)
	{
		*nl = '\0';
	}

	strlcpy(labelOut, contents, labelOutSize);
	free(contents);

	/*
	 * The label becomes a path component below basebackups/: only a plain
	 * [A-Za-z0-9_.-]+ name that does not start with a dot (no ".", "..",
	 * hidden files, separators) is ever used.
	 */
	if (labelOut[0] == '\0' || labelOut[0] == '.')
	{
		return false;
	}

	for (const char *c = labelOut; *c != '\0'; c++)
	{
		if (!(isalnum((unsigned char) *c) || *c == '_' || *c == '.' ||
			  *c == '-'))
		{
			log_error("Ignoring the invalid base backup label in \"%s\"",
					  pointerPath);
			labelOut[0] = '\0';
			return false;
		}
	}

	return true;
}


/*
 * stream_manifest_as_copy_data sends manifestPath's raw bytes as part of
 * the *same* CopyOut stream the tar archive was just sent on -- matching
 * real Postgres's own bbsink_copystream_* callbacks (basebackup_copy.c):
 * there is exactly one CopyOutResponse/CopyDone pair for the whole
 * BASE_BACKUP, covering every archive and the manifest together, not a
 * second one for the manifest alone (an earlier version of this function
 * got this wrong -- see this file's own header comment for the corrected
 * wire sequence). The manifest is announced with its own leading CopyData
 * message tagged PqBackupMsg_Manifest ('m', no payload), then its content
 * follows in ordinary 'd'-tagged CopyData chunks, exactly like tar_chunk_
 * cb()'s own tar content chunks -- both share PG_VERSION_NUM >= 150000 as
 * the same "does this client speak the typed CopyData framing" gate,
 * since a client too old for one is too old for the other.
 *
 * The manifest already exists as a complete, valid file on disk (written
 * by the real pg_basebackup run that produced this on-disk backup in the
 * first place -- see pg_basebackup_fetch()'s own comment, pgctl.c), so
 * this is a plain chunked read, not manifest generation.
 */
static bool
stream_manifest_as_copy_data(int sock, const char *manifestPath)
{
#if CBB_USE_ARCHIVE_FRAMING
	{
		char tag = 'm';   /* PqBackupMsg_Manifest, no payload */

		if (!ws_send_copy_data(sock, &tag, 1))
		{
			return false;
		}
	}
#endif

	FILE *file = fopen(manifestPath, "rb"); /* IGNORE-BANNED */

	if (file == NULL)
	{
		log_error("Failed to open \"%s\": %m", manifestPath);
		return false;
	}

	char buffer[64 * 1024];
	size_t got;
	bool ok = true;

	while (ok && (got = fread(buffer, 1, sizeof(buffer), file)) > 0)
	{
#if CBB_USE_ARCHIVE_FRAMING
		PQExpBuffer buf = createPQExpBuffer();

		appendPQExpBufferChar(buf, 'd');   /* PqMsg_CopyData content tag */
		appendBinaryPQExpBuffer(buf, buffer, got);

		ok = !PQExpBufferBroken(buf) &&
			 ws_send_copy_data(sock, buf->data, buf->len);

		destroyPQExpBuffer(buf);
#else
		ok = ws_send_copy_data(sock, buffer, (int32_t) got);
#endif
	}

	if (ok && ferror(file))
	{
		log_error("Short read on \"%s\" while streaming the backup "
				  "manifest (file changed size mid-read?)", manifestPath);
		ok = false;
	}

	fclose(file);

	return ok;
}


/*
 * fail_stream: a failure once BASE_BACKUP has started sending cannot be
 * recovered from -- the client is in the middle of a COPY. Like
 * PostgreSQL's walsender (FATAL), report it (best effort: an ErrorResponse
 * ends a COPY OUT on the client side) and end the connection rather than
 * going back to ReadyForQuery.
 */
static void
fail_stream(int sock)
{
	(void) ws_send_error_response(sock, "58030", "base backup failed");
	ws_connection_close_after_command = true;
}


void
cmd_base_backup(int sock, const WsRoute *route,
				const WsCommandOption *options, int nOptions)
{
	char label[NAMEDATALEN] = { 0 };
	char basebackupDir[MAXPGPATH] = { 0 };

	if (route == NULL || route->path[0] == '\0' ||
		!read_latest_basebackup_label(route->path, label, sizeof(label)))
	{
		ws_send_error_response(sock, "58P01",
							   "no base backup configured for this route "
							   "(the archiver hasn't taken one yet, or this "
							   "route wasn't given a storage path)");
		return;
	}

	sformat(basebackupDir, sizeof(basebackupDir), "%s/basebackups/%s",
			route->path, label);

	if (!directory_exists(basebackupDir))
	{
		ws_send_error_response(sock, "58P01",
							   "the latest base backup directory is missing "
							   "on disk");
		return;
	}

	BaseBackupOptions opts;

	collect_options(options, nOptions, &opts);

	if (opts.sendWal)
	{
		ws_send_error_response(sock, "0A000",
							   "WAL-inclusive BASE_BACKUP is not supported "
							   "yet -- retry with pg_basebackup's -X none");
		return;
	}

	char manifestPath[MAXPGPATH] = { 0 };

	if (opts.manifestRequested)
	{
		sformat(manifestPath, sizeof(manifestPath), "%s/backup_manifest",
				basebackupDir);

		if (!file_exists(manifestPath))
		{
			ws_send_error_response(sock, "58P01",
								   "this base backup was taken without a "
								   "manifest -- retry with pg_basebackup's "
								   "--no-manifest");
			return;
		}
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
	int timeline = 1;

	if (!read_backup_label(basebackupDir, lsn, sizeof(lsn), &timeline))
	{
		log_warn("No parseable backup_label under \"%s\"; reporting a "
				 "placeholder start position", basebackupDir);
	}

	/*
	 * Read-time compatibility guard: a 'replay'-sourced backup could in
	 * principle end up on a *later* timeline than the walcache's own real
	 * captured timeline (a real pg_basebackup rejects that combination
	 * outright once it reaches its own background WAL streaming step --
	 * see this file's own find_reachable_end_position() comment). service_
	 * archiver_basebackup.c's own basebackup_write_latest_pointer() is only
	 * ever called for a 'live' backup precisely to make this unreachable at
	 * the source, but checking again here, fresh, against whatever the
	 * walcache actually says *right now* costs one cheap directory scan and
	 * catches it even if that guarantee is ever weakened later -- the same
	 * defense in depth this project's own archiver-serve used to apply at
	 * write time, moved to read time since that's the only place a change
	 * to either side (a new backup, or the walcache advancing past a
	 * failover) is guaranteed to be visible.
	 */
	uint32_t walcacheTimeline = 0;
	char walcacheEndLsn[32] = { 0 };
	bool haveWalcacheInfo = find_reachable_end_position(route,
														&walcacheTimeline,
														walcacheEndLsn,
														sizeof(walcacheEndLsn));

	if (haveWalcacheInfo && (int) walcacheTimeline != timeline)
	{
		ws_send_error_response(sock, "58P01",
							   "the latest base backup is on a different "
							   "timeline than the WAL cache; refusing to "
							   "serve a mismatched pairing");
		return;
	}

	char tliStr[16];

	sformat(tliStr, sizeof(tliStr), "%d", timeline);

	if (!send_position_row(sock, lsn, tliStr))
	{
		log_error("cmd_base_backup: failed sending the start position row");
		fail_stream(sock);
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
		log_error("cmd_base_backup: failed sending the tablespace result set");
		fail_stream(sock);
		return;
	}

	if (!ws_send_copy_out_response(sock, 0))
	{
		log_error("cmd_base_backup: failed sending the tar CopyOutResponse");
		fail_stream(sock);
		return;
	}

#if CBB_USE_ARCHIVE_FRAMING
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
			log_error("cmd_base_backup: failed sending the NewArchive framing message");
			fail_stream(sock);
			return;
		}
	}
#endif

	TarStreamCbContext ctx = { sock, true };

	/*
	 * backup_manifest sits at basebackupDir's own root (written there by
	 * the real pg_basebackup run that originally produced this on-disk
	 * backup -- see pg_basebackup_fetch()'s own comment, pgctl.c) and
	 * describes *that* pull, not the bytes being retransmitted here. Real
	 * Postgres never puts it in the main tar either: this file serves it
	 * as its own second CopyOut stream below (when the client asks for
	 * one, see this file's own BASE_BACKUP wire sequence comment), so it
	 * is excluded from the tar itself rather than let a stale copy of it
	 * land at the receiving node's $PGDATA/backup_manifest.
	 */
	static const char *excludeNames[] = { "backup_manifest" };

	if (!tar_stream_directory(basebackupDir, tar_chunk_cb, &ctx,
							  excludeNames, 1) || !ctx.ok)
	{
		log_error("Failed to stream base backup tar contents from \"%s\"",
				  basebackupDir);
		fail_stream(sock);
		return;
	}

#if CBB_USE_ARCHIVE_FRAMING

	/*
	 * PG15+: manifest within this *same* CopyOut stream -- see stream_
	 * manifest_as_copy_data()'s own comment for why (real Postgres sends
	 * every archive and the manifest under one CopyOutResponse/CopyDone
	 * pair, not a separate one per phase). manifestPath was already
	 * resolved and existence-checked above, before any bytes went out, so
	 * a request for a manifest that turns out not to exist fails cleanly
	 * with an ErrorResponse rather than partway through an already-started
	 * BASE_BACKUP.
	 */
	if (opts.manifestRequested &&
		!stream_manifest_as_copy_data(sock, manifestPath))
	{
		log_error("cmd_base_backup: failed streaming the manifest CopyData");
		fail_stream(sock);
		return;
	}

	if (!ws_send_copy_done(sock))
	{
		log_error("cmd_base_backup: failed sending the CopyDone");
		fail_stream(sock);
		return;
	}
#else

	/*
	 * Pre-PG15: the client has no concept of a manifest sharing the tar's
	 * own CopyOut stream at all -- it reads the stream as nothing but raw
	 * tar bytes until CopyDone, so folding the manifest in here (as the
	 * PG15+ branch above correctly does for a client that expects it) gets
	 * misread as corrupt tar content ("invalid tar block header size"), a
	 * real bug this branch exists to fix. End the tar's own CopyOut first,
	 * then -- if a manifest was requested -- open a second, independent
	 * CopyOutResponse/CopyDone pair for it, still with raw, untagged bytes
	 * (stream_manifest_as_copy_data()'s own #else branch already omits the
	 * 'm'/'d' tags for this same CBB_USE_ARCHIVE_FRAMING gate), matching
	 * what a pre-PG15 client's own two-COPY-phase BASE_BACKUP handling
	 * expects.
	 */
	if (!ws_send_copy_done(sock))
	{
		log_error("cmd_base_backup: failed sending the tar CopyDone");
		fail_stream(sock);
		return;
	}

	if (opts.manifestRequested)
	{
		if (!ws_send_copy_out_response(sock, 0))
		{
			log_error("cmd_base_backup: failed sending the manifest CopyOutResponse");
			fail_stream(sock);
			return;
		}

		if (!stream_manifest_as_copy_data(sock, manifestPath))
		{
			log_error("cmd_base_backup: failed streaming the manifest CopyData");
			fail_stream(sock);
			return;
		}

		if (!ws_send_copy_done(sock))
		{
			log_error("cmd_base_backup: failed sending the manifest CopyDone");
			fail_stream(sock);
			return;
		}
	}
#endif

	/*
	 * The end-of-backup position must be a real, currently-reachable target
	 * -- re-sending the same (potentially long-stale) start position here
	 * would tell a real pg_basebackup's own background WAL streamer
	 * (--wal-method=stream) to wait for a target it may have already
	 * passed hours ago, or, worse, one from a since-pruned segment it can
	 * never reach; either way its background thread hangs the whole
	 * command forever waiting on a position that will never legitimately
	 * arrive as "new" data. haveWalcacheInfo/walcacheEndLsn were already
	 * computed above, for the timeline-compatibility check -- reused here
	 * rather than scanning the walcache directory twice. Falls back to the
	 * start position only when the walcache is completely empty (no base
	 * backup should exist at all in that case), and reuses tliStr as-is:
	 * the check above already proved walcacheTimeline == timeline whenever
	 * haveWalcacheInfo is true.
	 */
	const char *endLsnPtr = haveWalcacheInfo ? walcacheEndLsn : lsn;

	if (!send_position_row(sock, endLsnPtr, tliStr))
	{
		log_error("cmd_base_backup: failed sending the end position row");
		fail_stream(sock);
		return;
	}

	ws_send_command_complete(sock, "BASE_BACKUP");
}
