/*
 * src/bin/pg_autoctl/service_archiver_wal_scanner.c
 *   See service_archiver_wal_scanner.h.
 *
 * A dedicated, permanently-supervised process per membership, sibling of
 * that membership's own "archiver-capture-<formation>-<group>" (service_
 * archiver.c) and "archiver-pgreceivewal-ctl-<formation>-<group>" (service_
 * archiver_pgreceivewal_ctl.c) -- forked directly by service_archiver_
 * reconciler.c the same way those two already are.
 *
 * Its one job: periodically readdir() this membership's own WAL cache
 * directory and feed every completed segment it finds through the exact
 * same WAL-notify socket pg_receivewal's own WalSegmentClosedHook already
 * writes to (archiver_wal_notify.c) -- from the listener's side (service_
 * archiver.c's service_archiver_report_captured_wal()) a scan-discovered
 * segment and a live-captured one are indistinguishable, both just another
 * line on the socket, batched into the same bulk monitor report.
 *
 * This replaces what used to be an inline directory scan on the FSM tick
 * loop itself, run every ARCHIVER_WAL_FALLBACK_SCAN_TICKS ticks. Moving it
 * to its own process fixes a real problem with that: a full readdir() over
 * a WAL cache retaining thousands of segments is real wall-clock work, and
 * running it inline meant every 60th tick paid that cost before the tick
 * loop could do anything else (report FSM state, generate a base backup,
 * ...). As its own process, the scan cadence is now fully decoupled from
 * the FSM tick's own cadence.
 *
 * Own high-water mark, local to this process only. Restarting this process
 * re-sends already-known segments once -- harmless, the monitor side (ON
 * CONFLICT DO NOTHING) treats that as a no-op regardless of arrival order
 * (service_archiver.c's own drain no longer keeps a high-water mark of its
 * own either, for exactly that reason -- see its own comment).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "service_archiver_wal_scanner.h"

#include "archiver_systemid.h"
#include "archiver_wal_notify.h"
#include "defaults.h"
#include "log.h"
#include "signals.h"
#include "string_utils.h"

/*
 * How often this process actually re-scans the WAL cache directory, in
 * seconds -- a wall-clock gate, not a tick count, so it stays correct
 * regardless of how often this loop's own outer wait wakes up (kept short,
 * see ARCHIVER_WAL_SCANNER_POLL_SECONDS's own comment, so asked_to_stop is
 * noticed promptly).
 */
#define ARCHIVER_WAL_SCANNER_INTERVAL_SECONDS 30

/*
 * How often the outer loop wakes up to check asked_to_stop -- independent
 * of, and much shorter than, ARCHIVER_WAL_SCANNER_INTERVAL_SECONDS, so
 * this process reacts to a shutdown request quickly rather than sleeping
 * through it for up to a full scan interval.
 */
#define ARCHIVER_WAL_SCANNER_POLL_SECONDS 1

/* matches service_archiver.c's own layout constants (duplicated rather
 * than shared, matching that file's own precedent for this exact bit of
 * arithmetic -- see its own header comment on why) */
#define ARCHIVER_WAL_FNAME_LEN 24
#define ARCHIVER_WAL_SEGMENT_SIZE ((uint64_t) 0x1000000)
#define ARCHIVER_XLOG_SEGMENTS_PER_XLOGID \
	(((uint64_t) 0x100000000) / ARCHIVER_WAL_SEGMENT_SIZE)


static bool
is_wal_segment_filename(const char *name)
{
	size_t len = strlen(name);

	if (len != ARCHIVER_WAL_FNAME_LEN)
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


static int
wal_filename_compare(const void *a, const void *b)
{
	const char *nameA = *(const char *const *) a;
	const char *nameB = *(const char *const *) b;

	return strcmp(nameA, nameB);
}


static void
wal_segment_end_lsn(const char *walFileName, char *lsn, size_t lsnSize)
{
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(logIdHex, walFileName + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, walFileName + 16, 8); /* IGNORE-BANNED */

	uint32_t logId = (uint32_t) strtoul(logIdHex, NULL, 16);
	uint32_t seg = (uint32_t) strtoul(segHex, NULL, 16);

	uint64_t segno = (uint64_t) logId * ARCHIVER_XLOG_SEGMENTS_PER_XLOGID + seg;
	uint64_t position = segno * ARCHIVER_WAL_SEGMENT_SIZE + ARCHIVER_WAL_SEGMENT_SIZE;

	sformat(lsn, lsnSize, "%X/%08X",
			(uint32_t) (position >> 32),
			(uint32_t) (position & 0xFFFFFFFF));
}


/*
 * archiver_wal_scan_once does one full readdir() pass over walcacheDir and
 * sends every completed segment newer than *highWaterMark to socketPath,
 * advancing *highWaterMark as it goes -- best-effort throughout (a send
 * failure just means that one segment is skipped this round, picked up
 * again next round, matching archiver_wal_notify_send_segment()'s own best-effort
 * contract).
 */
static void
archiver_wal_scan_once(const char *walcacheDir, const char *socketPath,
					   uint64_t systemIdentifier,
					   char *highWaterMark, size_t highWaterMarkSize)
{
	DIR *dir = opendir(walcacheDir);

	if (dir == NULL)
	{
		return;
	}

	char **names = NULL;
	int count = 0;
	int capacity = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (!is_wal_segment_filename(entry->d_name))
		{
			continue;
		}

		if (strcmp(entry->d_name, highWaterMark) <= 0)
		{
			continue;
		}

		if (count == capacity)
		{
			capacity = capacity == 0 ? 16 : capacity * 2;
			names = realloc(names, capacity * sizeof(char *));
		}

		names[count++] = strdup(entry->d_name);
	}

	closedir(dir);

	if (count == 0)
	{
		return;
	}

	pg_qsort(names, count, sizeof(char *), wal_filename_compare);

	for (int i = 0; i < count; i++)
	{
		char lsn[PG_LSN_MAXLENGTH] = { 0 };

		wal_segment_end_lsn(names[i], lsn, sizeof(lsn));

		if (archiver_wal_notify_send_segment(socketPath, names[i], lsn,
									 systemIdentifier))
		{
			strlcpy(highWaterMark, names[i], highWaterMarkSize);
		}

		free(names[i]);
	}

	free(names);
}


/*
 * service_archiver_wal_scanner_loop is this process's own body: every
 * ARCHIVER_WAL_SCANNER_INTERVAL_SECONDS, scan and notify -- until asked to
 * stop.
 */
static void
service_archiver_wal_scanner_loop(KeeperConfig *config)
{
	char socketPath[MAXPGPATH] = { 0 };
	char highWaterMark[ARCHIVER_WAL_FNAME_LEN + 1] = { 0 };
	time_t lastScannedAt = 0;

	(void) archiver_wal_notify_socket_path(config, socketPath,
										   sizeof(socketPath));

	for (;;)
	{
		if (asked_to_stop || asked_to_stop_fast || asked_to_quit)
		{
			exit(EXIT_CODE_QUIT);
		}

		time_t now = time(NULL);

		if (lastScannedAt == 0 ||
			(now - lastScannedAt) >= ARCHIVER_WAL_SCANNER_INTERVAL_SECONDS)
		{
			lastScannedAt = now;

			uint64_t systemIdentifier = 0;

			if (archiver_systemid_read(config, &systemIdentifier))
			{
				archiver_wal_scan_once(config->pgSetup.pgdata, socketPath,
									   systemIdentifier,
									   highWaterMark, sizeof(highWaterMark));
			}
		}

		pg_usleep(ARCHIVER_WAL_SCANNER_POLL_SECONDS * 1000 * 1000);
	}
}


/*
 * service_archiver_wal_scanner_start forks this process -- matching
 * service_archiver_pgreceivewal_ctl_start()'s own fork-without-exec shape:
 * this is what service_archiver_reconciler.c supervises directly, as a
 * sibling of that same membership's own capture and pg_receivewal
 * controller processes, not their child.
 */
bool
service_archiver_wal_scanner_start(void *context, pid_t *pid)
{
	Keeper *keeper = (Keeper *) context;

	fflush(stdout);
	fflush(stderr);

	pid_t fpid = fork();

	switch (fpid)
	{
		case -1:
		{
			log_error("Failed to fork the archiver WAL scanner process");
			return false;
		}

		case 0:
		{
			(void) set_signal_handlers(false);
			(void) set_ps_title("pg_autoctl: archiver wal scanner");

			(void) service_archiver_wal_scanner_loop(&(keeper->config));

			/* unreachable: the loop only ever exit()s directly */
			exit(EXIT_CODE_INTERNAL_ERROR);
		}

		default:
		{
			log_debug("pg_autoctl archiver WAL scanner started in "
					  "subprocess %d", fpid);
			*pid = fpid;
			return true;
		}
	}
}
