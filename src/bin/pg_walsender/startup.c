/*
 * src/bin/pg_walsender/startup.c
 *   See startup.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <string.h>

#include "postgres_fe.h"

#include "startup.h"
#include "framing.h"
#include "log.h"

#define SSL_REQUEST_CODE 80877103
#define GSS_REQUEST_CODE 80877104
#define CANCEL_REQUEST_CODE 80877102


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

		if (code == SSL_REQUEST_CODE || code == GSS_REQUEST_CODE)
		{
			free(payload);

			/*
			 * MVP: no SSL/GSS support yet (see the design doc's Auth
			 * section) -- decline, real libpq's default sslmode=prefer
			 * falls back to plaintext automatically on 'N'.
			 */
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

			if (strcmp(key, "user") == 0)
			{
				strlcpy(params->user, value, sizeof(params->user));
			}
			else if (strcmp(key, "database") == 0)
			{
				strlcpy(params->database, value, sizeof(params->database));
			}
			else if (strcmp(key, "application_name") == 0)
			{
				strlcpy(params->applicationName, value, sizeof(params->applicationName));
			}
			else if (strcmp(key, "replication") == 0)
			{
				params->replicationDatabase = (strcasecmp(value, "database") == 0);
				params->replication = (strcmp(value, "1") == 0 ||
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
		if (params->database[0] == '\0')
		{
			strlcpy(params->database, params->user, sizeof(params->database));
		}

		return true;
	}
}
