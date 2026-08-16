/*
 * src/bin/pg_autoctl/archiver_wal_notify.c
 *   See archiver_wal_notify.h.
 *
 * A Unix domain socket, one per membership, at "<pgdata>/wal-notify.sock":
 * pg_receivewal's own WalSegmentClosedHook (vendor/pg_receivewal/pg_
 * receivewal.c, called from service_archiver_pgreceivewal_ctl.c's forked
 * child) connects, writes one line "<segment> <end-lsn> <sysid>\n", and
 * closes -- no persistent connection to manage across pg_receivewal's own
 * restarts or the listener's. The FSM tick (service_archiver_report_
 * captured_wal(), service_archiver.c) drains whatever's queued each tick
 * instead of scanning the WAL cache directory for it, and reports every
 * message drained in one tick to the monitor as a single bulk call
 * (monitor_report_wal_received_bulk()) rather than one round trip per
 * segment.
 *
 * Two producers share this exact same protocol and socket: pg_receivewal's
 * own hook above (live, per-segment, as WAL streams in) and this
 * membership's own periodic scanner process (service_archiver_wal_
 * scanner.c, a full readdir() of the WAL cache at a cadence independent of
 * the FSM tick loop). From the listener's side a scan-discovered segment
 * and a live-captured one are indistinguishable -- both are just another
 * line on the socket, batched into the same bulk report.
 *
 * Durability: a plain socket has none on its own -- a segment closing
 * while nothing is listening (the FSM tick process restarting, a listener
 * not opened yet) is simply never sent, no retry, no queueing on the
 * sender's side. That's deliberate, not an oversight: the scanner process
 * above exists specifically as the bounded correctness backstop for
 * exactly this gap, at a cadence far below every tick. This socket's own
 * job is shaving near-unbounded per-tick scan cost down to near-zero for
 * the common case, not replacing the guarantee that every segment is
 * *eventually* reported even if a notification is dropped.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "archiver_wal_notify.h"

#include "file_utils.h"
#include "log.h"
#include "string_utils.h"

/*
 * How long a single accepted connection's read may block before this
 * process gives up on it and moves on -- senders write one short line and
 * close immediately, so this only ever matters for a misbehaving or
 * stalled sender, never a well-formed notification.
 */
#define ARCHIVER_WAL_NOTIFY_RECV_TIMEOUT_MS 200


void
archiver_wal_notify_socket_path(KeeperConfig *config, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/wal-notify.sock", config->pgSetup.pgdata);
}


/*
 * archiver_wal_notify_send is called from inside pg_receivewal's own
 * process (the WalSegmentClosedHook) -- best-effort, never fatal: a
 * connect() failure (nobody listening yet, or the listener is mid-
 * restart) just means this one notification is skipped, silently, in
 * favor of the fallback scan noticing it later (see this file's own
 * header comment).
 */
bool
archiver_wal_notify_send(const char *socketPath, const char *walFileName,
						 const char *lsn, uint64_t systemIdentifier)
{
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sock < 0)
	{
		return false;
	}

	struct sockaddr_un addr = { 0 };

	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, socketPath, sizeof(addr.sun_path));

	if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		/* nothing listening yet -- expected, not an error, see header */
		close(sock);
		return false;
	}

	char line[BUFSIZE] = { 0 };
	int len = sformat(line, sizeof(line), "%s %s %" PRIu64 "\n",
					  walFileName, lsn, systemIdentifier);

	ssize_t written = write(sock, line, len);

	close(sock);

	return written == len;
}


/*
 * archiver_wal_notify_listener_open creates and binds the listening
 * socket -- unlinking a stale socket file first (a previous process
 * instance's own, never cleaned up after a crash: binding to an existing
 * path fails outright otherwise, and an actually-live listener at that
 * path would mean two processes trying to own the same membership at
 * once, already a problem this project's own reconciler-restart handling
 * (service_archiver_reconciler.c) is responsible for preventing upstream
 * of this, not something this file needs to re-detect).
 */
bool
archiver_wal_notify_listener_open(KeeperConfig *config,
								  ArchiverWalNotifyListener *listener)
{
	archiver_wal_notify_socket_path(config, listener->socketPath,
									sizeof(listener->socketPath));

	(void) unlink_file(listener->socketPath);

	int sock = socket(AF_UNIX, SOCK_STREAM, 0);

	if (sock < 0)
	{
		log_error("Failed to create the WAL-notify socket: %m");
		return false;
	}

	struct sockaddr_un addr = { 0 };

	addr.sun_family = AF_UNIX;
	strlcpy(addr.sun_path, listener->socketPath, sizeof(addr.sun_path));

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		log_error("Failed to bind the WAL-notify socket at \"%s\": %m",
				  listener->socketPath);
		close(sock);
		return false;
	}

	if (listen(sock, 16) != 0)
	{
		log_error("Failed to listen on the WAL-notify socket at \"%s\": %m",
				  listener->socketPath);
		close(sock);
		return false;
	}

	int flags = fcntl(sock, F_GETFL, 0);

	if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		log_error("Failed to set the WAL-notify socket non-blocking: %m");
		close(sock);
		return false;
	}

	listener->listenFd = sock;

	return true;
}


void
archiver_wal_notify_listener_close(ArchiverWalNotifyListener *listener)
{
	if (listener->listenFd >= 0)
	{
		close(listener->listenFd);
		listener->listenFd = -1;
	}

	(void) unlink_file(listener->socketPath);
}


/*
 * archiver_wal_notify_listener_drain accepts and processes every
 * connection currently queued, non-blocking (returns immediately once the
 * backlog is empty) -- meant to be called once per FSM tick, a bounded
 * amount of work regardless of how many segments this project's own WAL
 * cache retains in total (unlike the scan it replaces, this only ever
 * costs work proportional to segments captured *since the last tick*).
 */
bool
archiver_wal_notify_listener_drain(ArchiverWalNotifyListener *listener,
								   ArchiverWalNotifyCallback callback,
								   void *context)
{
	for (;;)
	{
		int fd = accept(listener->listenFd, NULL, NULL);

		if (fd < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				/* backlog empty -- done draining for this tick */
				return true;
			}

			if (errno == EINTR)
			{
				continue;
			}

			log_warn("Failed to accept a WAL-notify connection: %m");
			return true;
		}

		struct timeval timeout = {
			.tv_sec = ARCHIVER_WAL_NOTIFY_RECV_TIMEOUT_MS / 1000,
			.tv_usec = (ARCHIVER_WAL_NOTIFY_RECV_TIMEOUT_MS % 1000) * 1000
		};

		(void) setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
						  &timeout, sizeof(timeout));

		char line[BUFSIZE] = { 0 };
		ssize_t got = read(fd, line, sizeof(line) - 1);

		close(fd);

		if (got <= 0)
		{
			continue;
		}

		line[got] = '\0';

		char walFileName[MAXPGPATH] = { 0 };
		char lsn[PG_LSN_MAXLENGTH] = { 0 };
		uint64_t systemIdentifier = 0;

		if (sscanf(line, "%1023s %17s %" SCNu64, /* IGNORE-BANNED */
				  walFileName, lsn, &systemIdentifier) != 3)
		{
			log_warn("Failed to parse WAL-notify message: \"%s\"", line);
			continue;
		}

		if (!callback(context, walFileName, lsn, systemIdentifier))
		{
			return false;
		}
	}
}
