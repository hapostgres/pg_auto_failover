/*
 * src/bin/pg_walserver/fetch_client.c
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
#include "pgsql.h"
#include "string_utils.h"


int
ws_fetch_file_client(const char *host, int port, const char *user,
					 const char *routeKey, const char *sslmode,
					 const char *applicationName,
					 const char *filename, const char *outputPath)
{
	/*
	 * Reuse this project's own PGSQL connection facility (pgsql_init() +
	 * pgsql_open_connection(), src/bin/common/pgsql.c) rather than a bare
	 * PQconnectdbParams() call: the same retry policy (exponential
	 * backoff with jitter, PQping()-based "wait for the server to become
	 * ready" loop), PGCONNECT_TIMEOUT handling, and notice-processor
	 * wiring every other connection in this codebase already gets, for
	 * free, instead of a one-shot connect attempt with none of that.
	 * FETCH_FILE (pg_walserver's own wire-protocol extension, cmd_fetch_
	 * file.c) has no ready-made wrapper in pgsql.c the way IDENTIFY_
	 * SYSTEM/TIMELINE_HISTORY do (pgsql_identify_system()), so this still
	 * drives its own COPY OUT protocol directly on the raw PGconn
	 * pgsql_open_connection() hands back -- exactly the same shape that
	 * function's own header comment describes for a caller in this
	 * situation.
	 */
	PQExpBuffer connInfo = createPQExpBuffer();

	appendPQExpBuffer(connInfo, "host=%s port=%d user=%s dbname=%s "
								"fallback_application_name=%s",
					  host, port, user, routeKey, applicationName);

	if (sslmode != NULL && sslmode[0] != '\0')
	{
		appendPQExpBuffer(connInfo, " sslmode=%s", sslmode);
	}

	if (PQExpBufferBroken(connInfo))
	{
		log_error("Out of memory");
		destroyPQExpBuffer(connInfo);
		return 1;
	}

	PGSQL pgsql = { 0 };

	if (!pgsql_init(&pgsql, connInfo->data, PGSQL_CONN_UPSTREAM))
	{
		/* errors have already been logged */
		destroyPQExpBuffer(connInfo);
		return 1;
	}

	destroyPQExpBuffer(connInfo);

	PGconn *conn = pgsql_open_connection(&pgsql);

	if (conn == NULL)
	{
		/* errors have already been logged (pgsql_open_connection() itself,
		 * or its own retry loop) */
		return 1;
	}

	/* the filename is validated by the server, quoted here all the same */
	PQExpBuffer command = createPQExpBuffer();
	char *quoted = PQescapeLiteral(conn, filename, strlen(filename));

	if (quoted == NULL)
	{
		log_error("Failed to quote \"%s\": %s", filename, PQerrorMessage(conn));
		destroyPQExpBuffer(command);
		pgsql_finish(&pgsql);
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
		pgsql_finish(&pgsql);
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

	pgsql_finish(&pgsql);

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
