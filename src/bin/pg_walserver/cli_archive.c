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

/*
 * The bounded intra-invocation recheck (DESIGN-standalone-archiving.md's
 * own "Open questions": "how long, how many rechecks" -- a judgment call,
 * not a spec). Two rechecks, one second apart: short enough that an
 * operator watching archive_command run never mistakes this for a hang
 * (worst case, ~2 extra seconds added to one archive_command invocation,
 * nowhere near PostgreSQL's own retry cadence between whole invocations),
 * long enough to catch the common case of archive_command and a healthy
 * pull-side capturer (an embedded or external pg_receivewal) reacting to
 * the same "this segment just closed" moment within a second or two of
 * each other.
 *
 * The design also describes skipping this wait entirely when the route is
 * known to be push-only (no "capture = pull" configured) -- but
 * pg_walserver.ini has no "capture" property yet in this codebase (that
 * lands with the embedded pull capturer itself, a later, separate piece of
 * work -- see DESIGN-standalone-archiving.md's "Phasing"). Until this
 * client can actually learn whether the route it's archiving into has a
 * pull side, the safe, simple choice is to always do the short bounded
 * recheck: on a push-only route it costs at most WS_ARCHIVE_RECHECK_COUNT
 * cheap CHECK_FILE round trips (no file transfer) before pushing for real,
 * which is negligible next to the push itself. Revisit this the moment
 * "capture" exists to consult.
 */
#define WS_ARCHIVE_RECHECK_COUNT 2
#define WS_ARCHIVE_RECHECK_SLEEP_SECONDS 1

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
ws_archive_run(const WsArchiveTarget *target, const char *localPath,
			   const char *filename)
{
	uint64_t size = 0;
	uint32_t crc = 0;

	/* Step 1: compute the local file's own size and CRC32C -- one
	 * sequential local read, no network cost (DESIGN-standalone-
	 * archiving.md's own "push side" section). */
	if (!ws_file_crc32c(localPath, &size, &crc))
	{
		log_error("Failed to read \"%s\": %m", localPath);
		return false;
	}

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

	/* Step 2: CHECK_FILE. */
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
		PQfinish(conn);
		return true;
	}

	/*
	 * Step 3: missing/differs -- a short, bounded, intra-invocation
	 * recheck before pushing for real, see WS_ARCHIVE_RECHECK_COUNT's own
	 * comment above for why this is unconditional today.
	 */
	for (int attempt = 0;
		 attempt < WS_ARCHIVE_RECHECK_COUNT && strcmp(status, "matches") != 0;
		 attempt++)
	{
		sleep(WS_ARCHIVE_RECHECK_SLEEP_SECONDS);

		if (!check_file_status(conn, filename, size, crc, status, sizeof(status)))
		{
			PQfinish(conn);
			return false;
		}
	}

	if (strcmp(status, "matches") == 0)
	{
		log_info("\"%s\" appeared on \"%s\" while waiting (pull side likely "
				 "delivered it): nothing to push", filename, target->host);
		PQfinish(conn);
		return true;
	}

	/* Step 4: still missing/differs after the recheck -- push for real. */
	log_info("Pushing \"%s\" (%" PRIu64 " bytes, CRC32C %08X, currently "
			 "\"%s\" on \"%s\") via ARCHIVE_FILE",
			 filename, size, crc, status, target->host);

	bool ok = push_file(conn, localPath, filename);

	PQfinish(conn);

	if (ok)
	{
		log_info("Archived \"%s\" to \"%s\" route \"%s\"",
				 filename, target->host, target->route);
	}

	return ok;
}
