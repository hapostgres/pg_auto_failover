/*
 * src/bin/pg_walserver/cli_upstream.h
 *   Shared resolution logic for the client-side sub-commands
 *   (fetch-systemid, basebackup, setup): "which route, and what upstream
 *   connection to reach it from", an explicit-flag-always-wins-over-config
 *   layering this project's own precedent (`pg_autoctl`'s config-file-vs-
 *   command-line-flag resolution elsewhere in this codebase); this
 *   project's own `pg_walserver archive`/`pg_walserver restore` client
 *   sub-commands (cli_archive.c/cli_restore.c) use the same layering for
 *   their own, simpler --cluster/--host/--port/--user set, without needing
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

bool cli_resolve_upstream(const char *pgdata, const char *configFile,
						  const char *routeKey,
						  const char *pathArg, const char *upstreamArg,
						  const char *hostArg, const char *portArg,
						  const char *userArg,
						  WsUpstreamTarget *target);

/*
 * cli_parse_upstream_conninfo parses a plain libpq keyword/value connection
 * string (routes.h's own "upstream" property shape) directly, filling in
 * target's host/port/user/sslOptions -- the same parsing cli_resolve_
 * upstream() uses internally for an explicit --upstream/a route's own
 * "upstream" property, exposed here for receivewal.c's own need to turn a
 * route's raw "upstream" string into connection fields (to create this
 * route's own replication slot) without going through the rest of cli_
 * resolve_upstream()'s --path/--cluster/--pgdata resolution, which doesn't
 * apply there.
 */
bool cli_parse_upstream_conninfo(const char *conninfo, WsUpstreamTarget *target);

#endif /* WS_CLI_UPSTREAM_H */
