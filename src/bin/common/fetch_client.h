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
 *   pg_walserver and pg_autoctl already link src/bin/common/.
 *
 *   Its current, real caller is `pg_walserver restore` (cli_restore.c),
 *   used as a standalone deployment's own restore_command; a future,
 *   separate "archiving" PR is expected to grow `pg_autoctl restore
 *   command` to call this same function directly, in-process, once that
 *   PR's own monitor-backed archiver-node/quorum bookkeeping exists on top
 *   of it -- no execv() into a sub-command either way, which used to be how
 *   this was wired (a `pg_walserver fetch-file` sub-command) and is no
 *   longer: pg_walserver is a server binary (plus its scram-secret/setup/
 *   fetch-systemid/basebackup/create-cert/archive/restore client-side
 *   utilities), nothing else.
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
 * partial file at outputPath). sslmode is an ordinary libpq sslmode string
 * (e.g. "prefer"/"require"), or NULL/empty to leave it to libpq's own
 * default/PGSSLMODE. applicationName is sent as the connection's own
 * application_name (and fallback_application_name), the caller's choice --
 * this common client has no opinion of its own on what to call itself.
 * Returns 0 on success, 1 on any failure (connection, auth, missing file,
 * short write) -- always with a human-readable message already logged,
 * matching restore_command's own "non-zero means retry me" contract.
 */
int ws_fetch_file_client(const char *host, int port, const char *user,
						 const char *routeKey, const char *sslmode,
						 const char *applicationName,
						 const char *filename, const char *outputPath);

#endif /* WS_FETCH_CLIENT_H */
