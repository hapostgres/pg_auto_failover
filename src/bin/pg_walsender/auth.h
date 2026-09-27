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
 *   Authentication comes FIRST, as in PostgreSQL: the HBA rules are looked
 *   up with the route the client asked for (NULL when it is not a known
 *   route, so "monitor" cannot match), then the method runs, and only after
 *   a successful authentication is an unknown route reported (3D000,
 *   "database does not exist"). A rejection is one generic message naming
 *   the peer address and user, never the route. Client supplied strings are
 *   sanitized (control characters, length) before being logged.
 *
 *   The whole exchange runs under the connection's absolute authentication
 *   deadline (--auth-timeout, see accept_loop.h). Before authentication a
 *   client message is at most WS_MAX_AUTH_MESSAGE_LEN bytes.
 *
 *   Without any HBA file configured (no --pgdata: only with the explicit
 *   --insecure flag, for manual testing), everything is accepted.
 *
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
	char monitorUriPath[MAXPGPATH]; /* how "monitor" addresses reach the monitor:
	                                 * only the refresher (refresher.h) reads it,
	                                 * and children just test that it exists */
	char refreshSockPath[MAXPGPATH]; /* the refresher's datagram socket */
} WsAuthConfig;

/*
 * ws_authenticate authenticates the connection per the HBA file and then
 * resolves routeKey to a route. routeKey is passed explicitly because it is
 * not always the connection's dbname (a real walreceiver sends the literal
 * "replication", see accept_loop.c). On success returns
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
