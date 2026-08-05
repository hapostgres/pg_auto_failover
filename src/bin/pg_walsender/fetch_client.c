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
#include "framing.h"
#include "log.h"


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
send_startup_message(int sock, const char *database)
{
	PQExpBuffer buf = createPQExpBuffer();
	int32_t version = htonl(196608);   /* protocol 3.0 */

	appendBinaryPQExpBuffer(buf, (const char *) &version, 4);

	appendBinaryPQExpBuffer(buf, "user", strlen("user") + 1);
	appendBinaryPQExpBuffer(buf, PG_AUTOCTL_REPLICA_USERNAME,
							strlen(PG_AUTOCTL_REPLICA_USERNAME) + 1);

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


int
ws_fetch_file_client(const char *host, int port, const char *routeKey,
					 const char *filename, const char *outputPath)
{
	int sock = connect_to(host, port);

	if (sock < 0)
	{
		return 1;
	}

	char database[512];

	sformat(database, sizeof(database), "fetch/%s", routeKey);

	if (!send_startup_message(sock, database))
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

	free(payload);

	if (type != 'R')
	{
		log_error("Unexpected message type '%c' from %s:%d (expected "
				  "AuthenticationOk)", type, host, port);
		close(sock);
		return 1;
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
