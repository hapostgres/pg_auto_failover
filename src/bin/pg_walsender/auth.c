/*
 * src/bin/pg_walsender/auth.c
 *   See auth.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "auth.h"
#include "defaults.h"
#include "framing.h"
#include "log.h"


static bool
ws_get_peer_ip(int sock, char *ipBuf, size_t ipBufSize)
{
	struct sockaddr_storage addr;
	socklen_t addrLen = sizeof(addr);

	if (getpeername(sock, (struct sockaddr *) &addr, &addrLen) != 0)
	{
		log_error("Failed to getpeername() on the accepted connection: %m");
		return false;
	}

	if (getnameinfo((struct sockaddr *) &addr, addrLen,
					ipBuf, ipBufSize, NULL, 0, NI_NUMERICHOST) != 0)
	{
		log_error("Failed to resolve the peer's numeric address: %m");
		return false;
	}

	return true;
}


bool
ws_authenticate(int sock, const WsStartupParams *params, const char *routeKey,
				const WsRoute *routes, int routeCount,
				const WsRoute **foundRoute)
{
	*foundRoute = NULL;

	if (strcmp(params->user, PG_AUTOCTL_REPLICA_USERNAME) != 0)
	{
		log_warn("Rejecting connection for unknown user \"%s\"", params->user);
		ws_send_error_response(sock, "28000",
							   "role is not permitted to connect to pg_walsender");
		return false;
	}

	if (routeCount == 0)
	{
		/*
		 * No routes file was supplied at all: manual/standalone testing
		 * mode, accept unconditionally now that the role matched. A real
		 * deployment always passes --routes (see main.c), so this branch
		 * never applies to a pg_autoctl-supervised pg_walsender.
		 */
		return true;
	}

	const WsRoute *route = routes_find(routes, routeCount, routeKey);

	if (route == NULL)
	{
		log_warn("Rejecting connection for unknown route \"%s\"", routeKey);
		ws_send_error_response(sock, "3D000",
							   "unknown formation/group requested as dbname");
		return false;
	}

	char peerIP[NI_MAXHOST];

	if (!ws_get_peer_ip(sock, peerIP, sizeof(peerIP)))
	{
		ws_send_error_response(sock, "08000", "failed to identify peer address");
		return false;
	}

	if (!routes_host_allowed(route, peerIP))
	{
		log_warn("Rejecting connection from %s: not in the allowed_hosts list "
				 "for route \"%s\"", peerIP, route->key);
		ws_send_error_response(sock, "28000",
							   "no pg_hba.conf-equivalent entry for this host");
		return false;
	}

	*foundRoute = route;

	return true;
}
