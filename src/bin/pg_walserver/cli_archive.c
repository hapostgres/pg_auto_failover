/*
 * src/bin/pg_walserver/cli_archive.c
 *   See cli_archive.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "libpq-fe.h"
#include "pqexpbuffer.h"

#include "cli_archive.h"
#include "file_utils.h"
#include "log.h"
#include "ws_util.h"

#define WS_ARCHIVE_CONNECT_TIMEOUT_SECONDS "10"

/* CopyData chunks of this size when pushing, matching cmd_fetch_file.c's
 * own WS_FETCH_CHUNK_SIZE for the read side */
#define WS_ARCHIVE_CHUNK_SIZE (128 * 1024)


/*
 * check_file_status runs one CHECK_FILE round trip for filename/size/crc32c
 * against conn, filling statusOut (at least 16 bytes) with the server's
 * reply ("missing"/"matches"/"differs"). Returns false (statusOut
 * untouched) on any connection/protocol failure -- always with an error
 * already logged.
 */
static bool
check_file_status(PGconn *conn, const char *filename, uint64_t size,
				  uint32_t crc, char *statusOut, size_t statusOutSize)
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
	PQclear(res);

	return true;
}


/*
 * show_capture runs "SHOW capture" against conn and fills pullOut with
 * whether the connected route has "capture = pull" configured. Returns
 * false (pullOut untouched) on any connection/protocol failure, with an
 * error already logged -- the same failure shape check_file_status() uses.
 */
static bool
show_capture(PGconn *conn, bool *pullOut)
{
	PGresult *res = PQexec(conn, "SHOW capture");

	if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) != 1)
	{
		log_error("SHOW capture failed: %s", PQresultErrorMessage(res));
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
 * per cmd_archive_file.c's own contract, includes the byte-identical
 * "already there" case) -- false on any failure, with an error already
 * logged.
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

	char buffer[WS_ARCHIVE_CHUNK_SIZE];
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


bool
ws_archive_run(const WsWalServerTarget *target, const char *localPath,
			   const char *filename)
{
	char portStr[16];

	sformat(portStr, sizeof(portStr), "%d", target->port);

	const char *keys[] = {
		"host", "port", "user", "dbname", "sslmode", "connect_timeout",
		"fallback_application_name", NULL
	};
	const char *values[] = {
		target->host, portStr, target->user, target->route,
		target->sslmode[0] != '\0' ? target->sslmode : NULL,
		WS_ARCHIVE_CONNECT_TIMEOUT_SECONDS, "pg_walserver archive", NULL
	};

	PGconn *conn = PQconnectdbParams(keys, values, 0);

	if (PQstatus(conn) != CONNECTION_OK)
	{
		log_error("Failed to connect to %s:%d: %s",
				  target->host, target->port, PQerrorMessage(conn));
		PQfinish(conn);
		return false;
	}

	/*
	 * Learn the connected route's own "capture" setting: it decides which
	 * of the two disjoint behaviors below this invocation runs, never a
	 * manually-set client flag (which would silently go stale the moment
	 * an operator changes the route's own "capture" setting without also
	 * updating every archive_command line referencing it). See this file's
	 * own header comment and README.md's "The archive push side" section
	 * for the full design.
	 */
	bool capturePull = false;

	if (!show_capture(conn, &capturePull))
	{
		PQfinish(conn);
		return false;
	}

	bool ok;

	if (capturePull)
	{
		/*
		 * The route has an embedded pull capturer writing into the same
		 * directory this push would target: never push here, only ever
		 * check. PostgreSQL's own archive_command retry loop is the entire
		 * retry mechanism -- it calls this client again later, cheaply,
		 * until the capturer catches up and CHECK_FILE reports "matches".
		 */
		uint64_t size = 0;
		uint32_t crc = 0;

		if (!ws_file_crc32c(localPath, &size, &crc))
		{
			log_error("Failed to read \"%s\": %m", localPath);
			PQfinish(conn);
			return false;
		}

		char status[16] = { 0 };

		if (!check_file_status(conn, filename, size, crc, status, sizeof(status)))
		{
			PQfinish(conn);
			return false;
		}

		if (strcmp(status, "matches") == 0)
		{
			log_info("\"%s\" already matches what \"%s\" has for \"%s\": "
					 "nothing to push", filename, target->host, target->route);
			ok = true;
		}
		else
		{
			log_error("\"%s\" is not yet on \"%s\" route \"%s\" (%s): "
					  "waiting for its own pull capturer to catch up",
					  filename, target->host, target->route, status);
			ok = false;
		}
	}
	else
	{
		/*
		 * No embedded pull capturer on this route: this client is the only
		 * writer, so an unconditional push every invocation is safe. The
		 * server's own overwrite-safety (cmd_archive_file.c: compare real
		 * bytes on disk vs. real bytes received) already makes this
		 * idempotent on PostgreSQL's own retries, with no CHECK_FILE round
		 * trip needed first.
		 */
		log_info("Pushing \"%s\" to \"%s\" route \"%s\" via ARCHIVE_FILE",
				 filename, target->host, target->route);

		ok = push_file(conn, localPath, filename);
	}

	PQfinish(conn);

	if (ok && !capturePull)
	{
		log_info("Archived \"%s\" to \"%s\" route \"%s\"",
				 filename, target->host, target->route);
	}

	return ok;
}
