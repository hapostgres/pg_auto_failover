/*
 * src/bin/pg_walserver/cmd_start_replication.c
 *   See cmd_start_replication.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "port/pg_bswap.h"
#include "pqexpbuffer.h"

#include "cmd_replication_slot.h"
#include "cmd_start_replication.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "signals.h"
#include "wal_dir_scan.h"

#define WS_STREAM_CHUNK_SIZE (32 * 1024)
#define WS_KEEPALIVE_INTERVAL_SEC 5
#define WS_POLL_INTERVAL_USEC (200 * 1000)

/*
 * How often a slot's own "restart_lsn" is actually rewritten to disk as
 * StandbyStatusUpdate feedback keeps arriving -- a real standby/
 * pg_receivewal sends one every few hundred milliseconds to a few
 * seconds, far more often than a small marker file needs to be
 * rewritten. Matches WS_KEEPALIVE_INTERVAL_SEC's own cadence: no
 * particular reason they must be equal, just one less magic number to
 * track.
 */
#define WS_SLOT_FEEDBACK_PERSIST_INTERVAL_SEC 5

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


/*
 * WsSlotFeedbackState tracks, across one whole START_REPLICATION session,
 * the highest "flush" position a client's own StandbyStatusUpdate
 * messages have reported, and when that was last actually persisted to
 * the named slot's own marker file (cmd_replication_slot.h's
 * ws_replication_slot_update_restart_lsn()). route/slotName are set once
 * at session start; active is false for the (still by far the common)
 * case of no SLOT clause at all, in which case every function below is a
 * no-op.
 */
typedef struct WsSlotFeedbackState
{
	bool active;
	const WsRoute *route;
	char slotName[NAMEDATALEN];
	uint64_t restartLsn;      /* highest known-good value, in memory */
	time_t lastPersisted;
} WsSlotFeedbackState;


static void
append_int64(PQExpBuffer buf, int64_t v)
{
	uint64_t n = pg_hton64((uint64_t) v);

	appendBinaryPQExpBuffer(buf, (const char *) &n, 8);
}


/*
 * slot_feedback_persist writes slot's own current in-memory restartLsn to
 * its marker file right now, unconditionally (the throttling itself is
 * the caller's job -- see slot_feedback_apply()'s own comment, and this
 * function's own use at the end of cmd_start_replication() to flush one
 * final time on disconnect). A failed write is only ever logged, never
 * fatal to the stream itself: the marker file simply keeps whatever
 * value it already had, a strictly more conservative (never less
 * protective) outcome than losing the advance silently.
 */
static void
slot_feedback_persist(WsSlotFeedbackState *slot, time_t now)
{
	if (!slot->active)
	{
		return;
	}

	char lsnStr[32];

	sformat(lsnStr, sizeof(lsnStr), "%X/%08X",
			(uint32_t) (slot->restartLsn >> 32),
			(uint32_t) slot->restartLsn);

	if (!ws_replication_slot_update_restart_lsn(slot->route, slot->slotName,
												lsnStr))
	{
		log_warn("START_REPLICATION: failed to update slot \"%s\"'s own "
				 "restart_lsn to %s", slot->slotName, lsnStr);
	}

	slot->lastPersisted = now;
}


/*
 * slot_feedback_apply decodes payload as a StandbyStatusUpdate ('r')
 * message -- Byte1('r') Int64 write Int64 flush Int64 apply Int64
 * sendTime Byte1 replyRequested, the same wire shape a real walreceiver
 * sends periodically on its own -- and, when its own "flush" field is
 * genuinely an advance on what is already known, updates slot's own
 * in-memory restartLsn and, no more often than every
 * WS_SLOT_FEEDBACK_PERSIST_INTERVAL_SEC, persists it to the slot's own
 * marker file. restart_lsn tracks "flush", never "write" or "apply": the
 * same field a real walsender's own PhysicalConfirmReceivedLocation()
 * uses for exactly this purpose -- "flush" is what the standby has
 * durably fsync()'d, the point before which WAL is genuinely no longer
 * needed to protect it against its own crash. Silently does nothing for
 * any other message shape (an 'h' HotStandbyFeedback message, or a
 * malformed/short 'r' one) -- feedback is advisory input from a client
 * this project doesn't otherwise trust, never something a protocol
 * error should be raised over.
 */
static void
slot_feedback_apply(WsSlotFeedbackState *slot, const char *payload,
					int32_t payloadLen)
{
	if (!slot->active || payloadLen < 34 || payload[0] != 'r')
	{
		return;
	}

	uint64_t flushNet;

	memcpy(&flushNet, payload + 9, sizeof(flushNet)); /* IGNORE-BANNED */

	uint64_t flush = pg_ntoh64(flushNet);

	if (flush <= slot->restartLsn)
	{
		/* never regress -- an out-of-order or stale report is simply
		 * ignored, the same "restart_lsn only ever moves forward"
		 * invariant a real physical slot maintains */
		return;
	}

	slot->restartLsn = flush;

	time_t now = time(NULL);

	if (now - slot->lastPersisted >= WS_SLOT_FEEDBACK_PERSIST_INTERVAL_SEC)
	{
		slot_feedback_persist(slot, now);
	}
}


/*
 * send_xlogdata sends one CopyData-framed XLogData ('w') message: dataStart
 * and walEnd (the WAL positions this chunk covers) followed by data itself
 * -- the same wire shape a real walsender's own WALData message uses.
 * sendTime is always sent as zero: no client this project serves acts on
 * it.
 */
static bool
send_xlogdata(int sock, uint64_t dataStart, uint64_t walEnd,
			  const char *data, size_t len)
{
	PQExpBuffer buf = createPQExpBuffer();

	appendPQExpBufferChar(buf, 'w');   /* PqReplMsg_WALData */
	append_int64(buf, (int64_t) dataStart);
	append_int64(buf, (int64_t) walEnd);
	append_int64(buf, (int64_t) 0);   /* sendTime, not load-bearing here */
	appendBinaryPQExpBuffer(buf, data, len);

	bool ok = !PQExpBufferBroken(buf) && ws_send_copy_data(sock, buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/*
 * send_keepalive sends one CopyData-framed Primary keepalive ('k') message:
 * walEnd, sendTime (zero, unused), and replyRequested (always false --
 * this project never blocks a stream waiting for a standby status update).
 */
static bool
send_keepalive(int sock, uint64_t walEnd)
{
	PQExpBuffer buf = createPQExpBuffer();

	appendPQExpBufferChar(buf, 'k');   /* PqReplMsg_Keepalive */
	append_int64(buf, (int64_t) walEnd);
	append_int64(buf, (int64_t) 0);   /* sendTime */
	appendPQExpBufferChar(buf, 0);   /* replyRequested = false */

	bool ok = !PQExpBufferBroken(buf) && ws_send_copy_data(sock, buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


/*
 * wait_for_more_data_or_client waits up to WS_POLL_INTERVAL_USEC for
 * either more WAL bytes to become available or a message from the
 * client. A 'd' CopyData message is handed to slot_feedback_apply() --
 * a standby status update advances slot's own restart_lsn (see its own
 * comment); anything else is simply not a shape that function acts on.
 * Returns false when the client has disconnected/terminated or we've
 * been asked to stop, in which case the caller should end the stream.
 */
static bool
wait_for_more_data_or_client(int sock, uint64_t currentLsn,
							 time_t *lastKeepalive, WsSlotFeedbackState *slot)
{
	if (asked_to_stop || asked_to_stop_fast)
	{
		return false;
	}

	fd_set readSet;

	FD_ZERO(&readSet);
	FD_SET(sock, &readSet);

	struct timeval timeout = { 0, WS_POLL_INTERVAL_USEC };

	int selectRet = select(sock + 1, &readSet, NULL, NULL, &timeout);

	if (selectRet < 0 && errno != EINTR)
	{
		return false;
	}

	if (selectRet > 0 && FD_ISSET(sock, &readSet))
	{
		char type;
		char *payload = NULL;
		int32_t payloadLen = 0;

		if (!ws_read_message(sock, &type, &payload, &payloadLen,
							 WS_MAX_COMMAND_MESSAGE_LEN))
		{
			free(payload);
			return false;   /* client disconnected */
		}

		if (type == 'd')   /* CopyData: a standby status update, maybe */
		{
			slot_feedback_apply(slot, payload, payloadLen);
		}

		free(payload);

		if (type == 'X' || type == 'c')   /* Terminate or CopyDone */
		{
			return false;
		}
	}

	time_t now = time(NULL);

	if (now - *lastKeepalive >= WS_KEEPALIVE_INTERVAL_SEC)
	{
		if (!send_keepalive(sock, currentLsn))
		{
			return false;
		}

		*lastKeepalive = now;
	}

	return true;
}


/*
 * trim_trailing_zeros returns the length of buffer with any trailing run of
 * zero bytes removed. A ".partial" segment is pre-allocated to its full
 * the route's segment size by pg_receivewal the moment it's created (matching
 * real Postgres's own WAL file pre-allocation, XLogFileInitInternal) --
 * unlike a real primary's own walsender, which only ever knows about bytes
 * it has actually flushed, a plain fread() from a ".partial" file cannot
 * tell real WAL content apart from the not-yet-written tail, which reads
 * back as zeros. Sending that tail as if it were real WAL data is exactly
 * what a real standby's own recovery logic detects as "invalid record
 * length ... got 0" -- and, on that response, terminates its walreceiver
 * outright rather than treating it as "no more data yet, retry" (which is
 * pg_receivewal's own polling behavior, so it never noticed).
 *
 * Trimming any trailing zero run before ever sending it means an in-
 * progress chunk boundary is re-read (and re-trimmed) on the next
 * iteration rather than shipped as real data -- self-correcting, at worst
 * a few bytes of redundant re-reads per tick, never sent out early.
 */
static size_t
trim_trailing_zeros(const char *buffer, size_t len)
{
	while (len > 0 && buffer[len - 1] == 0)
	{
		len--;
	}

	return len;
}


/*
 * find_oldest_segno scans walcacheDir for the lowest-numbered WAL segment
 * present on the given timeline (complete or still ".partial" -- either
 * counts as "this archiver has it"). Returns false (*oldestSegno untouched)
 * if nothing has been captured on that timeline at all yet.
 *
 * This is what lets the main streaming loop below tell "the requested
 * segment hasn't been captured *yet*" (segno >= oldest present -- normal,
 * just wait) apart from "the requested segment predates everything this
 * archiver has ever captured" (segno < oldest present -- a real, permanent
 * gap, not a timing issue): pg_receivewal has no replication slot before
 * this project's own recent fix (service_archiver_start_pgreceivewal(),
 * pg_autoctl's service_archiver.c), so a pg_receivewal whose very first
 * connection attempt loses the startup HBA-propagation race restarts
 * streaming from the server's then-current position instead of resuming,
 * silently skipping every segment in between -- observed in practice
 * during end-to-end testing. Without this check, a
 * client asking to stream from inside that permanent gap (e.g. a real pg_
 * basebackup's own --wal-method=stream background receiver, replaying from
 * the position a BASE_BACKUP response advertised) would sit in this file's
 * own wait_for_more_data_or_client() loop forever, waiting for a segment
 * that can never arrive.
 */
static bool
find_oldest_segno(const char *walcacheDir, uint32_t timeline,
				  uint64_t segSize, uint64_t *oldestSegno)
{
	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		return false;
	}

	bool found = false;
	uint64_t best = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		size_t len = strlen(entry->d_name);
		char segPart[25] = { 0 };

		if (len == 24)
		{
			memcpy(segPart, entry->d_name, 24); /* IGNORE-BANNED */
		}
		else if (len == 24 + 8 && streq(entry->d_name + 24, ".partial"))
		{
			memcpy(segPart, entry->d_name, 24); /* IGNORE-BANNED */
		}
		else
		{
			continue;
		}

		bool isHex = true;

		for (size_t i = 0; i < 24 && isHex; i++)
		{
			isHex = isxdigit((unsigned char) segPart[i]);
		}

		if (!isHex)
		{
			continue;
		}

		char tliHex[9] = { 0 };

		memcpy(tliHex, segPart, 8); /* IGNORE-BANNED */

		if ((uint32_t) strtoul(tliHex, NULL, 16) != timeline)
		{
			continue;
		}

		char logIdHex[9] = { 0 };
		char segHex[9] = { 0 };

		memcpy(logIdHex, segPart + 8, 8); /* IGNORE-BANNED */
		memcpy(segHex, segPart + 16, 8); /* IGNORE-BANNED */

		uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
		uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);
		uint64_t segno = (uint64_t) logId *
						 (UINT64CONST(0x100000000) / segSize) + seg;

		if (!found || segno < best)
		{
			best = segno;
			found = true;
		}
	}

	closedir(dir);

	if (found)
	{
		*oldestSegno = best;
	}

	return found;
}


void
cmd_start_replication(int sock, const WsRoute *route,
					  const char *slotName, uint64_t startLsn,
					  bool haveTimeline, uint32_t timeline)
{
	if (route == NULL || route->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	WsSlotFeedbackState slotState = { 0 };

	if (slotName != NULL && slotName[0] != '\0')
	{
		/* SLOT of a slot that does not exist is refused, the same
		 * requirement a real walsender enforces (never a silent no-op) --
		 * this must happen before ws_send_copy_both_response() below,
		 * same as the "no WAL cache directory" check above: once CopyBoth
		 * starts, an error can no longer be a plain ErrorResponse */
		if (!ws_replication_slot_exists(route, slotName))
		{
			ws_send_error_response(sock, "42704",
								   "replication slot does not exist");
			return;
		}

		slotState.active = true;
		slotState.route = route;
		strlcpy(slotState.slotName, slotName, sizeof(slotState.slotName));
		slotState.lastPersisted = time(NULL);

		char restartLsnStr[32] = { 0 };

		if (ws_replication_slot_read_restart_lsn(route, slotName, restartLsnStr,
												 sizeof(restartLsnStr)))
		{
			uint32_t hi, lo;

			if (sscanf(restartLsnStr, "%X/%X", &hi, &lo) == 2) /* IGNORE-BANNED */
			{
				slotState.restartLsn = ((uint64_t) hi << 32) | lo;
			}
		}
	}

	if (!haveTimeline)
	{
		char discardLsn[32] = { 0 };

		if (!wal_position_cache_read(route->path, &timeline, discardLsn,
									 sizeof(discardLsn)))
		{
			(void) wal_dir_find_latest(route, &timeline, discardLsn,
									   sizeof(discardLsn));
		}
	}

	if (!ws_send_copy_both_response(sock, 0))
	{
		ws_connection_close_after_command = true;
		return;
	}

	log_info("START_REPLICATION: streaming from %X/%08X on timeline %u "
			 "from \"%s\"",
			 (uint32_t) (startLsn >> 32), (uint32_t) startLsn, timeline,
			 route->path);

	uint64_t segSize = ws_route_wal_segment_size(route);
	uint64_t segno = startLsn / segSize;
	uint64_t offset = startLsn % segSize;
	uint64_t currentLsn = startLsn;
	time_t lastKeepalive = time(NULL);

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		char filename[32];

		wal_segment_filename(timeline, segno, segSize, filename,
							 sizeof(filename));

		char completePath[MAXPGPATH];

		sformat(completePath, sizeof(completePath), "%s/%s",
				route->path, filename);

		bool isComplete = file_exists(completePath);

		char partialPath[MAXPGPATH];

		sformat(partialPath, sizeof(partialPath), "%s.partial", completePath);

		const char *readPath = isComplete ? completePath : partialPath;

		if (!isComplete && !file_exists(partialPath))
		{
			uint64_t oldestSegno;

			if (find_oldest_segno(route->path, timeline, segSize,
								  &oldestSegno) &&
				segno < oldestSegno)
			{
				char oldestName[32];

				wal_segment_filename(timeline, oldestSegno, segSize,
									 oldestName, sizeof(oldestName));

				log_error("START_REPLICATION: requested segment \"%s\" "
						  "predates the oldest segment this archiver has "
						  "captured (\"%s\") -- it was never captured and "
						  "can never become available, refusing to wait "
						  "forever for it",
						  filename, oldestName);

				ws_send_error_response(sock, "58P01",
									   "requested WAL segment predates this "
									   "archiver's captured history and will "
									   "never become available");

				/* an error inside CopyBoth ends the connection */
				ws_connection_close_after_command = true;
				return;
			}

			/* nothing captured for this segment yet -- wait for it */
			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive,
											  &slotState))
			{
				break;
			}

			continue;
		}

		FILE *file = fopen(readPath, "rb"); /* IGNORE-BANNED */

		if (file == NULL)
		{
			log_warn("Failed to open \"%s\": %m (will retry)", readPath);

			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive,
											  &slotState))
			{
				break;
			}

			continue;
		}

		if (fseeko(file, (off_t) offset, SEEK_SET) != 0)
		{
			log_error("Failed to seek to offset %" PRIu64 " in \"%s\": %m",
					  offset, readPath);
			fclose(file);
			ws_send_error_response(sock, "58030",
								   "failed to read the requested WAL segment");
			ws_connection_close_after_command = true;
			return;
		}

		char buffer[WS_STREAM_CHUNK_SIZE];
		size_t got = fread(buffer, 1, sizeof(buffer), file);

		fclose(file);

		if (!isComplete)
		{
			got = trim_trailing_zeros(buffer, got);
		}

		if (got == 0)
		{
			if (isComplete)
			{
				/* fully drained this now-complete segment: move on */
				segno++;
				offset = 0;
				continue;
			}

			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive,
											  &slotState))
			{
				break;
			}

			continue;
		}

		if (!send_xlogdata(sock, currentLsn, currentLsn + got, buffer, got))
		{
			break;   /* client gone */
		}

		currentLsn += got;
		offset += got;

		if (offset >= segSize)
		{
			segno++;
			offset = 0;
		}
	}

	/*
	 * One final, unthrottled flush of whatever feedback arrived since the
	 * last periodic write: a client that cleanly ends its own session
	 * right after its last StandbyStatusUpdate (e.g. pg_basebackup's
	 * --wal-method=stream background receiver, reaching its target LSN
	 * and disconnecting) would otherwise lose up to
	 * WS_SLOT_FEEDBACK_PERSIST_INTERVAL_SEC seconds of real progress.
	 */
	slot_feedback_persist(&slotState, time(NULL));

	(void) ws_send_copy_done(sock);

	/*
	 * Real walsender.c's own controlled-shutdown path (WalSndDone) follows
	 * CopyDone with a CommandComplete tagged "COPY" before returning to
	 * the command loop -- required protocol, not optional decoration: a
	 * real client's receivelog.c (ReceiveXlogStream) only accepts an
	 * ended stream as a *successful* stop when it can read a matching
	 * PGRES_COMMAND_OK result afterward; without it, a client that decided
	 * on its own to stop here (e.g. pg_basebackup's --wal-method=stream
	 * background receiver, once it reaches its target LSN) falls through
	 * to "unexpected termination of replication stream" and exits
	 * non-zero, even though nothing on the wire was actually wrong. A
	 * genuinely long-lived streaming client (real walreceiver, primary_
	 * conninfo) never triggers this path at all -- it never decides to
	 * stop on its own -- which is why this went unnoticed until a real
	 * pg_basebackup was tested end to end.
	 */
	(void) ws_send_command_complete(sock, "COPY");

	log_info("START_REPLICATION: stream ended at %X/%08X",
			 (uint32_t) (currentLsn >> 32), (uint32_t) currentLsn);
}
