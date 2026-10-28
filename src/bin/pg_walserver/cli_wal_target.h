/*
 * src/bin/pg_walserver/cli_wal_target.h
 *   Shared "which pg_walserver to connect to" target and flag-parsing for
 *   every client sub-command that connects *to* pg_walserver's own
 *   replication-protocol server, rather than *out* to an upstream Postgres
 *   primary the way fetch-systemid/basebackup/setup do (see
 *   cli_upstream.h's own header comment for that separate, genuinely
 *   different case -- deliberately not reused here).
 *
 *   `archive-wal`, `restore-wal`, and `archive-cleanup` each connect to
 *   pg_walserver itself over a plain libpq connection and only ever need
 *   --cluster/--host/--port/--user/--sslmode to do so; WsWalServerTarget
 *   and cli_wal_target_getopt() below are that one shared shape and one
 *   shared parser, previously duplicated byte-for-byte as WsArchiveTarget/
 *   WsRestoreTarget and cli_archive_getopt()/cli_restore_getopt() in
 *   cli_archive.h/cli_restore_wal.h. Each command still owns its own
 *   distinct positional-argument handling and its own dispatch/run
 *   function; only this flag-parsing plumbing is shared.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_WAL_TARGET_H
#define WS_CLI_WAL_TARGET_H

#include <limits.h>
#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsWalServerTarget
{
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char user[NAMEDATALEN];
	char cluster[NAMEDATALEN + 16];   /* sent as the connection's dbname */
	char sslmode[32];                /* libpq sslmode, e.g. "prefer"/"require" */
} WsWalServerTarget;

/*
 * cli_wal_target_getopt resets *target to its defaults (port WS_DEFAULT_PORT,
 * user PG_AUTOCTL_REPLICA_USERNAME) and parses --cluster/--host/--port/
 * --user/--sslmode out of argv, the same shape every pg_walserver-facing
 * client sub-command needs. Returns optind, the same convention every
 * CommandLine getopt callback in this binary follows (commandline.c calls
 * it, then runs the command's own positional-argument handling on
 * argv+optind). Prints ws_root's own usage and exits(1) on an unrecognized
 * flag, exactly as the callers this replaces did.
 */
int cli_wal_target_getopt(int argc, char **argv, WsWalServerTarget *target);

#endif /* WS_CLI_WAL_TARGET_H */
