/*
 * src/bin/pg_walserver/cli_upstream.h
 *   Shared resolution logic for the client-side sub-commands
 *   (fetch-systemid, basebackup, setup): "which route, and what upstream
 *   connection to reach it from", an explicit-flag-always-wins-over-config
 *   layering this project's own precedent (`pg_autoctl`'s config-file-vs-
 *   command-line-flag resolution elsewhere in this codebase); this
 *   project's own `pg_walserver archive`/`pg_walserver restore` client
 *   sub-commands (cli_archive.c/cli_restore.c) use the same layering for
 *   their own, simpler --route/--host/--port/--user set, without needing
 *   this file at all (they connect to pg_walserver itself, not to an
 *   upstream Postgres primary, see cli_archive.c's own header comment). A
 *   later, separate "archiving" PR is expected to add an analogous
 *   `pg_autoctl restore command`/`pg_autoctl archive command`, with full
 *   monitor-backed quorum/archiver-node participation -- not part of this
 *   file or this PR.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_CLI_UPSTREAM_H
#define WS_CLI_UPSTREAM_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "pgsql.h"

/*
 * Everything a client sub-command needs to reach the route's upstream and
 * write into the route's own directory.
 */
typedef struct WsUpstreamTarget
{
	char path[MAXPGPATH];               /* the route's own local directory */
	NodeAddress node;                   /* host + port */
	char userName[NAMEDATALEN];
	SSLOptions sslOptions;
} WsUpstreamTarget;

/*
 * cli_resolve_upstream fills *target for --route/--pgdata (looked up in
 * <pgdata>/pg_walserver.ini) and/or --path/--upstream/--host/--port/--user
 * given directly on the command line -- an explicit flag always wins over
 * whatever the route's own "upstream"/"path" ini properties say. Returns
 * false (with an error already logged) when neither source leaves *target
 * fully resolved (a path and a host are both required; user defaults to
 * "pgautofailover_replicator", port to 5432 when the upstream conninfo
 * didn't say).
 */
bool cli_resolve_upstream(const char *pgdata, const char *routeKey,
						  const char *pathArg, const char *upstreamArg,
						  const char *hostArg, const char *portArg,
						  const char *userArg,
						  WsUpstreamTarget *target);

#endif /* WS_CLI_UPSTREAM_H */
