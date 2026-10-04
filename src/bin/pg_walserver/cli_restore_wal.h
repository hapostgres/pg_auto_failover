/*
 * src/bin/pg_walserver/cli_restore_wal.h
 *   `pg_walserver restore-wal <filename> <destination-path>`: meant to be
 *   run as (part of) a Postgres restore_command, e.g.:
 *
 *     restore_command = 'pg_walserver restore-wal %f %p --cluster mycluster \
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
 *   header comment). Wraps fetch_client.c's own
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
 *   to resolve at all -- only where pg_walserver itself is, the shared
 *   WsWalServerTarget (cli_wal_target.h: --cluster/--host/--port/--user/
 *   --sslmode) also used by cli_archive.h's own `archive-wal`.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_RESTORE_WAL_H
#define WS_CLI_RESTORE_WAL_H

#include <stdbool.h>

#include "commandline.h"

#include "postgres_fe.h"

#include "cli_wal_target.h"

/*
 * ws_restore_fetch_file fetches filename (the "%f" argument) from the
 * pg_walserver described by target and writes it to outputPath (the "%p"
 * argument). Returns true on success (the caller exits 0), false with an
 * error already logged to stderr otherwise (the caller exits nonzero).
 */
bool ws_restore_fetch_file(const WsWalServerTarget *target,
						   const char *filename, const char *outputPath);

extern CommandLine restore_command;

#endif /* WS_CLI_RESTORE_WAL_H */
