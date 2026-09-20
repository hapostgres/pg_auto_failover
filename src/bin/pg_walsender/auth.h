/*
 * src/bin/pg_walsender/auth.h
 *   Connection authentication for pg_walsender. The route the client asked
 *   for must exist in the routes file; then the first matching rule of the
 *   HBA file (hba.h) decides the method:
 *
 *     trust          accept
 *     scram-sha-256  real SCRAM-SHA-256 exchange (RFC 5802, as spoken by
 *                    libpq, so pg_basebackup, pg_receivewal and a
 *                    standby's walreceiver work unmodified) against the
 *                    stored verifier for the user in the passwd file, one
 *                    "<user>:SCRAM-SHA-256$<iter>:<salt>$<stored>:<server>"
 *                    per line (create one with `pg_walsender scram-secret`)
 *     reject         refuse
 *
 *   Without any HBA file configured (no --pgdata: manual/standalone
 *   testing), everything is accepted as before.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WS_AUTH_H
#define WS_AUTH_H

#include <stdbool.h>

#include "postgres_fe.h"

#include "walsender.h"
#include "routes.h"

typedef struct WsAuthConfig
{
	char hbaPath[MAXPGPATH];       /* empty: no authentication at all */
	char passwdPath[MAXPGPATH];    /* scram-sha-256 verifiers */
} WsAuthConfig;

/*
 * ws_authenticate resolves routeKey to a route and authenticates the
 * connection per the HBA file. routeKey is passed explicitly because the
 * FETCH_FILE side-channel (see cmd_fetch_file.h) reuses this path with a
 * "fetch/" prefix stripped off the connection's dbname. On success returns
 * true and sets *foundRoute (NULL when routes were not supplied at all).
 * On failure an ErrorResponse has already been sent; the caller only needs
 * to close the connection. AuthenticationOk is NOT sent here: the caller
 * sends it, as before.
 */
bool ws_authenticate(int sock, const WsStartupParams *params,
					 const char *routeKey,
					 const WsRoute *routes, int routeCount,
					 const WsAuthConfig *authConfig,
					 const WsRoute **foundRoute);

#endif /* WS_AUTH_H */
