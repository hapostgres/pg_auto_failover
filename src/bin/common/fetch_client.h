/*
 * src/bin/common/fetch_client.h
 *   A one-shot client for pg_walserver's own FETCH_FILE extension (see
 *   pg_walserver/cmd_fetch_file.h for the server side that answers it): a
 *   plain libpq connection issuing "FETCH_FILE '<name>'" as a simple query
 *   and saving its COPY OUT reply to a file. Every ordinary libpq connection
 *   option works exactly as it does for pg_basebackup (password via
 *   PGPASSWORD/.pgpass, TLS via PGSSLMODE/PGSSLROOTCERT/..., etc), because
 *   this is nothing more than a libpq client speaking one extra command --
 *   it has no dependency on pg_walserver's own wire-framing code
 *   (framing.h) or any other pg_walserver-internal header, which is why it
 *   lives here in common/ rather than in src/bin/pg_walserver/: both
 *   pg_walserver (as a mere historical location, not a dependency: it does
 *   not itself call this) and pg_autoctl already link src/bin/common/, and
 *   `pg_autoctl restore command` (a later "archiving" PR) calls
 *   ws_fetch_file_client() directly, in-process -- no execv() into a
 *   `pg_walserver fetch-file` sub-command, which used to be how this was
 *   wired and is no longer: pg_walserver is a server binary (plus its
 *   scram-secret utility sub-command), nothing else.
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
