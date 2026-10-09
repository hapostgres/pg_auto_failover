/*
 * src/bin/pg_walserver/cli_basebackup.h
 *   `pg_walserver basebackup`: a one-shot client that takes a real
 *   pg_basebackup of a cluster's upstream straight into
 *   "<path>/basebackups/<label>/", then atomically swaps
 *   "<path>/basebackups/.latest" to point at it -- the file
 *   cmd_base_backup.c's server side reads on every BASE_BACKUP request.
 *
 *   `basebackup --keep-count <N> --keep-age <interval>` (both optional;
 *   plain `basebackup` still behaves exactly as before with neither given)
 *   composes this with `archive-cleanup`'s own retention logic (ws_
 *   archive_cleanup_execute(), cli_archive_cleanup.c/.h) so one cron line can
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

bool cli_basebackup_run(const WsUpstreamTarget *target,
						char *labelOut, size_t labelOutSize);

bool cli_basebackup_cluster_has_backup(const char *path);

extern CommandLine basebackup_command;

#endif /* WS_CLI_BASEBACKUP_H */
