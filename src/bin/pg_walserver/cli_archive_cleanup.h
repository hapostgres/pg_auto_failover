/*
 * src/bin/pg_walserver/cli_archive_cleanup.h
 *   `pg_walserver archive-cleanup --cluster <name> --pgdata <path> |
 *   --path <dir> [--keep-count <N>] [--keep-age <interval>] [--dry-run]`:
 *   a local, operator- or cron-driven retention tool for one route's own
 *   directory, mirroring real PostgreSQL's own `pg_archivecleanup` contrib
 *   tool -- same filename-prefix-extraction algorithm for `.partial`/
 *   `.backup` files (`SetWALFileNameForCleanup()`/`CleanupPriorWALFiles()`,
 *   `src/bin/pg_archivecleanup/pg_archivecleanup.c`), same
 *   ignore-the-timeline-byte-range string comparison -- extended to also
 *   retire base backups whose own required starting WAL segment has fallen
 *   before the retention cutoff, which stock `pg_archivecleanup` explicitly
 *   does not do (it has no notion of a base backup at all).
 *
 *   Purely local: unlike `archive-wal`/`restore-wal`, this never connects
 *   to a running `pg_walserver serve` over the wire (there is no
 *   ARCHIVE_FILE-shaped "delete" wire command, and deleting from a
 *   directory a running server is actively reading/writing needs no
 *   protocol round trip, only the same file the server itself already
 *   reads and writes directly) -- it resolves a route's own directory the
 *   same way `fetch-systemid`/`basebackup`/`setup` do
 *   (`--pgdata`/`--cluster`/`--path`), not the `WsWalServerTarget`
 *   (`cli_wal_target.h`) `archive-wal`/`restore-wal` use to reach
 *   `pg_walserver` itself -- there is no "pg_walserver instance" to
 *   connect to here at all, only a directory to prune.
 *
 *   Retention: **infinite by default**, exactly as vanilla PostgreSQL never
 *   runs `pg_archivecleanup` on its own (it is always operator-wired, via
 *   `archive_cleanup_command` or a cron job) -- at least one of
 *   `--keep-count`/`--keep-age` is required, refusing to run with neither
 *   (an accidental "delete everything" default would be actively
 *   dangerous). When both are given, whichever is more conservative
 *   (keeps more) wins: a base backup, or a WAL segment, is only ever
 *   removed when BOTH constraints independently agree it may go. The
 *   backup currently pointed to by `basebackups/.latest`, and every WAL
 *   segment its own `backup_label` requires, are never removed by either
 *   rule, regardless of age or count.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_ARCHIVE_CLEANUP_H
#define WS_CLI_ARCHIVE_CLEANUP_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "commandline.h"

#include "postgres_fe.h"

/*
 * WsBackupInfo is one enumerated base backup directory under
 * "<route path>/basebackups/" -- exported so "pg_walserver list backups"
 * (cli_list.c) can reuse this file's own enumeration (ws_backup_list_load(),
 * below) rather than re-deriving the same backup_label parsing/label-
 * timestamp-parsing logic a second time.
 */
typedef struct WsBackupInfo
{
	char label[NAMEDATALEN];        /* "basebackup-20260101T000000Z" */
	char dirPath[MAXPGPATH];
	time_t takenAt;                 /* parsed out of the label itself */
	bool haveStart;                 /* backup_label parsed successfully */
	char startSegment[25];          /* this backup's own required starting
	                                * WAL segment (24 hex digits + NUL) */
} WsBackupInfo;

/*
 * ws_backup_list_load scans "<routePath>/basebackups/" for backup
 * directories, parses each one's own label timestamp and (via
 * read_backup_label(), cmd_base_backup.c) its own required starting WAL
 * segment, and returns them sorted oldest-first (label strings sort
 * chronologically) in a freshly malloc'd array (free() it yourself).
 * Returns true even when there are zero backups (an empty, not-yet-used
 * route); false only on a directory that cannot be opened at all.
 */
bool ws_backup_list_load(const char *routePath, uint64_t segSize,
						 WsBackupInfo **backupsOut, int *countOut);

/*
 * A parsed --keep-age value: a bare count and one of the four required
 * suffixes. 'h'/'d'/'w' are fixed-length durations (3600/86400/604800
 * seconds respectively); 'm' is real calendar-month arithmetic (struct tm
 * plus timegm(), see cli_archive_cleanup.c's own ws_retention_age_cutoff())
 * -- NOT a fixed 30-day approximation, since month lengths vary.
 */
typedef struct WsRetentionAge
{
	long value;
	char unit;   /* 'h', 'd', 'w', or 'm' */
} WsRetentionAge;

/*
 * ws_parse_retention_age parses a --keep-age argument such as "72h",
 * "14d", "4w", "3m" into *age. An explicit suffix is required -- there is
 * no bare-number default, ambiguity here is worse than a clear error.
 * Returns false with an error already logged (naming the accepted
 * suffixes) on anything else.
 */
bool ws_parse_retention_age(const char *str, WsRetentionAge *age);

/*
 * ws_archive_cleanup_run prunes routePath (one route's own directory: WAL
 * segments/.partial/.backup files directly under it, base backups under
 * its "basebackups/" subdirectory) down to whatever haveKeepCount/
 * haveKeepAge (at least one must be true) ask to retain. dryRun logs what
 * would be removed without removing anything. Returns false, with an
 * error already logged, on a configuration problem (neither retention
 * flag given, an unreadable route directory, a ".latest" backup that
 * cannot be found or parsed) -- never partway through an unsafe removal.
 *
 * Before any deletion, a pre-flight WAL-continuity check (ws_check_wal_
 * continuity(), cli_archive_cleanup.c) verifies every kept backup can
 * still walk forward, with no missing segment, to wherever it needs to
 * reach (accounting for legitimate timeline switches via "%08X.history"
 * files). Always computed and logged, in both dry-run and a real run. On
 * a real run, finding a problem refuses the *entire* operation (nothing
 * deleted at all, not even otherwise-safe parts) unless force is true --
 * force exists for an operator who has independently verified proceeding
 * is safe (e.g. an independent backup, or an accepted/expected gap); a
 * default, unattended cron job should never blindly pass it. dryRun never
 * deletes anything regardless of force/continuity, but still returns
 * false when a problem was found and force was not given, so its own
 * exit status reflects what a real run would have refused to do.
 */
bool ws_archive_cleanup_run(const char *routePath,
							bool haveKeepCount, int keepCount,
							bool haveKeepAge, WsRetentionAge keepAge,
							bool dryRun, bool force);

extern CommandLine archive_cleanup_command;

#endif /* WS_CLI_ARCHIVE_CLEANUP_H */
