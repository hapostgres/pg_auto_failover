/*
 * src/bin/common/fetch_client.c
 *   See fetch_client.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "fetch_client.h"

#include "file_utils.h"
#include "log.h"

#define FETCH_CONNECT_TIMEOUT_SECONDS "10"


int
ws_fetch_file_client(const char *host, int port, const char *user,
					 const char *routeKey, const char *sslmode,
					 const char *filename, const char *outputPath)
{
	char portStr[16];

	sformat(portStr, sizeof(portStr), "%d", port);

	const char *keys[] = {
		"host", "port", "user", "dbname", "sslmode", "connect_timeout",
		"fallback_application_name", NULL
	};
	const char *values[] = {
		host, portStr, user, routeKey,
		sslmode != NULL && sslmode[0] != '\0' ? sslmode : NULL,
		FETCH_CONNECT_TIMEOUT_SECONDS, "fetch_client (FETCH_FILE)", NULL
	};

	PGconn *conn = PQconnectdbParams(keys, values, 0);

	if (PQstatus(conn) != CONNECTION_OK)
	{
		log_error("Failed to connect to %s:%d: %s", host, port,
				  PQerrorMessage(conn));
		PQfinish(conn);
		return 1;
	}

	/* the filename is validated by the server, quoted here all the same */
	PQExpBuffer command = createPQExpBuffer();
	char *quoted = PQescapeLiteral(conn, filename, strlen(filename));

	if (quoted == NULL)
	{
		log_error("Failed to quote \"%s\": %s", filename, PQerrorMessage(conn));
		destroyPQExpBuffer(command);
		PQfinish(conn);
		return 1;
	}

	appendPQExpBuffer(command, "FETCH_FILE %s", quoted);
	PQfreemem(quoted);

	PGresult *res = PQexec(conn, command->data);

	destroyPQExpBuffer(command);

	if (PQresultStatus(res) != PGRES_COPY_OUT)
	{
		log_error("Failed to fetch \"%s\": %s", filename,
				  PQresultErrorMessage(res));
		PQclear(res);
		PQfinish(conn);
		return 1;
	}

	PQclear(res);

	PQExpBuffer contents = createPQExpBuffer();
	char *chunk = NULL;
	int n;

	while ((n = PQgetCopyData(conn, &chunk, 0)) > 0)
	{
		appendBinaryPQExpBuffer(contents, chunk, n);
		PQfreemem(chunk);
	}

	bool ok = n == -1 && !PQExpBufferBroken(contents);

	if (!ok)
	{
		log_error("Failed to receive \"%s\": %s", filename, PQerrorMessage(conn));
	}

	/* consume the command completion (or the error that ended the COPY) */
	while ((res = PQgetResult(conn)) != NULL)
	{
		if (PQresultStatus(res) != PGRES_COMMAND_OK)
		{
			log_error("Failed to fetch \"%s\": %s", filename,
					  PQresultErrorMessage(res));
			ok = false;
		}

		PQclear(res);
	}

	PQfinish(conn);

	if (!ok)
	{
		destroyPQExpBuffer(contents);
		return 1;
	}

	char tmpPath[MAXPGPATH];

	sformat(tmpPath, sizeof(tmpPath), "%s.pg_walserver_fetch_tmp", outputPath);

	if (!write_file(contents->data, contents->len, tmpPath))
	{
		log_error("Failed to write \"%s\": %m", tmpPath);
		destroyPQExpBuffer(contents);
		return 1;
	}

	long size = contents->len;

	destroyPQExpBuffer(contents);

	if (rename(tmpPath, outputPath) != 0)
	{
		log_error("Failed to rename \"%s\" to \"%s\": %m", tmpPath, outputPath);
		return 1;
	}

	log_info("Fetched \"%s\" (%ld bytes) to \"%s\"", filename, size, outputPath);

	return 0;
}
