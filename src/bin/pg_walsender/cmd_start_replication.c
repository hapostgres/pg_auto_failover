/*
 * src/bin/pg_walsender/cmd_start_replication.c
 *   See cmd_start_replication.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "port/pg_bswap.h"
#include "pqexpbuffer.h"

#include "cmd_start_replication.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "signals.h"
#include "wal_dir_scan.h"

#define WS_WAL_SEGMENT_SIZE UINT64CONST(0x1000000)
#define WS_STREAM_CHUNK_SIZE (32 * 1024)
#define WS_KEEPALIVE_INTERVAL_SEC 5
#define WS_POLL_INTERVAL_USEC (200 * 1000)


static void
append_int64(PQExpBuffer buf, int64_t v)
{
	uint64_t n = pg_hton64((uint64_t) v);

	appendBinaryPQExpBuffer(buf, (const char *) &n, 8);
}


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
 * either more WAL bytes to become available or a message from the client,
 * draining (and ignoring the content of) any standby status update the
 * client sends meanwhile -- this project has no cascading/retention logic
 * that needs to react to it yet. Returns false when the client has
 * disconnected/terminated or we've been asked to stop, in which case the
 * caller should end the stream.
 */
static bool
wait_for_more_data_or_client(int sock, uint64_t currentLsn, time_t *lastKeepalive)
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

		if (!ws_read_message(sock, &type, &payload, &payloadLen))
		{
			free(payload);
			return false;   /* client disconnected */
		}

		free(payload);

		if (type == 'X' || type == 'c')   /* Terminate or CopyDone */
		{
			return false;
		}

		/* 'd' CopyData: a standby status update / hot-standby feedback we
		 * don't act on yet -- already consumed above, nothing more to do */
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
 * WS_WAL_SEGMENT_SIZE by pg_receivewal the moment it's created (matching
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


static bool
parse_lsn(const char *s, uint64_t *lsn, const char **endptr)
{
	char *afterHi;
	unsigned long hi = strtoul(s, &afterHi, 16);

	if (afterHi == s || *afterHi != '/')
	{
		return false;
	}

	char *afterLo;
	unsigned long lo = strtoul(afterHi + 1, &afterLo, 16);

	if (afterLo == afterHi + 1)
	{
		return false;
	}

	*lsn = ((uint64_t) hi << 32) | (uint32_t) lo;
	*endptr = afterLo;

	return true;
}


static const char *
skip_ws(const char *p)
{
	while (isspace((unsigned char) *p))
	{
		p++;
	}

	return p;
}


void
cmd_start_replication(int sock, const WsRoute *route, const char *rawArgs)
{
	if (route == NULL || route->walcacheDir[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	const char *p = skip_ws(rawArgs);

	if (strncasecmp(p, "SLOT", 4) == 0 && isspace((unsigned char) p[4]))
	{
		p = skip_ws(p + 4);

		/* consume a possibly-quoted slot name, positioning is unaffected
		 * by which slot (if any) was named -- see this file's own header
		 * comment on why no real slot-based retention exists yet */
		if (*p == '"')
		{
			p++;
			while (*p && *p != '"')
			{
				p++;
			}
			if (*p == '"')
			{
				p++;
			}
		}
		else
		{
			while (*p && !isspace((unsigned char) *p))
			{
				p++;
			}
		}

		p = skip_ws(p);
	}

	if (strncasecmp(p, "PHYSICAL", 8) == 0 &&
		(isspace((unsigned char) p[8]) || p[8] == '\0'))
	{
		p = skip_ws(p + 8);
	}

	uint64_t startLsn;
	const char *after;

	if (!parse_lsn(p, &startLsn, &after))
	{
		ws_send_error_response(sock, "22023", "invalid or missing start LSN");
		return;
	}

	p = skip_ws(after);

	uint32_t timeline = (route->timeline > 0) ? (uint32_t) route->timeline : 1;

	if (strncasecmp(p, "TIMELINE", 8) == 0)
	{
		p = skip_ws(p + 8);
		timeline = (uint32_t) strtoul(p, NULL, 10);
	}

	if (!ws_send_copy_both_response(sock, 0))
	{
		return;
	}

	log_info("START_REPLICATION: streaming from %X/%08X on timeline %u "
			 "from \"%s\"",
			 (uint32_t) (startLsn >> 32), (uint32_t) startLsn, timeline,
			 route->walcacheDir);

	uint64_t segno = startLsn / WS_WAL_SEGMENT_SIZE;
	uint64_t offset = startLsn % WS_WAL_SEGMENT_SIZE;
	uint64_t currentLsn = startLsn;
	time_t lastKeepalive = time(NULL);

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast)
		{
			break;
		}

		char filename[32];

		wal_segment_filename(timeline, segno, filename, sizeof(filename));

		char completePath[MAXPGPATH];

		snprintf(completePath, sizeof(completePath), "%s/%s",
				 route->walcacheDir, filename);

		bool isComplete = file_exists(completePath);

		char partialPath[MAXPGPATH];

		snprintf(partialPath, sizeof(partialPath), "%s.partial", completePath);

		const char *readPath = isComplete ? completePath : partialPath;

		if (!isComplete && !file_exists(partialPath))
		{
			/* nothing captured for this segment yet -- wait for it */
			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive))
			{
				break;
			}

			continue;
		}

		FILE *file = fopen(readPath, "rb");

		if (file == NULL)
		{
			log_warn("Failed to open \"%s\": %m (will retry)", readPath);

			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive))
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
			break;
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

			if (!wait_for_more_data_or_client(sock, currentLsn, &lastKeepalive))
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

		if (offset >= WS_WAL_SEGMENT_SIZE)
		{
			segno++;
			offset = 0;
		}
	}

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
