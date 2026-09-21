/*
 * src/bin/pg_walsender/auth.c
 *   See auth.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "postgres_fe.h"

#include "auth.h"
#include "defaults.h"
#include "file_utils.h"
#include "framing.h"
#include "hba.h"
#include "log.h"
#include "scram.h"
#include "tls.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/* authentication request codes, see the protocol's AuthenticationSASL* */
#define AUTH_REQ_SASL 10
#define AUTH_REQ_SASL_CONTINUE 11
#define AUTH_REQ_SASL_FINAL 12

#define SCRAM_MECHANISM "SCRAM-SHA-256"


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


/*
 * find_verifier looks the user up in the passwd file: one
 * "<user>:<SCRAM-SHA-256 secret>" per line.
 */
static bool
find_verifier(const char *passwdPath, const char *user, ScramVerifier *verifier)
{
	char *contents = NULL;
	long size = 0;

	if (passwdPath[0] == '\0' ||
		!read_file_if_exists(passwdPath, &contents, &size) || contents == NULL)
	{
		return false;
	}

	bool found = false;
	char *lineSave = NULL;

	for (char *line = strtok_r(contents, "\n", &lineSave);
		 line != NULL && !found;
		 line = strtok_r(NULL, "\n", &lineSave))
	{
		char *colon = strchr(line, ':');

		if (colon == NULL)
		{
			continue;
		}

		*colon = '\0';

		if (streq(line, user))
		{
			found = scram_parse_verifier(colon + 1, verifier);
		}
	}

	free(contents);

	return found;
}


static bool
send_auth_request(int sock, int32_t code, const char *data, size_t dataLen)
{
	char buf[SCRAM_MAX_MESSAGE_LEN + 8];

	if (dataLen + 4 > sizeof(buf))
	{
		return false;
	}

	int32_t netCode = htonl(code);

	memcpy(buf, &netCode, 4); /* IGNORE-BANNED */

	if (dataLen > 0)
	{
		memcpy(buf + 4, data, dataLen); /* IGNORE-BANNED */
	}

	return ws_send_message(sock, 'R', buf, (int32_t) (4 + dataLen));
}


/*
 * scram_authenticate runs the SCRAM-SHA-256 exchange over the connection.
 */
static bool
scram_authenticate(int sock, const WsAuthConfig *authConfig, const char *user)
{
	ScramVerifier verifier;

	if (!find_verifier(authConfig->passwdPath, user, &verifier))
	{
		log_warn("No SCRAM verifier for user \"%s\" in \"%s\"", user,
				 authConfig->passwdPath);
		ws_send_error_response(sock, "28P01",
							   "password authentication failed");
		return false;
	}

	/* AuthenticationSASL: the list of mechanisms, NUL-terminated list */
	char mechanisms[] = SCRAM_MECHANISM "\0";

	if (!send_auth_request(sock, AUTH_REQ_SASL, mechanisms, sizeof(mechanisms)))
	{
		return false;
	}

	char type;
	char *payload = NULL;
	int32_t payloadLen = 0;

	/* SASLInitialResponse: mechanism\0 int32 length, client-first-message */
	if (!ws_read_message(sock, &type, &payload, &payloadLen) || type != 'p')
	{
		free(payload);
		return false;
	}

	size_t mechLen = strnlen(payload, payloadLen);

	if (!streq(payload, SCRAM_MECHANISM) || (int32_t) mechLen + 1 + 4 > payloadLen)
	{
		free(payload);
		ws_send_error_response(sock, "28000", "unsupported SASL mechanism");
		return false;
	}

	int32_t initialLen = 0;

	memcpy(&initialLen, payload + mechLen + 1, 4); /* IGNORE-BANNED */
	initialLen = ntohl(initialLen);

	const char *clientFirstData = payload + mechLen + 1 + 4;
	int32_t available = payloadLen - (int32_t) mechLen - 1 - 4;

	if (initialLen < 0 || initialLen > available ||
		initialLen >= SCRAM_MAX_MESSAGE_LEN)
	{
		free(payload);
		ws_send_error_response(sock, "08P01", "malformed SASLInitialResponse");
		return false;
	}

	char clientFirst[SCRAM_MAX_MESSAGE_LEN];

	memcpy(clientFirst, clientFirstData, initialLen); /* IGNORE-BANNED */
	clientFirst[initialLen] = '\0';
	free(payload);

	ScramServerState state;
	char serverFirst[SCRAM_MAX_MESSAGE_LEN];

	if (!scram_server_first(&state, &verifier, clientFirst,
							serverFirst, sizeof(serverFirst)))
	{
		ws_send_error_response(sock, "08P01", "malformed SCRAM message");
		return false;
	}

	if (!send_auth_request(sock, AUTH_REQ_SASL_CONTINUE, serverFirst,
						   strlen(serverFirst)))
	{
		return false;
	}

	/* SASLResponse: the client-final-message, raw */
	payload = NULL;

	if (!ws_read_message(sock, &type, &payload, &payloadLen) || type != 'p' ||
		payloadLen <= 0 || payloadLen >= SCRAM_MAX_MESSAGE_LEN)
	{
		free(payload);
		return false;
	}

	char clientFinal[SCRAM_MAX_MESSAGE_LEN];

	memcpy(clientFinal, payload, payloadLen); /* IGNORE-BANNED */
	clientFinal[payloadLen] = '\0';
	free(payload);

	char serverFinal[SCRAM_MAX_MESSAGE_LEN];

	if (!scram_server_final(&state, &verifier, clientFinal,
							serverFinal, sizeof(serverFinal)))
	{
		log_warn("SCRAM authentication failed for user \"%s\"", user);
		ws_send_error_response(sock, "28P01",
							   "password authentication failed");
		return false;
	}

	return send_auth_request(sock, AUTH_REQ_SASL_FINAL, serverFinal,
							 strlen(serverFinal));
}


bool
ws_authenticate(int sock, const WsStartupParams *params, const char *routeKey,
				const WsRoute *routes, int routeCount,
				const WsAuthConfig *authConfig, const WsRoute **foundRoute)
{
	*foundRoute = NULL;

	if (authConfig->hbaPath[0] == '\0')
	{
		/*
		 * No HBA file configured: manual/standalone testing mode. A real deployment always passes --pgdata (see
		 * main.c), so this never applies to a pg_autoctl-supervised
		 * pg_walsender.
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

	WsAuthMethod method = WS_AUTH_REJECT;

	if (!hba_lookup(authConfig->hbaPath, route->path, authConfig->monitorUriPath,
					route->key, params->user, peerIP, ws_tls_active(), &method))
	{
		ws_send_error_response(sock, "28000", "authentication is unavailable");
		return false;
	}

	switch (method)
	{
		case WS_AUTH_TRUST:
		{
			*foundRoute = route;
			return true;
		}

		case WS_AUTH_SCRAM:
		{
			if (!scram_authenticate(sock, authConfig, params->user))
			{
				return false;
			}

			*foundRoute = route;
			return true;
		}

		case WS_AUTH_REJECT:
		default:
		{
			log_warn("Rejecting connection from %s as user \"%s\" for route "
					 "\"%s\": no matching HBA entry", peerIP, params->user,
					 route->key);

			char message[256];

			sformat(message, sizeof(message),
					"no pg_walsender HBA entry for host \"%s\", user \"%s\", "
					"route \"%s\"", peerIP, params->user, route->key);
			ws_send_error_response(sock, "28000", message);
			return false;
		}
	}
}
