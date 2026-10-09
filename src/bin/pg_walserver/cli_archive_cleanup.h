/*
 * src/bin/pg_walserver/cli_archive_cleanup.h
 *   `pg_walserver archive-cleanup --cluster <name> --pgdata <path> |
 *   --path <dir> [--keep-count <N>] [--keep-age <interval>] [--dry-run]`:
 *   a local, operator- or cron-driven retention tool for one cluster's own
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
 *   reads and writes directly) -- it resolves a cluster's own directory the
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

#include "string_utils.h"

/*
 * WsBackupInfo/ws_backup_list_load() -- enumerating one cluster's own base
 * backup directories -- live in backup_list.h/.c, shared with
 * "pg_walserver list backups" (cli_list.c).
 */

bool ws_archive_cleanup_execute(const char *clusterPath,
								bool haveKeepCount, int keepCount,
								bool haveKeepAge, RetentionAge keepAge,
								bool dryRun, bool force);

extern CommandLine archive_cleanup_command;

#endif /* WS_CLI_ARCHIVE_CLEANUP_H */
