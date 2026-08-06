/*
 * src/bin/pg_walsender/auth.h
 *   Trust-equivalent authentication, matching this project's existing
 *   convention: no password/SCRAM infrastructure exists anywhere in
 *   pg_auto_failover today (pghba.c installs plain "trust" entries for the
 *   replicator role, defaults.h's REPLICATION_PASSWORD_DEFAULT is NULL).
 *   pg_walsender mirrors that: accept iff the startup packet's user is the
 *   replicator role and, when the resolved route carries an allowed_hosts
 *   list, the peer address matches -- routes.c's allowed_hosts is
 *   effectively pg_walsender's own pg_hba.conf, since it has no PGDATA of
 *   its own to carry a real one.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_AUTH_H
#define WS_AUTH_H

#include <stdbool.h>

#include "walsender.h"
#include "routes.h"

/*
 * ws_authenticate checks params against the replicator username and, if
 * routes/routeCount is non-empty, against the route matching routeKey and
 * its allowed_hosts. routeKey is passed explicitly rather than read from
 * params->database because the FETCH_FILE side-channel (see
 * cmd_fetch_file.h) reuses this same auth path with a "fetch/" prefix
 * stripped off the connection's actual dbname -- the caller (accept_loop.c)
 * decides what routeKey means, this function only ever looks it up. On
 * success returns true and sets *foundRoute (NULL when routes were not
 * supplied at all -- a manual-testing convenience, see main.c's --routes
 * option). On failure, an ErrorResponse has already been sent to sock; the
 * caller only needs to close the connection.
 */
bool ws_authenticate(int sock, const WsStartupParams *params,
					 const char *routeKey,
					 const WsRoute *routes, int routeCount,
					 const WsRoute **foundRoute);

#endif /* WS_AUTH_H */
