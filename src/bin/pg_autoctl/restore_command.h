/*
 * src/bin/pg_autoctl/restore_command.h
 *   restore_command: `pg_autoctl restore command`. A thin CLI shim around
 *   ws_fetch_file_client() (see src/bin/common/fetch_client.h, called
 *   directly, in-process) that resolves the archiver's host/port/route/user
 *   on the caller's behalf, the same way archiver_confirm.h resolves the
 *   monitor connection on behalf of `pg_autoctl archive command` -- see
 *   that header's own comment.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef RESTORE_COMMAND_H
#define RESTORE_COMMAND_H

#include <stdbool.h>

#include "pgsql.h"

/*
 * Connection info needed to reach an archiver's pg_walsender: everything
 * ws_fetch_file_client() itself takes, minus the password (left to libpq's
 * normal PGPASSWORD/.pgpass resolution, exactly as any other libpq client
 * in this project already does).
 */
typedef struct RestoreCommandInfo
{
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char route[NAMEDATALEN * 2]; /* "<formation>/<group>" */
	char user[NAMEDATALEN];
} RestoreCommandInfo;

/*
 * restore_command_resolve fills in *info, in priority order:
 *
 *   1. any of host/port/route/user given non-empty on the command line
 *      (highest priority: an explicit override always wins),
 *   2. this node's own pg_autoctl configuration at pgdata, when it is a
 *      registered pg_auto_failover node with a reachable monitor (the
 *      preferred, zero-extra-setup path for a warm-standby-style replica
 *      created with `pg_autoctl create postgres`/`node run`),
 *   3. the on-disk cache written by `pg_autoctl restore command --set-up`
 *      (or by a previous successful run of step 2), for a plain ad hoc
 *      replica with no pg_auto_failover node of its own, or for any node
 *      whose monitor is currently unreachable.
 *
 * Returns true when *info is fully populated (host, route and user all
 * non-empty; port defaults to PG_AUTOCTL_ARCHIVER_SERVE_PORT when still
 * unset by any of the above), false when none of the three sources could
 * provide enough information.
 */
bool restore_command_resolve(const char *pgdata, RestoreCommandInfo *info);

/*
 * restore_command_set_up writes the cache file (step 3 above) at pgdata,
 * for `pg_autoctl restore command --set-up`.
 */
bool restore_command_set_up(const char *pgdata, const RestoreCommandInfo *info);

/*
 * restore_command_run resolves the connection info and then calls
 * ws_fetch_file_client() directly, in-process, to fetch sourceFile into
 * destFile. Returns 0 on success, a nonzero exit code otherwise (connection
 * failure, missing file, short write, or a connection info that could not
 * be resolved at all) -- already matching the restore_command contract
 * ("non-zero means retry me"), see fetch_client.h.
 */
int restore_command_run(const char *pgdata, const RestoreCommandInfo *cliInfo,
						const char *sourceFile, const char *destFile);

#endif /* RESTORE_COMMAND_H */
