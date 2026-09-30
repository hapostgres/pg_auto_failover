/*
 * src/bin/pg_walserver/cli_fetch_systemid.h
 *   `pg_walserver fetch-systemid`: a one-shot client that connects to a
 *   route's upstream and writes its real Postgres system identifier into
 *   that route's own "pg_walserver_systemid" file -- the file cmd_identify_
 *   system.c's server side reads back on every IDENTIFY_SYSTEM. While it's
 *   connected, it also records the upstream's Postgres major version (as
 *   PQserverVersion()'s raw server_version_num, e.g. 170004) into a sibling
 *   "pg_walserver_pgversion" file -- cli_basebackup.c's own use of this file
 *   to pick a version-safe pg_basebackup binary.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_FETCH_SYSTEMID_H
#define WS_CLI_FETCH_SYSTEMID_H

#include <stdbool.h>
#include <stdint.h>

#include "commandline.h"

#include "cli_upstream.h"

/*
 * cli_fetch_systemid_run connects to target's upstream (a plain
 * replication-mode IDENTIFY_SYSTEM, via pgctl_identify_system()) and writes
 * its system identifier into "<target->path>/pg_walserver_systemid", atomically
 * (write_file_atomic()). If that file already exists with a *different*
 * identifier, refuses rather than overwriting -- the same "never silently
 * replace what's already there" principle as PostgreSQL's own archive_
 * command overwrite-safety rule, applied here to a route's own identity: an
 * operator pointing an existing route at the wrong instance, or a
 * once-restored cluster whose real identifier changed, must never have
 * this file silently swapped out from under it. Pass force = true to allow
 * it anyway (used by cli_setup.c after its own explicit confirmation that a
 * fresh route is being created).
 *
 * Returns true on success (including the "already correct, nothing to do"
 * case), false with an error already logged otherwise, including the
 * identifier-mismatch case above.
 */
bool cli_fetch_systemid_run(const WsUpstreamTarget *target, bool force,
							uint64_t *systemIdentifierOut);

extern CommandLine fetch_systemid_command;

#endif /* WS_CLI_FETCH_SYSTEMID_H */
