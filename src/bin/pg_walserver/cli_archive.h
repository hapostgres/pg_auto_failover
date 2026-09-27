/*
 * src/bin/pg_walserver/cli_archive.h
 *   `pg_walserver archive-wal <path-to-file> <filename>`: meant to be run
 *   as (part of) a Postgres archive_command, e.g.:
 *
 *     archive_command = 'pg_walserver archive-wal %p %f --route mycluster \
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
 *   Implements the exact 4-step sequence README.md's "The archive push
 *   side: CHECK_FILE + ARCHIVE_FILE + pg_walserver archive-wal" section
 *   lays out: compute the local file's own size and CRC32C, CHECK_FILE, a
 *   short bounded recheck when it isn't already there, then ARCHIVE_FILE
 *   only if it's still needed after that. Exits 0 on success (including
 *   "already matches", the case that makes this safe to run *alongside*
 *   something else already feeding the same route -- an embedded or
 *   external pg_receivewal -- without ever duplicating a transfer),
 *   nonzero with a clean stderr message on any failure: exactly
 *   PostgreSQL's own archive_command contract (retry forever on nonzero;
 *   see this project's own precedent for "the caller -- Postgres -- is our
 *   retry loop, one attempt per invocation, no local retry-count state"
 *   wherever the pgaf-integration side's own archive_command driver
 *   documents it).
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
 *   like src/bin/common/fetch_client.c's own FETCH_FILE client, with no
 *   ReplicationSource/pgctl.c involved at all. WsArchiveTarget below
 *   mirrors cli_upstream.h's own flag *names* (--route/--host/--port/
 *   --user) for consistency, but is resolved directly in cli_archive.c
 *   rather than through cli_resolve_upstream(). `cli_restore_wal.c`'s own
 *   `restore-wal` follows the exact same reasoning/shape for the read side.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_ARCHIVE_H
#define WS_CLI_ARCHIVE_H

#include <limits.h>
#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsArchiveTarget
{
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char user[NAMEDATALEN];
	char route[NAMEDATALEN + 16];   /* sent as the connection's dbname */
	char sslmode[32];                /* libpq sslmode, e.g. "prefer"/"require" */
} WsArchiveTarget;

/*
 * ws_archive_run implements the 4-step archive_command sequence described
 * above against localPath (the "%p" argument: the file's real path, still
 * on the *source* instance's own filesystem) to be archived as filename
 * (the "%f" argument: the bare name it is archived under). Returns true on
 * success (the caller exits 0), false with an error already logged to
 * stderr otherwise (the caller exits nonzero, so PostgreSQL retries).
 */
bool ws_archive_run(const WsArchiveTarget *target,
					const char *localPath, const char *filename);

#endif /* WS_CLI_ARCHIVE_H */
