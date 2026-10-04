/*
 * src/bin/pg_walserver/push_client.c
 *   See push_client.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "push_client.h"

#include "file_utils.h"
#include "log.h"
#include "pgsql.h"
#include "string_utils.h"
#include "ws_util.h"

/* CopyData chunks of this size when pushing, matching fetch_client.c's
 * own read-side chunk size */
#define WS_PUSH_CHUNK_SIZE (128 * 1024)

/* local helpers */
static bool check_file_status(PGconn *conn, const char *filename,
							  uint64_t size, uint32_t crc, char *statusOut,
							  size_t statusOutSize, bool *fallbackOut);
static bool show_receivewal(PGconn *conn, bool *pullOut);
static bool push_file(PGconn *conn, const char *localPath,
					  const char *filename);


/*
 * check_file_status runs one CHECK_FILE round trip for filename/size/crc32c
 * against conn, filling statusOut (at least 16 bytes) with the server's
 * reply ("missing"/"matches"/"differs") and fallbackOut with whether the
 * server recommends pushing via ARCHIVE_FILE right away instead of waiting
 * -- see pg_walserver's cmd_check_file.h for exactly what that means and
 * when the server says "yes". Returns false (both out params untouched)
 * on any connection/protocol failure -- always with an error already
 * logged.
 */
static bool
check_file_status(PGconn *conn, const char *filename, uint64_t size,
				  uint32_t crc, char *statusOut, size_t statusOutSize,
				  bool *fallbackOut)
{
	char *quoted = PQescapeLiteral(conn, filename, strlen(filename));

	if (quoted == NULL)
	{
		log_error("Failed to quote \"%s\": %s", filename, PQerrorMessage(conn));
		return false;
	}

	PQExpBuffer command = createPQExpBuffer();

	appendPQExpBuffer(command, "CHECK_FILE %s %" PRIu64 " crc32c:%08X",
					  quoted, size, crc);
	PQfreemem(quoted);

	if (PQExpBufferBroken(command))
	{
		log_error("Out of memory building the CHECK_FILE command");
		destroyPQExpBuffer(command);
		return false;
	}

	PGresult *res = PQexec(conn, command->data);

	destroyPQExpBuffer(command);

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
	{
		log_error("CHECK_FILE \"%s\" failed: %s", filename,
				  PQresultErrorMessage(res));
		PQclear(res);
		return false;
	}

	strlcpy(statusOut, PQgetvalue(res, 0, 0), statusOutSize);

	/*
	 * PQnfields() guards against an older server that only ever sent the
	 * one "status" column: never treat that as a fallback recommendation,
	 * just the pre-existing behavior.
	 */
	*fallbackOut = PQnfields(res) > 1 &&
				   strcmp(PQgetvalue(res, 0, 1), "yes") == 0;

	PQclear(res);

	return true;
}


/*
 * show_receivewal runs "SHOW receivewal" against conn and fills pullOut with
 * whether the connected route has "receivewal = pull" configured. Returns
 * false (pullOut untouched) on any connection/protocol failure, with an
 * error already logged -- the same failure shape check_file_status() uses.
 */
static bool
show_receivewal(PGconn *conn, bool *pullOut)
{
	PGresult *res = PQexec(conn, "SHOW receivewal");

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
	{
		log_error("SHOW receivewal failed: %s", PQresultErrorMessage(res));
		PQclear(res);
		return false;
	}

	*pullOut = strcmp(PQgetvalue(res, 0, 0), "pull") == 0;
	PQclear(res);

	return true;
}


/*
 * push_file streams localPath's contents to conn via ARCHIVE_FILE's own
 * CopyIn, filename already validated by the caller. Returns true only once
 * the server has confirmed the push with a clean CommandComplete (which,
 * per pg_walserver's own cmd_archive_file.c contract, includes the
 * byte-identical "already there" case) -- false on any failure, with an
 * error already logged.
 */
static bool
push_file(PGconn *conn, const char *localPath, const char *filename)
{
	char *quoted = PQescapeLiteral(conn, filename, strlen(filename));

	if (quoted == NULL)
	{
		log_error("Failed to quote \"%s\": %s", filename, PQerrorMessage(conn));
		return false;
	}

	PQExpBuffer command = createPQExpBuffer();

	appendPQExpBuffer(command, "ARCHIVE_FILE %s", quoted);
	PQfreemem(quoted);

	if (PQExpBufferBroken(command))
	{
		log_error("Out of memory building the ARCHIVE_FILE command");
		destroyPQExpBuffer(command);
		return false;
	}

	PGresult *res = PQexec(conn, command->data);

	destroyPQExpBuffer(command);

	if (PQresultStatus(res) != PGRES_COPY_IN)
	{
		log_error("ARCHIVE_FILE \"%s\" failed: %s", filename,
				  PQresultErrorMessage(res));
		PQclear(res);
		return false;
	}

	PQclear(res);

	int fd = open(localPath, O_RDONLY | O_CLOEXEC);

	if (fd < 0)
	{
		log_error("Failed to open \"%s\": %m", localPath);
		(void) PQputCopyEnd(conn, "failed to open the local file");
		(void) PQgetResult(conn);
		return false;
	}

	char buffer[WS_PUSH_CHUNK_SIZE];
	bool ok = true;

	for (;;)
	{
		ssize_t got = read(fd, buffer, sizeof(buffer));

		if (got < 0 && errno == EINTR)
		{
			continue;
		}

		if (got < 0)
		{
			log_error("Failed to read \"%s\": %m", localPath);
			ok = false;
			break;
		}

		if (got == 0)
		{
			break;
		}

		if (PQputCopyData(conn, buffer, (int) got) <= 0)
		{
			log_error("Failed to send \"%s\" to the server: %s",
					  localPath, PQerrorMessage(conn));
			ok = false;
			break;
		}
	}

	close(fd);

	if (PQputCopyEnd(conn, ok ? NULL : "read failure on the source file") <= 0)
	{
		log_error("Failed to end the ARCHIVE_FILE COPY: %s", PQerrorMessage(conn));
		ok = false;
	}

	PGresult *finalRes;
	bool serverOk = ok;

	while ((finalRes = PQgetResult(conn)) != NULL)
	{
		if (PQresultStatus(finalRes) != PGRES_COMMAND_OK)
		{
			log_error("ARCHIVE_FILE \"%s\" failed: %s", filename,
					  PQresultErrorMessage(finalRes));
			serverOk = false;
		}

		PQclear(finalRes);
	}

	return ok && serverOk;
}


int
ws_push_file_client(const char *host, int port, const char *user,
					const char *routeKey, const char *sslmode,
					const char *applicationName,
					const char *localPath, const char *filename)
{
	/*
	 * Reuse this project's own PGSQL connection facility (pgsql_init() +
	 * pgsql_open_connection(), src/bin/common/pgsql.c) rather than a bare
	 * PQconnectdbParams() call: the same retry policy (exponential
	 * backoff with jitter, PQping()-based "wait for the server to become
	 * ready" loop), PGCONNECT_TIMEOUT handling, and notice-processor
	 * wiring every other connection in this codebase already gets, for
	 * free, instead of a one-shot connect attempt with none of that --
	 * exactly like fetch_client.c's own ws_fetch_file_
	 * client() already does for the read side.
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

	/*
	 * Learn the connected route's own "receivewal" setting: it decides
	 * which of the two disjoint behaviors below this invocation runs,
	 * never a manually-set client flag (which would silently go stale the
	 * moment an operator changes the route's own "receivewal" setting
	 * without also updating every archive_command line referencing it).
	 * See cli_archive.h's own header comment and README.md's "The archive
	 * push side" section for the full design.
	 */
	bool receivewalPull = false;

	if (!show_receivewal(conn, &receivewalPull))
	{
		pgsql_finish(&pgsql);
		return 1;
	}

	bool ok;
	bool pushedAsFallback = false;

	if (receivewalPull)
	{
		/*
		 * The route has an embedded receivewal worker writing into the
		 * same directory this push would target: ordinarily never push
		 * here, only ever check -- PostgreSQL's own archive_command retry
		 * loop is the entire retry mechanism, calling this client again
		 * later, cheaply, until the receivewal worker catches up and
		 * CHECK_FILE reports "matches". The one exception is the server's
		 * own "fallback" recommendation (cmd_check_file.h's own header
		 * comment): a hole a streaming worker can never retroactively
		 * fill (typically a timeline switch left filename behind on the
		 * old timeline) -- in that one case, waiting for "the receivewal
		 * worker to catch up" would wait forever, so this pushes it
		 * directly instead, exactly as the non-pull branch below always
		 * does.
		 */
		uint64_t size = 0;
		uint32_t crc = 0;

		if (!ws_file_crc32c(localPath, &size, &crc))
		{
			log_error("Failed to read \"%s\": %m", localPath);
			pgsql_finish(&pgsql);
			return 1;
		}

		char status[16] = { 0 };
		bool fallback = false;

		if (!check_file_status(conn, filename, size, crc, status,
							   sizeof(status), &fallback))
		{
			pgsql_finish(&pgsql);
			return 1;
		}

		if (strcmp(status, "matches") == 0)
		{
			log_info("\"%s\" already matches what \"%s\" has for \"%s\": "
					 "nothing to push", filename, host, routeKey);
			ok = true;
		}
		else if (fallback)
		{
			log_info("\"%s\" is not on \"%s\" route \"%s\" (%s), and its own "
					 "embedded receivewal worker has already streamed past "
					 "it (likely a timeline switch left it behind): pushing "
					 "it directly via ARCHIVE_FILE instead of waiting",
					 filename, host, routeKey, status);
			ok = push_file(conn, localPath, filename);
			pushedAsFallback = true;
		}
		else
		{
			log_error("\"%s\" is not yet on \"%s\" route \"%s\" (%s): "
					  "waiting for its own receivewal worker to catch up",
					  filename, host, routeKey, status);
			ok = false;
		}
	}
	else
	{
		/*
		 * No embedded receivewal worker on this route: this client is the
		 * only writer, so an unconditional push every invocation is safe.
		 * The server's own overwrite-safety (cmd_archive_file.c: compare
		 * real bytes on disk vs. real bytes received) already makes this
		 * idempotent on PostgreSQL's own retries, with no CHECK_FILE round
		 * trip needed first.
		 */
		log_info("Pushing \"%s\" to \"%s\" route \"%s\" via ARCHIVE_FILE",
				 filename, host, routeKey);

		ok = push_file(conn, localPath, filename);
	}

	pgsql_finish(&pgsql);

	if (ok && (!receivewalPull || pushedAsFallback))
	{
		log_info("Archived \"%s\" to \"%s\" route \"%s\"", filename, host, routeKey);
	}

	return ok ? 0 : 1;
}
