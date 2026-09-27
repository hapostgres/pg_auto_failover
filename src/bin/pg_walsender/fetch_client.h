/*
 * src/bin/pg_walsender/fetch_client.h
 *   `pg_walsender fetch-file ...` (see main.c): the FETCH_FILE client
 *   (cmd_fetch_file.h) for a restore_command. A plain libpq connection, so
 *   the password (PGPASSWORD, .pgpass), TLS (PGSSLMODE, PGSSLROOTCERT, ...)
 *   and every other connection option work as they do for pg_basebackup.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_FETCH_CLIENT_H
#define WS_FETCH_CLIENT_H

/*
 * Connects to host:port as user, requests filename for routeKey ("<formation>/
 * <group>", the dbname), and writes the result to outputPath (via a same-directory
 * temp file + rename, so a killed/interrupted fetch never leaves a
 * partial file at outputPath). Returns 0 on success, 1 on any failure
 * (connection, auth, missing file, short write) -- always with a
 * human-readable message already logged, matching restore_command's own
 * "non-zero means retry me" contract.
 */
int ws_fetch_file_client(const char *host, int port, const char *user,
						 const char *routeKey,
						 const char *filename, const char *outputPath);

#endif /* WS_FETCH_CLIENT_H */
