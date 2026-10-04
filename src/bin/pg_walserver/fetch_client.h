/*
 * src/bin/pg_walserver/fetch_client.h
 *   A one-shot client for pg_walserver's own FETCH_FILE extension (see
 *   cmd_fetch_file.h for the server side that answers it): a plain libpq
 *   connection issuing "FETCH_FILE '<name>'" as a simple query and saving
 *   its COPY OUT reply to a file. Every ordinary libpq connection option
 *   works exactly as it does for pg_basebackup (password via
 *   PGPASSWORD/.pgpass, TLS via PGSSLMODE/PGSSLROOTCERT/..., etc), because
 *   this is nothing more than a libpq client speaking one extra command --
 *   built on src/bin/common/pgsql.c's generic connect/retry facility, but
 *   this file itself stays here rather than in common/: it's pg_walserver's
 *   own FETCH_FILE command it's speaking, not a generic utility, matching
 *   pg_autoctl's own convention of keeping domain-specific protocol clients
 *   (monitor.c, etc.) in the binary that owns the protocol, not in common/.
 *
 *   Its current, real caller is `pg_walserver restore-wal`
 *   (cli_restore_wal.c), used as a standalone deployment's own
 *   restore_command.
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
