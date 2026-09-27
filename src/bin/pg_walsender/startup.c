/*
 * src/bin/pg_walsender/startup.c
 *   See startup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <string.h>

#include "postgres_fe.h"

#include "startup.h"
#include "tls.h"
#include "framing.h"
#include "log.h"

#define SSL_REQUEST_CODE 80877103
#define GSS_REQUEST_CODE 80877104
#define CANCEL_REQUEST_CODE 80877102

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


bool
ws_startup_negotiate(int sock, WsStartupParams *params)
{
	memset(params, 0, sizeof(WsStartupParams));

	for (;;)
	{
		char *payload = NULL;
		int32_t payloadLen = 0;

		if (!ws_read_startup_payload(sock, &payload, &payloadLen))
		{
			free(payload);
			return false;
		}

		if (payloadLen < 4)
		{
			log_error("Received a malformed startup packet (%d bytes)", payloadLen);
			free(payload);
			return false;
		}

		int32_t code;

		memcpy(&code, payload, 4); /* IGNORE-BANNED */
		code = ntohl(code);

		if (code == SSL_REQUEST_CODE)
		{
			free(payload);

			/*
			 * Like PostgreSQL: answer 'S' and run the TLS handshake on this
			 * socket when the server has a certificate, 'N' otherwise (a
			 * client's sslmode=prefer then falls back to plaintext). A second
			 * SSLRequest on an already encrypted connection is a protocol
			 * violation.
			 */
			if (ws_tls_active())
			{
				return false;
			}

			if (!ws_tls_server_enabled())
			{
				if (!ws_write_raw_byte(sock, 'N'))
				{
					return false;
				}

				continue;
			}

			if (!ws_write_raw_byte(sock, 'S') || !ws_tls_server_accept(sock))
			{
				return false;
			}

			continue;
		}

		if (code == GSS_REQUEST_CODE)
		{
			free(payload);

			/* no GSSAPI encryption support: decline */
			if (!ws_write_raw_byte(sock, 'N'))
			{
				return false;
			}

			continue;
		}

		if (code == CANCEL_REQUEST_CODE)
		{
			log_debug("Ignoring a CancelRequest on a walsender connection");
			free(payload);
			return false;
		}

		if ((code >> 16) != 3)
		{
			log_error("Unsupported startup protocol version 0x%08x", code);
			(void) ws_send_error_response(sock, "0A000",
										  "unsupported frontend protocol");
			free(payload);
			return false;
		}

		/*
		 * Parse the NUL-separated key/value pairs following the version
		 * code first -- we need to know which "_pq_.*" options (if any) the
		 * client sent *before* we can answer NegotiateProtocolVersion below:
		 * real libpq's protocol-GREASE self-test sends
		 * "_pq_.test_protocol_negotiation" and requires the server to echo
		 * it back as unsupported (we don't parse any "_pq_.*" options, so
		 * every one seen here is unsupported by definition).
		 */
		const char *ptr = payload + 4;
		const char *end = payload + payloadLen;

		enum
		{
			WS_MAX_UNSUPPORTED_OPTIONS = 16
		};
		const char *unsupportedOptions[WS_MAX_UNSUPPORTED_OPTIONS];
		int nUnsupportedOptions = 0;

		while (ptr < end && *ptr != '\0')
		{
			const char *key = ptr;

			ptr += strlen(ptr) + 1;

			if (ptr >= end)
			{
				break;
			}

			const char *value = ptr;

			ptr += strlen(ptr) + 1;

			if (streq(key, "user"))
			{
				strlcpy(params->user, value, sizeof(params->user));
			}
			else if (streq(key, "database"))
			{
				strlcpy(params->database, value, sizeof(params->database));
			}
			else if (streq(key, "application_name"))
			{
				strlcpy(params->applicationName, value, sizeof(params->applicationName));
			}
			else if (streq(key, "replication"))
			{
				params->replicationDatabase = (strcasecmp(value, "database") == 0);
				params->replication = (streq(value, "1") ||
									   strcasecmp(value, "true") == 0 ||
									   params->replicationDatabase);
			}
			else if (strncmp(key, "_pq_.", 5) == 0 &&
					 nUnsupportedOptions < WS_MAX_UNSUPPORTED_OPTIONS)
			{
				unsupportedOptions[nUnsupportedOptions++] = key;
			}
		}

		/*
		 * Only protocol 3.0 is implemented. A client is free to ask for a
		 * newer minor version than we understand -- real libpq deliberately
		 * probes with a bogus one (protocol "GREASE", e.g. 3.9999) to
		 * verify a server properly negotiates rather than silently
		 * accepting whatever was asked for, and refuses to proceed against
		 * a server that gets this wrong. Tell it the newest minor version
		 * we actually speak (0) via NegotiateProtocolVersion, matching real
		 * Postgres's own backend behaviour, then continue the connection at
		 * that version rather than closing it.
		 */
		if ((code & 0xFFFF) != 0)
		{
			if (!ws_send_negotiate_protocol_version(sock, 0,
													unsupportedOptions,
													nUnsupportedOptions))
			{
				free(payload);
				return false;
			}
		}

		free(payload);

		/*
		 * A real replication connection always carries "database" too when
		 * replication=database is used (that's how pg_basebackup connects);
		 * a bare replication=1/true connection (pg_receivewal's style) may
		 * not set "database" at all. Default it to the "user" so downstream
		 * routing always has *something* to look up rather than an empty
		 * key -- callers that require a real "<formation>/<group>" key
		 * still get a clean "unknown route" ErrorResponse from auth.c.
		 */

		/*
		 * Like PostgreSQL's ProcessStartupPacket(): a startup packet
		 * without a user name is refused right here.
		 */
		if (params->user[0] == '\0')
		{
			log_error("Received a startup packet without a user name");
			(void) ws_send_error_response(sock, "28000",
										  "no PostgreSQL user name specified "
										  "in startup packet");
			return false;
		}

		if (params->database[0] == '\0')
		{
			strlcpy(params->database, params->user, sizeof(params->database));
		}

		return true;
	}
}
