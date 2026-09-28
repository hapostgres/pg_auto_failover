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
#include <time.h>

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
 */
bool ws_archive_cleanup_run(const char *routePath,
							bool haveKeepCount, int keepCount,
							bool haveKeepAge, WsRetentionAge keepAge,
							bool dryRun);

#endif /* WS_CLI_ARCHIVE_CLEANUP_H */
