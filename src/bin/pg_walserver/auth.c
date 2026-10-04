/*
 * src/bin/pg_walserver/auth.c
 *   See auth.h.
 *
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
#include "string_utils.h"
#include "tls.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))

/* authentication request codes, see the protocol's AuthenticationSASL* */
#define AUTH_REQ_SASL 10
#define AUTH_REQ_SASL_CONTINUE 11
#define AUTH_REQ_SASL_FINAL 12


/*
 * ws_get_peer_ip writes the accepted connection's peer address, as a numeric
 * string (getnameinfo() with NI_NUMERICHOST, so no DNS lookup happens here),
 * into ipBuf. Returns false and logs on any getpeername()/getnameinfo()
 * failure.
 */
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
	size_t size = 0;

	if (passwdPath[0] == '\0' ||
		!read_file_capped(passwdPath, WS_MAX_CONFIG_FILE_SIZE, true,
						  &contents, &size, NULL))
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


/*
 * send_auth_request sends one 'R' Authentication* message: a 4-byte
 * big-endian request code followed by dataLen bytes of mechanism-specific
 * payload (empty for AuthenticationOk-style codes, a SCRAM message for the
 * SASL codes). Returns false when dataLen would not fit the local buffer.
 */
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
 * scram_authenticate runs the SCRAM-SHA-256 exchange over the connection:
 * SCRAM-SHA-256-PLUS (tls-server-end-point channel binding) is offered first
 * when the connection is encrypted, plain SCRAM-SHA-256 always. A user
 * without a usable verifier goes through the whole exchange with a mock one
 * and fails like a wrong password does, so that it cannot be told apart.
 */
static bool
scram_authenticate(int sock, const WsAuthConfig *authConfig, const char *user)
{
	ScramVerifier verifier;
	bool doomed = false;

	if (!find_verifier(authConfig->passwdPath, user, &verifier))
	{
		char safeUser[NAMEDATALEN + 8];

		sanitizeForLog(user, safeUser, sizeof(safeUser));
		log_warn("No SCRAM verifier for user \"%s\" in \"%s\"", safeUser,
				 authConfig->passwdPath);

		if (!scram_mock_verifier(user, &verifier))
		{
			ws_send_error_response(sock, "28P01",
								   "password authentication failed");
			return false;
		}

		doomed = true;
	}

	unsigned char cbindData[SCRAM_MAX_CBIND_LEN];
	int cbindDataLen = 0;

	if (ws_tls_active() &&
		!ws_tls_certificate_hash(cbindData, sizeof(cbindData), &cbindDataLen))
	{
		cbindDataLen = 0;
	}

	/* AuthenticationSASL: the NUL-separated mechanisms, then an empty one */
	char mechanisms[128];
	size_t mechanismsLen = 0;

	if (cbindDataLen > 0)
	{
		mechanismsLen += (size_t) sformat(mechanisms, sizeof(mechanisms), "%s",
										  SCRAM_MECHANISM_PLUS) + 1;
	}

	mechanismsLen += (size_t) sformat(mechanisms + mechanismsLen,
									  sizeof(mechanisms) - mechanismsLen, "%s",
									  SCRAM_MECHANISM) + 1;
	mechanisms[mechanismsLen++] = '\0';

	if (!send_auth_request(sock, AUTH_REQ_SASL, mechanisms, mechanismsLen))
	{
		return false;
	}

	char type;
	char *payload = NULL;
	int32_t payloadLen = 0;

	/* SASLInitialResponse: mechanism\0 int32 length, client-first-message */
	if (!ws_read_message(sock, &type, &payload, &payloadLen,
						 WS_MAX_AUTH_MESSAGE_LEN) || type != 'p')
	{
		free(payload);
		return false;
	}

	size_t mechLen = strnlen(payload, payloadLen);
	bool plus = streq(payload, SCRAM_MECHANISM_PLUS) && cbindDataLen > 0;

	if ((!plus && !streq(payload, SCRAM_MECHANISM)) ||
		(int32_t) mechLen + 1 + 4 > payloadLen)
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

	if (!scram_server_first(&state, &verifier,
							plus ? SCRAM_MECHANISM_PLUS : SCRAM_MECHANISM,
							clientFirst, cbindData, cbindDataLen,
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

	if (!ws_read_message(sock, &type, &payload, &payloadLen,
						 WS_MAX_AUTH_MESSAGE_LEN) || type != 'p' ||
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
							serverFinal, sizeof(serverFinal)) || doomed)
	{
		char safeUser[NAMEDATALEN + 8];

		sanitizeForLog(user, safeUser, sizeof(safeUser));
		log_warn("SCRAM authentication failed for user \"%s\"", safeUser);
		ws_send_error_response(sock, "28P01",
							   "password authentication failed");
		return false;
	}

	return send_auth_request(sock, AUTH_REQ_SASL_FINAL, serverFinal,
							 strlen(serverFinal));
}


/*
 * ws_client_cert_matches implements "clientcert=verify-full" (hba.h): the
 * connection must be TLS, must have presented a client certificate, and
 * that certificate's Subject CN must equal user exactly (no user name
 * mapping, see hba.h's own comment). Sends a clean ErrorResponse and
 * returns false on any failure -- never a silent fall-through to another
 * auth method.
 */
static bool
ws_client_cert_matches(int sock, const char *user)
{
	char safeUser[NAMEDATALEN + 8];

	sanitizeForLog(user, safeUser, sizeof(safeUser));

	if (!ws_tls_active())
	{
		log_warn("Rejecting connection as user \"%s\": clientcert=verify-full "
				 "requires a TLS connection", safeUser);
		ws_send_error_response(sock, "08000",
							   "a TLS connection with a client certificate "
							   "is required");
		return false;
	}

	char cn[NAMEDATALEN * 4];

	if (!ws_tls_get_peer_cert_cn(cn, sizeof(cn)))
	{
		log_warn("Rejecting connection as user \"%s\": no client certificate "
				 "was presented", safeUser);
		ws_send_error_response(sock, "08000",
							   "a client certificate is required");
		return false;
	}

	if (strcmp(cn, user) != 0)
	{
		char safeCn[NAMEDATALEN * 4 + 8];

		sanitizeForLog(cn, safeCn, sizeof(safeCn));
		log_warn("Rejecting connection as user \"%s\": client certificate "
				 "CN \"%s\" does not match", safeUser, safeCn);
		ws_send_error_response(sock, "08000",
							   "client certificate CN does not match "
							   "the requested user");
		return false;
	}

	return true;
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
		 * No HBA file configured: the explicit --insecure testing mode
		 * (main.c refuses to start without --pgdata otherwise), where any
		 * dbname is accepted without authentication.
		 */
		return true;
	}

	/*
	 * Authenticate BEFORE revealing anything, as PostgreSQL does: which
	 * routes exist is only told to a client that got through the HBA rules
	 * and the password exchange. An unknown route is reported (3D000,
	 * "database does not exist") only after a successful authentication.
	 *
	 * Three tiers, in order: an exact dbname match (what every command
	 * except a real physical standby's own walreceiver can set directly);
	 * failing that, over TLS, the client's own SNI hostname (a real
	 * standby's walreceiver always sends the literal dbname "replication",
	 * never a real route key -- see routes_find_by_hostname()'s own
	 * comment); failing that too, the "*" wildcard, if the file has one.
	 * HBA's own ROUTE matching, just below, stays independent of all of
	 * this and always sees the literal dbname the client sent -- see
	 * hba.h's own comment.
	 */
	const WsRoute *route = routes_find_exact(routes, routeCount, routeKey);

	if (route == NULL && ws_tls_active())
	{
		route = routes_find_by_hostname(routes, routeCount,
										ws_tls_get_sni_hostname());
	}

	if (route == NULL)
	{
		route = routes_find_exact(routes, routeCount, WS_ROUTES_WILDCARD_KEY);
	}

	char peerIP[NI_MAXHOST];

	if (!ws_get_peer_ip(sock, peerIP, sizeof(peerIP)))
	{
		ws_send_error_response(sock, "08000", "failed to identify peer address");
		return false;
	}

	char safeUser[NAMEDATALEN + 8];
	char safeRoute[NAMEDATALEN + 24];

	sanitizeForLog(params->user, safeUser, sizeof(safeUser));
	sanitizeForLog(routeKey, safeRoute, sizeof(safeRoute));

	WsAuthMethod method = WS_AUTH_REJECT;
	bool requireClientCert = false;

	hba_match(&authConfig->hbaRuleSet, routeKey, params->user, peerIP,
			  ws_tls_active(), &method, &requireClientCert);

	/*
	 * A "reject" line's own generic message (below) is deliberately never
	 * shadowed by a certificate-specific one, even if such a line also
	 * carries "clientcert=verify-full": that combination is pointless
	 * (nothing after a reject is ever reached), but if written, "reject" is
	 * what runs, not the certificate check.
	 */
	if (requireClientCert && method != WS_AUTH_REJECT &&
		!ws_client_cert_matches(sock, params->user))
	{
		/* ws_client_cert_matches() already logged and sent the
		 * ErrorResponse; never fall through to method below */
		return false;
	}

	switch (method)
	{
		case WS_AUTH_TRUST:
		{
			break;
		}

		case WS_AUTH_SCRAM:
		{
			if (!scram_authenticate(sock, authConfig, params->user))
			{
				return false;
			}

			break;
		}

		case WS_AUTH_REJECT:
		default:
		{
			log_warn("Rejecting connection from %s as user \"%s\" for route "
					 "\"%s\": no matching HBA entry", peerIP, safeUser,
					 safeRoute);

			/* one generic message: it names the peer and the user, both
			 * known to the client already, and never the route */
			char message[256];

			sformat(message, sizeof(message),
					"no pg_walserver HBA entry for host \"%s\", user \"%s\"",
					peerIP, safeUser);
			ws_send_error_response(sock, "28000", message);
			return false;
		}
	}

	if (route == NULL)
	{
		log_warn("Authenticated connection for unknown route \"%s\"",
				 safeRoute);

		char message[256];

		sformat(message, sizeof(message), "database \"%s\" does not exist",
				safeRoute);
		ws_send_error_response(sock, "3D000", message);
		return false;
	}

	if (route->disabled)
	{
		/*
		 * A dropped ("cluster drop" without --purge) route: refused the
		 * same way an unknown route is, after authentication, never
		 * before -- see this function's own header comment on why. The
		 * route genuinely still exists in the config file (its own "path"
		 * stays on record for a later "cluster drop --purge"/"cluster
		 * prune" to find it), so this is deliberately a distinct message
		 * from "database does not exist", not the same 3D000 case.
		 */
		log_warn("Authenticated connection for dropped (disabled) route "
				 "\"%s\"", safeRoute);

		char message[256];

		sformat(message, sizeof(message),
				"database \"%s\" has been dropped and is no longer served",
				safeRoute);
		ws_send_error_response(sock, "3D000", message);
		return false;
	}

	*foundRoute = route;
	return true;
}
