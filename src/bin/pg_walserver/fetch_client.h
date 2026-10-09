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

int ws_fetch_file_client(const char *host, int port, const char *user,
						 const char *clusterKey, const char *sslmode,
						 const char *applicationName,
						 const char *filename, const char *outputPath);

#endif /* WS_FETCH_CLIENT_H */
