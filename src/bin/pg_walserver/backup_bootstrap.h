/*
 * src/bin/pg_walserver/backup_bootstrap.h
 *   A one-shot, plain fork() (no execv()) that takes a single route's very
 *   first base backup in the background, reusing `pg_walserver basebackup`'s
 *   own logic (cli_basebackup.c) in-process -- see backup_bootstrap.c's own
 *   header comment for the full design, and accept_loop.c's own
 *   ws_bootstrap_missing_backups() for who calls this and when.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_BACKUP_BOOTSTRAP_H
#define WS_BACKUP_BOOTSTRAP_H

#include <stdbool.h>
#include <sys/types.h>

#include "postgres_fe.h"

#include "routes.h"

/*
 * ws_backup_bootstrap_start forks a plain child (no execv(): this is a
 * one-time transient operation, not a long-lived service needing capture.
 * c's own fork()+execv()-for-live-upgrade treatment) that takes route's
 * first base backup and exits -- never blocks the caller beyond the
 * fork() call itself. Returns true with *pidOut set once the child has
 * been forked (the caller is responsible for eventually reaping it, the
 * same way it already reaps every other child it forks); false, with an
 * error already logged, only if fork() itself failed.
 *
 * The child, in order:
 *
 *   - for a "capture = pull" route, waits (bounded, see backup_bootstrap.c's
 *     own WS_BOOTSTRAP_STREAM_WAIT_* constants) for wal_dir_has_any_segment()
 *     to become true against route's own real, already-started, supervised
 *     capturer (capture.c) -- never a throwaway primer, unlike the removed
 *     "setup --with-basebackup" design this replaces: by the time this
 *     function is ever called, "serve" has already started (or already
 *     reconciled, on reload) route's own real capturer, so there is always
 *     a genuine one to wait on directly;
 *   - takes the backup itself (cli_basebackup_run(), cli_basebackup.c),
 *     retried up to WS_BOOTSTRAP_BACKUP_MAX_ATTEMPTS times with a short
 *     delay between attempts -- bounded, never an infinite retry loop;
 *   - logs a clear error and exits nonzero on final failure. The route
 *     keeps serving whatever it already has either way; an operator's own
 *     "pg_walserver basebackup" (or their own cron job around it) is what
 *     eventually gets such a route a backup -- this project provides the
 *     facility, not the scheduling policy, the same philosophy a future
 *     "archive-cleanup"-style command is expected to follow too.
 */
bool ws_backup_bootstrap_start(const WsRoute *route, pid_t *pidOut);

#endif /* WS_BACKUP_BOOTSTRAP_H */
