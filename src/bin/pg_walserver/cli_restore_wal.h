/*
 * src/bin/pg_walserver/cli_restore_wal.h
 *   `pg_walserver restore-wal <filename> <destination-path>`: meant to be
 *   run as (part of) a Postgres restore_command, e.g.:
 *
 *     restore_command = 'pg_walserver restore-wal %f %p --route mycluster \
 *                         --host archive.example.com --user archiver_repl'
 *
 *   Named "restore-wal", not the bare "restore", for the same reason the
 *   push side is "archive-wal", not "archive": `pg_walserver` already has
 *   a separate `basebackup` sub-command, so an unqualified "archive"/
 *   "restore" verb would leave it ambiguous whether it might also cover
 *   base backups -- exactly the ambiguity pgBackRest
 *   (`archive-push`/`archive-get` vs. `backup`/`restore`), Barman
 *   (`barman-wal-archive`/`barman-wal-restore` vs. `backup`/`restore`), and
 *   WAL-G (`wal-push`/`wal-fetch` vs. `backup-push`/`backup-fetch`) each
 *   deliberately avoid by giving their WAL-only archive_command/
 *   restore_command hooks a distinct, WAL-qualified name; PostgreSQL's own
 *   documentation literally titles this feature "Setting Up WAL
 *   Archiving," so "-wal" matches this project's own vocabulary, not an
 *   invented one.
 *
 *   Mirrors PostgreSQL's own restore_command substitution order -- "%f"
 *   (the bare filename recovery wants next) then "%p" (the local path it
 *   should be written to) -- the reverse of archive_command's own "%p %f",
 *   which is `cli_archive.c`'s own argument order (see that file's own
 *   header comment). Wraps src/bin/common/fetch_client.c's own
 *   ws_fetch_file_client(), finally giving that function a real caller:
 *   connects to pg_walserver, issues FETCH_FILE '<name>' as a plain simple
 *   query, and writes the result to the destination path (a same-directory
 *   temp file plus atomic rename, so a killed/interrupted restore never
 *   leaves a partial file where Postgres expects a complete one). Exits 0
 *   with the file written on success, nonzero with a clean stderr message
 *   otherwise -- including "not found", which PostgreSQL's own
 *   restore_command contract treats as the ordinary, expected way recovery
 *   discovers it has reached the end of the available WAL, not a hard
 *   error: this client does not try to distinguish that case from any
 *   other failure by parsing the server's error text, it simply reports
 *   failure and lets the caller (Postgres) decide what a nonzero exit
 *   means at this point in recovery, exactly as archive_command's own
 *   contract already works the other way around (see cli_archive.h).
 *
 *   Deliberately does NOT reuse cli_upstream.c's cli_resolve_upstream(),
 *   for the exact same reason cli_archive.c does not (see its own header
 *   comment): `restore-wal`, like `archive-wal`, connects to pg_walserver's
 *   own replication-protocol server, not out to a Postgres primary the way
 *   fetch-systemid/basebackup/setup do, so it has no "route's own upstream"
 *   to resolve at all -- only where pg_walserver itself is (--route/--host/
 *   --port/--user/--sslmode, mirroring cli_archive.h's own WsArchiveTarget
 *   flag names for consistency).
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_RESTORE_WAL_H
#define WS_CLI_RESTORE_WAL_H

#include <limits.h>
#include <stdbool.h>

#include "postgres_fe.h"

typedef struct WsRestoreTarget
{
	char host[_POSIX_HOST_NAME_MAX];
	int port;
	char user[NAMEDATALEN];
	char route[NAMEDATALEN + 16];   /* sent as the connection's dbname */
	char sslmode[32];                /* libpq sslmode, e.g. "prefer"/"require" */
} WsRestoreTarget;

/*
 * ws_restore_run fetches filename (the "%f" argument) from the pg_walserver
 * described by target and writes it to outputPath (the "%p" argument).
 * Returns true on success (the caller exits 0), false with an error
 * already logged to stderr otherwise (the caller exits nonzero).
 */
bool ws_restore_run(const WsRestoreTarget *target,
					const char *filename, const char *outputPath);

#endif /* WS_CLI_RESTORE_WAL_H */
