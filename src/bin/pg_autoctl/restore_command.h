/*
 * src/bin/pg_autoctl/restore_command.h
 *   restore_command: `pg_autoctl restore command`. A thin CLI shim around
 *   `pg_walsender fetch-file` (see fetch_client.h) that resolves the
 *   archiver's host/port/route/user on the caller's behalf, the same way
 *   archiver_confirm.h resolves the monitor connection on behalf of
 *   `pg_autoctl archive command` -- see that header's own comment.
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
 * `pg_walsender fetch-file` itself takes on its command line, minus the
 * password (left to libpq's normal PGPASSWORD/.pgpass resolution, exactly
 * as `pg_walsender fetch-file` already does today).
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
 * restore_command_run resolves the connection info and then execv()s
 * `pg_walsender fetch-file` (found next to the running pg_autoctl binary,
 * as service_archiver_serve.c already does for `archiver serve`) to fetch
 * sourceFile into destFile. Never returns on success (execv replaces this
 * process, and pg_walsender fetch-file's own exit code becomes ours,
 * already matching the restore_command contract -- see fetch_client.h);
 * returns a nonzero exit code only when it could not even get as far as
 * exec'ing pg_walsender.
 */
int restore_command_run(const char *pgdata, const RestoreCommandInfo *cliInfo,
						const char *sourceFile, const char *destFile);

#endif /* RESTORE_COMMAND_H */
