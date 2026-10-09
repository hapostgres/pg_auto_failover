/*
 * src/bin/pg_walserver/cli_archive.h
 *   `pg_walserver archive-wal <path-to-file> <filename>`: meant to be run
 *   as (part of) a Postgres archive_command, e.g.:
 *
 *     archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster \
 *                         --host archive.example.com --user archiver_repl'
 *
 *   Named "archive-wal", not the bare "archive", because `pg_walserver`
 *   already has a separate `basebackup` sub-command: an unqualified
 *   "archive"/"restore" verb would leave it ambiguous whether it might also
 *   cover base backups -- exactly the ambiguity pgBackRest
 *   (`archive-push`/`archive-get` vs. `backup`/`restore`), Barman
 *   (`barman-wal-archive`/`barman-wal-restore` vs. `backup`/`restore`), and
 *   WAL-G (`wal-push`/`wal-fetch` vs. `backup-push`/`backup-fetch`) each
 *   deliberately avoid by giving their WAL-only archive_command/
 *   restore_command hooks a distinct, WAL-qualified name; PostgreSQL's own
 *   documentation literally titles this feature "Setting Up WAL
 *   Archiving," so "-wal" matches this project's own vocabulary, not an
 *   invented one. See cli_restore_wal.h for the read-side counterpart.
 *
 *   Implements the two disjoint behaviors README.md's "The archive push
 *   side: CHECK_FILE + ARCHIVE_FILE + pg_walserver archive-wal" section
 *   describes, chosen automatically per invocation from the connected
 *   cluster's own "receivewal" setting (SHOW receivewal, cmd_show.c), never a
 *   manually-set client flag:
 *
 *     - the cluster has "receivewal = pull" configured (its own embedded,
 *       supervised pg_receivewal writes into this same directory):
 *       ordinarily only ever CHECK_FILE, never ARCHIVE_FILE. Exit 0 on
 *       "matches", exit 1 on "missing"/"differs" with a clean stderr
 *       message -- no sleep, no retry loop inside this client,
 *       PostgreSQL's own archive_command retry loop is the entire retry
 *       mechanism. The one exception: CHECK_FILE's own "fallback" column
 *       (cmd_check_file.h) tells this client when the embedded receivewal
 *       worker has already streamed *past* filename without it ever
 *       showing up -- a hole a streaming worker can never retroactively
 *       fill, typically a timeline switch that left a segment behind on
 *       the old timeline -- in which case this pushes it directly via
 *       ARCHIVE_FILE right away instead of waiting on a retry loop that
 *       would otherwise never succeed;
 *     - the cluster has no "receivewal = pull" (absent or "none"): only ever
 *       ARCHIVE_FILE, unconditionally pushing the full file every
 *       invocation, no CHECK_FILE round trip first -- the server's own
 *       overwrite-safety (cmd_archive_file.c) already makes this
 *       idempotent on a retry.
 *
 *   Exit 0 on success, nonzero with a clean stderr message on any failure:
 *   exactly PostgreSQL's own archive_command contract (retry forever on
 *   nonzero; see this project's own precedent for "the caller -- Postgres
 *   -- is our retry loop, one attempt per invocation, no local retry-count
 *   state" wherever the pgaf-integration side's own archive_command driver
 *   documents it, e.g. archiver_confirm.c).
 *
 *   Deliberately does NOT reuse cli_upstream.c's cli_resolve_upstream() as
 *   it stands: that helper resolves a WsUpstreamTarget (a NodeAddress +
 *   SSLOptions shaped for src/bin/common/pgctl.c's prepare_primary_
 *   conninfo(), i.e. a real *Postgres* primary connection) the way fetch-
 *   systemid/basebackup connect *out* from pg_walserver to an upstream
 *   Postgres instance. Here the direction is reversed and the target is a
 *   different kind of server entirely: `archive-wal` runs as an
 *   archive_command *on* the Postgres primary itself, connecting *to*
 *   pg_walserver's own replication-protocol server -- a plain libpq
 *   connection issuing CHECK_FILE/ARCHIVE_FILE as simple queries, exactly
 *   like fetch_client.c's own FETCH_FILE client, with no
 *   ReplicationSource/pgctl.c involved at all. The shared WsWalServerTarget
 *   (cli_wal_target.h) mirrors cli_upstream.h's own flag *names*
 *   (--cluster/--host/--port/--user) for consistency, but is resolved
 *   directly by cli_wal_target_getopt() rather than through
 *   cli_resolve_upstream(). `cli_restore_wal.c`'s own `restore-wal` follows
 *   the exact same reasoning/shape for the read side.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_ARCHIVE_H
#define WS_CLI_ARCHIVE_H

#include <stdbool.h>

#include "commandline.h"

#include "postgres_fe.h"

#include "cli_wal_target.h"

/*
 * ws_archive_push_file implements the 4-step archive_command sequence
 * described above against localPath (the "%p" argument: the file's real
 * path, still on the *source* instance's own filesystem) to be archived as
 * filename (the "%f" argument: the bare name it is archived under).
 * Returns true on success (the caller exits 0), false with an error
 * already logged to stderr otherwise (the caller exits nonzero, so
 * PostgreSQL retries).
 */
bool ws_archive_push_file(const WsWalServerTarget *target,
						  const char *localPath, const char *filename);

extern CommandLine archive_command;

#endif /* WS_CLI_ARCHIVE_H */
