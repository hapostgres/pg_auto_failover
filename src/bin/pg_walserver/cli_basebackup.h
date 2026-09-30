/*
 * src/bin/pg_walserver/cli_basebackup.h
 *   `pg_walserver basebackup`: a one-shot client that takes a real
 *   pg_basebackup of a route's upstream straight into
 *   "<path>/basebackups/<label>/", then atomically swaps
 *   "<path>/basebackups/.latest" to point at it -- the file
 *   cmd_base_backup.c's server side reads on every BASE_BACKUP request.
 *
 *   `basebackup --keep-count <N> --keep-age <interval>` (both optional;
 *   plain `basebackup` still behaves exactly as before with neither given)
 *   composes this with `archive-cleanup`'s own retention logic (ws_
 *   archive_cleanup_run(), cli_archive_cleanup.c/.h) so one cron line can
 *   both take a new backup and immediately prune what the retention policy
 *   no longer needs -- see cli_root.c's own cli_basebackup_command_run().
 *   Cleanup only ever runs after a *successful* backup, and a cleanup
 *   refusal (e.g. its WAL-continuity check finds a problem and --force
 *   wasn't given) only logs an error: it never discards or unreports the
 *   backup that was just taken. `archive-cleanup` itself remains fully
 *   independent and is unaffected by any of this.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_BASEBACKUP_H
#define WS_CLI_BASEBACKUP_H

#include <stdbool.h>

#include "commandline.h"

#include "cli_upstream.h"

/*
 * cli_basebackup_run takes a real pg_basebackup of target's upstream into a
 * freshly created "<target->path>/basebackups/<label>" directory (label:
 * a UTC timestamp, matching service_archiver_basebackup.c's own scheme so
 * both the standalone and pgaf-integrated backups sit side by side without
 * a naming collision), validates the result (backup_label and PG_VERSION
 * both present -- pg_basebackup itself already guarantees a well-formed
 * backup_label on a zero exit, this is a defense against a partial result
 * rather than a re-parse of it), and only then atomically swaps
 * "<target->path>/basebackups/.latest" to the new label. Never touches
 * .latest on failure: a route always keeps serving its previous,
 * known-good backup until a new one actually completes.
 *
 * Returns true on success (labelOut, when not NULL, receives the new
 * backup's own label), false with an error already logged otherwise.
 */
bool cli_basebackup_run(const WsUpstreamTarget *target,
						char *labelOut, size_t labelOutSize);

/*
 * cli_basebackup_route_has_backup returns true when
 * "<path>/basebackups/.latest" exists and is non-empty -- the same "does
 * this route already have a usable base backup" check cmd_base_backup.c's
 * own read_latest_basebackup_label() effectively makes (it additionally
 * validates the label's own character set, not needed for this plain
 * existence check). Used by accept_loop.c's own ws_bootstrap_missing_
 * backups() to decide which routes "pg_walserver serve" needs to take an
 * automatic bootstrap backup for.
 */
bool cli_basebackup_route_has_backup(const char *path);

extern CommandLine basebackup_command;

#endif /* WS_CLI_BASEBACKUP_H */
