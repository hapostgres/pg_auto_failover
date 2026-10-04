/*
 * src/bin/common/push_client.h
 *   A one-shot client for pg_walserver's own CHECK_FILE/ARCHIVE_FILE
 *   extensions (see pg_walserver/cmd_check_file.h and cmd_archive_file.h
 *   for the server side that answers them): a plain libpq connection
 *   issuing "CHECK_FILE ..."/"ARCHIVE_FILE ..." as simple queries and
 *   streaming a local file's contents up via CopyIn. Every ordinary libpq
 *   connection option works exactly as it does for pg_basebackup (password
 *   via PGPASSWORD/.pgpass, TLS via PGSSLMODE/PGSSLROOTCERT/..., etc),
 *   because this is nothing more than a libpq client speaking two extra
 *   commands -- it has no dependency on pg_walserver's own wire-framing
 *   code (framing.h) or any other pg_walserver-internal header, which is
 *   why it lives here in common/ rather than in src/bin/pg_walserver/:
 *   both pg_walserver and pg_autoctl already link src/bin/common/.
 *
 *   Its current, real caller is `pg_walserver archive-wal` (cli_archive.c),
 *   used as a standalone deployment's own archive_command.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_PUSH_CLIENT_H
#define WS_PUSH_CLIENT_H

/*
 * Connects to host:port as user, pushes localPath up as filename for
 * routeKey ("<formation>/<group>", the dbname), and returns 0 on success
 * (including the "already there, byte-identical" case), 1 on any failure
 * (connection, auth, the embedded receivewal worker not having caught up
 * yet, a short write) -- always with a human-readable message already
 * logged, matching archive_command's own "non-zero means retry me"
 * contract. applicationName is sent as the connection's own
 * application_name (and fallback_application_name), the caller's choice
 * -- this common client has no opinion of its own on what to call itself.
 * sslmode is an ordinary libpq sslmode string (e.g. "prefer"/"require"),
 * or NULL/empty to leave it to libpq's own default/PGSSLMODE.
 */
int ws_push_file_client(const char *host, int port, const char *user,
						const char *routeKey, const char *sslmode,
						const char *applicationName,
						const char *localPath, const char *filename);

#endif /* WS_PUSH_CLIENT_H */
