/*
 * src/bin/pg_walsender/fetch_client.c
 *   See fetch_client.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "pqexpbuffer.h"

#include "fetch_client.h"
#include "defaults.h"
#include "file_utils.h"
#include "env_utils.h"
#include "framing.h"
#include "log.h"
#include "scram.h"
#include "tls.h"

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))


static int
connect_to(const char *host, int port)
{
	char portStr[16];

	sformat(portStr, sizeof(portStr), "%d", port);

	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *res = NULL;
	int rc = getaddrinfo(host, portStr, &hints, &res);

	if (rc != 0)
	{
		log_error("Failed to resolve \"%s\": %s", host, gai_strerror(rc));
		return -1;
	}

	int sock = -1;

	for (struct addrinfo *rp = res; rp != NULL; rp = rp->ai_next)
	{
		sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (sock < 0)
		{
			continue;
		}

		if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
		{
			break;
		}

		close(sock);
		sock = -1;
	}

	freeaddrinfo(res);

	if (sock < 0)
	{
		log_error("Failed to connect to %s:%d: %m", host, port);
	}

	return sock;
}


static bool
send_startup_message(int sock, const char *user, const char *database)
{
	PQExpBuffer buf = createPQExpBuffer();
	int32_t version = htonl(196608);   /* protocol 3.0 */

	appendBinaryPQExpBuffer(buf, (const char *) &version, 4);

	appendBinaryPQExpBuffer(buf, "user", strlen("user") + 1);
	appendBinaryPQExpBuffer(buf, user, strlen(user) + 1);

	appendBinaryPQExpBuffer(buf, "database", strlen("database") + 1);
	appendBinaryPQExpBuffer(buf, database, strlen(database) + 1);

	appendPQExpBufferChar(buf, '\0');   /* terminates the parameter list */

	int32_t totalLen = htonl(buf->len + 4);
	bool ok = !PQExpBufferBroken(buf) &&
			  ws_write_bytes(sock, &totalLen, 4) &&
			  ws_write_bytes(sock, buf->data, buf->len);

	destroyPQExpBuffer(buf);

	return ok;
}


static void
extract_error_message(const char *payload, int32_t payloadLen,
					  char *out, size_t outSize)
{
	out[0] = '\0';

	const char *p = payload;
	const char *end = payload + payloadLen;

	while (p < end && *p != '\0')
	{
		char code = *p++;
		const char *value = p;

		while (p < end && *p != '\0')
		{
			p++;
		}

		if (code == 'M')
		{
			size_t len = Min((size_t) (p - value), outSize - 1);

			memcpy(out, value, len); /* IGNORE-BANNED */
			out[len] = '\0';
		}

		if (p < end)
		{
			p++;   /* skip this field's NUL terminator */
		}
	}
}


/*
 * client_scram_authenticate answers an AuthenticationSASL request with a
 * SCRAM-SHA-256 exchange, the password coming from PGPASSWORD (as libpq
 * would), and consumes everything up to and including AuthenticationOk.
 */
static bool
client_scram_authenticate(int sock)
{
	char password[512] = { 0 };

	if (!get_env_copy("PGPASSWORD", password, sizeof(password)) ||
		password[0] == '\0')
	{
		log_error("The server requires a password: set PGPASSWORD");
		return false;
	}

	ScramClientState state;
	char clientFirst[SCRAM_MAX_MESSAGE_LEN];

	if (!scram_client_first(&state, clientFirst, sizeof(clientFirst)))
	{
		return false;
	}

	/* SASLInitialResponse: mechanism\0 int32 length message */
	PQExpBuffer buf = createPQExpBuffer();
	int32_t netLen = htonl((int32_t) strlen(clientFirst));

	appendBinaryPQExpBuffer(buf, "SCRAM-SHA-256", strlen("SCRAM-SHA-256") + 1);
	appendBinaryPQExpBuffer(buf, (const char *) &netLen, 4);
	appendBinaryPQExpBuffer(buf, clientFirst, strlen(clientFirst));

	bool ok = !PQExpBufferBroken(buf) &&
			  ws_send_message(sock, 'p', buf->data, buf->len);

	destroyPQExpBuffer(buf);

	if (!ok)
	{
		return false;
	}

	char type;
	char *payload = NULL;
	int32_t payloadLen = 0;

	if (!ws_read_message(sock, &type, &payload, &payloadLen))
	{
		free(payload);
		return false;
	}

	if (type == 'E')
	{
		char message[512];

		extract_error_message(payload, payloadLen, message, sizeof(message));
		log_error("Authentication failed: %s", message);
		free(payload);
		return false;
	}

	if (type != 'R' || payloadLen < 4 || payloadLen >= SCRAM_MAX_MESSAGE_LEN)
	{
		free(payload);
		return false;
	}

	char serverFirst[SCRAM_MAX_MESSAGE_LEN];

	memcpy(serverFirst, payload + 4, payloadLen - 4); /* IGNORE-BANNED */
	serverFirst[payloadLen - 4] = '\0';
	free(payload);

	char clientFinal[SCRAM_MAX_MESSAGE_LEN];

	if (!scram_client_final(&state, password, serverFirst,
							clientFinal, sizeof(clientFinal)) ||
		!ws_send_message(sock, 'p', clientFinal, (int32_t) strlen(clientFinal)))
	{
		return false;
	}

	payload = NULL;

	if (!ws_read_message(sock, &type, &payload, &payloadLen))
	{
		free(payload);
		return false;
	}

	if (type == 'E')
	{
		char message[512];

		extract_error_message(payload, payloadLen, message, sizeof(message));
		log_error("Authentication failed: %s", message);
		free(payload);
		return false;
	}

	if (type != 'R' || payloadLen < 4 || payloadLen >= SCRAM_MAX_MESSAGE_LEN)
	{
		free(payload);
		return false;
	}

	char serverFinal[SCRAM_MAX_MESSAGE_LEN];

	memcpy(serverFinal, payload + 4, payloadLen - 4); /* IGNORE-BANNED */
	serverFinal[payloadLen - 4] = '\0';
	free(payload);

	if (!scram_client_verify_server_final(&state, serverFinal))
	{
		log_error("The server's SCRAM signature did not verify");
		return false;
	}

	/* AuthenticationOk */
	payload = NULL;

	bool gotOk = ws_read_message(sock, &type, &payload, &payloadLen) &&
				 type == 'R';

	free(payload);

	return gotOk;
}


int
ws_fetch_file_client(const char *host, int port, const char *user,
					 const char *routeKey,
					 const char *filename, const char *outputPath)
{
	int sock = connect_to(host, port);

	if (sock < 0)
	{
		return 1;
	}

	/*
	 * TLS the way libpq does with PGSSLMODE (default "prefer"): ask with an
	 * SSLRequest, and go on encrypted when the server says 'S'.
	 */
	char sslmode[32] = "prefer";

	(void) get_env_copy("PGSSLMODE", sslmode, sizeof(sslmode));

	if (!streq(sslmode, "disable"))
	{
		int32_t requestLen = htonl(8);
		int32_t requestCode = htonl(80877103);
		char answer = 'N';

		if (!ws_write_bytes(sock, &requestLen, 4) ||
			!ws_write_bytes(sock, &requestCode, 4) ||
			!ws_read_bytes(sock, &answer, 1))
		{
			log_error("Failed to negotiate TLS with %s:%d", host, port);
			close(sock);
			return 1;
		}

		if (answer == 'S')
		{
			if (!ws_tls_client_connect(sock))
			{
				close(sock);
				return 1;
			}
		}
		else if (streq(sslmode, "require") || streq(sslmode, "verify-ca") ||
				 streq(sslmode, "verify-full"))
		{
			log_error("The server does not support TLS, PGSSLMODE=%s", sslmode);
			close(sock);
			return 1;
		}
	}

	char database[512];

	sformat(database, sizeof(database), "fetch/%s", routeKey);

	if (!send_startup_message(sock, user, database))
	{
		log_error("Failed to send the startup packet to %s:%d: %m", host, port);
		close(sock);
		return 1;
	}

	char type;
	char *payload = NULL;
	int32_t payloadLen = 0;

	if (!ws_read_message(sock, &type, &payload, &payloadLen))
	{
		log_error("Failed to read the authentication response from %s:%d",
				  host, port);
		free(payload);
		close(sock);
		return 1;
	}

	if (type == 'E')
	{
		char message[512];

		extract_error_message(payload, payloadLen, message, sizeof(message));
		log_error("Authentication failed: %s", message);
		free(payload);
		close(sock);
		return 1;
	}

	int32_t authCode = -1;

	if (type == 'R' && payloadLen >= 4)
	{
		memcpy(&authCode, payload, 4); /* IGNORE-BANNED */
		authCode = ntohl(authCode);
	}

	if (type == 'R' && authCode == 10)
	{
		free(payload);

		if (!client_scram_authenticate(sock))
		{
			close(sock);
			return 1;
		}
	}
	else
	{
		free(payload);

		if (type != 'R' || authCode != 0)
		{
			log_error("Unexpected authentication response '%c'/%d from "
					  "%s:%d (expected AuthenticationOk)",
					  type, authCode, host, port);
			close(sock);
			return 1;
		}
	}

	char line[300];

	sformat(line, sizeof(line), "%s\n", filename);

	if (!ws_write_bytes(sock, line, strlen(line)))
	{
		log_error("Failed to send the filename request to %s:%d: %m", host, port);
		close(sock);
		return 1;
	}

	if (!ws_read_message(sock, &type, &payload, &payloadLen))
	{
		log_error("Failed to read the file response from %s:%d", host, port);
		free(payload);
		close(sock);
		return 1;
	}

	if (type == 'E')
	{
		char message[512];

		extract_error_message(payload, payloadLen, message, sizeof(message));
		log_error("Failed to fetch \"%s\": %s", filename, message);
		free(payload);
		close(sock);
		return 1;
	}

	if (type != 'd')
	{
		log_error("Unexpected message type '%c' from %s:%d (expected CopyData)",
				  type, host, port);
		free(payload);
		close(sock);
		return 1;
	}

	close(sock);

	char tmpPath[MAXPGPATH];

	sformat(tmpPath, sizeof(tmpPath), "%s.pg_walsender_fetch_tmp", outputPath);

	if (!write_file(payload, payloadLen, tmpPath))
	{
		log_error("Failed to write \"%s\": %m", tmpPath);
		free(payload);
		return 1;
	}

	free(payload);

	if (rename(tmpPath, outputPath) != 0)
	{
		log_error("Failed to rename \"%s\" to \"%s\": %m", tmpPath, outputPath);
		return 1;
	}

	log_info("Fetched \"%s\" (%d bytes) to \"%s\"", filename, payloadLen, outputPath);

	return 0;
}
