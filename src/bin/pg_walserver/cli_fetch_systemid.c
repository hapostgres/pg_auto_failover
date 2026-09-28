/*
 * src/bin/pg_walserver/cli_fetch_systemid.c
 *   See cli_fetch_systemid.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <inttypes.h>
#include <string.h>

#include "postgres_fe.h"

#include "cli_fetch_systemid.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "string_utils.h"

#define WS_SYSTEMID_FILENAME "pg_walserver_systemid"
#define WS_PGVERSION_FILENAME "pg_walserver_pgversion"


/*
 * read_existing_systemid reads "<path>/pg_walserver_systemid" if it exists,
 * returning the parsed identifier in *out. Returns false when the file is
 * absent or unparseable (treated as "no prior identifier", never a hard
 * error: a brand new route has no systemid file yet).
 */
static bool
read_existing_systemid(const char *path, uint64_t *out)
{
	char sysidPath[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;

	sformat(sysidPath, sizeof(sysidPath), "%s/" WS_SYSTEMID_FILENAME, path);

	if (!read_file_if_exists(sysidPath, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool ok = stringToUInt64(contents, out);

	free(contents);

	return ok;
}


/*
 * read_existing_pgversion reads "<path>/pg_walserver_pgversion" if it
 * exists, returning the parsed server_version_num in *out. Returns false
 * when the file is absent or unparseable (treated as "no prior recorded
 * version", never a hard error: a brand new route, or one created before
 * this file existed, has no pgversion file yet).
 */
static bool
read_existing_pgversion(const char *path, int *out)
{
	char pgversionPath[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;

	sformat(pgversionPath, sizeof(pgversionPath), "%s/" WS_PGVERSION_FILENAME,
			path);

	if (!read_file_if_exists(pgversionPath, &contents, &size) ||
		contents == NULL || size == 0)
	{
		return false;
	}

	bool ok = stringToInt(contents, out);

	free(contents);

	return ok;
}


bool
cli_fetch_systemid_run(const WsUpstreamTarget *target, bool force,
					   uint64_t *systemIdentifierOut)
{
	ReplicationSource replicationSource = { 0 };

	replicationSource.primaryNode = target->node;
	strlcpy(replicationSource.userName, target->userName,
			sizeof(replicationSource.userName));
	strlcpy(replicationSource.applicationName, "pg_walserver-fetch-systemid",
			sizeof(replicationSource.applicationName));
	replicationSource.sslOptions = target->sslOptions;

	if (env_exists("PGPASSWORD"))
	{
		(void) get_env_copy("PGPASSWORD", replicationSource.password,
							sizeof(replicationSource.password));
	}

	log_info("Connecting to %s:%d as \"%s\" to fetch the system identifier",
			 target->node.host, target->node.port, target->userName);

	if (!pgctl_identify_system(&replicationSource))
	{
		/* errors have already been logged */
		return false;
	}

	uint64_t identifier = replicationSource.system.identifier;
	uint64_t existingIdentifier = 0;
	bool needSystemIdWrite = true;

	if (read_existing_systemid(target->path, &existingIdentifier))
	{
		if (existingIdentifier == identifier)
		{
			log_info("\"%s\" already has the correct system identifier "
					 "(%" PRIu64 ")", target->path, identifier);

			needSystemIdWrite = false;
		}
		else if (!force)
		{
			log_error("Refusing to overwrite the system identifier already "
					  "recorded for this route: %" PRIu64 " on disk, "
														  "%" PRIu64
					  " from %s:%d -- this route's identity would "
					  "be changing under it, which usually means the wrong "
					  "upstream was given, or this route needs a fresh path "
					  "instead of reusing an old one",
					  existingIdentifier, identifier, target->node.host,
					  target->node.port);
			return false;
		}
		else
		{
			log_warn("Overwriting the system identifier recorded for \"%s\": "
					 "%" PRIu64 " -> %" PRIu64 " (--force)",
					 target->path, existingIdentifier, identifier);
		}
	}

	if (needSystemIdWrite)
	{
		char sysidPath[MAXPGPATH] = { 0 };
		char contents[64] = { 0 };

		sformat(sysidPath, sizeof(sysidPath), "%s/" WS_SYSTEMID_FILENAME,
				target->path);

		int len = sformat(contents, sizeof(contents), "%" PRIu64, identifier);

		if (!write_file_atomic(contents, len, sysidPath))
		{
			log_error("Failed to write \"%s\"", sysidPath);
			return false;
		}

		log_info("Wrote system identifier %" PRIu64 " to \"%s\"",
				 identifier, sysidPath);
	}

	/*
	 * Same overwrite-safety dance, its own file, for the upstream's major
	 * Postgres version -- see cli_basebackup.c's own use of this file to
	 * pick a version-safe pg_basebackup binary.
	 */
	int version = replicationSource.system.serverVersion;
	int existingVersion = 0;
	bool needVersionWrite = true;

	if (read_existing_pgversion(target->path, &existingVersion))
	{
		if (existingVersion == version)
		{
			log_info("\"%s\" already has the correct upstream Postgres "
					 "version (%d)", target->path, version);

			needVersionWrite = false;
		}
		else if (!force)
		{
			log_error("Refusing to overwrite the upstream Postgres version "
					  "already recorded for this route: %d on disk, %d "
					  "from %s:%d -- pass --force to overwrite it "
					  "deliberately",
					  existingVersion, version, target->node.host,
					  target->node.port);
			return false;
		}
		else
		{
			log_warn("Overwriting the upstream Postgres version recorded "
					 "for \"%s\": %d -> %d (--force)",
					 target->path, existingVersion, version);
		}
	}

	if (needVersionWrite)
	{
		char pgversionPath[MAXPGPATH] = { 0 };
		char contents[64] = { 0 };

		sformat(pgversionPath, sizeof(pgversionPath), "%s/" WS_PGVERSION_FILENAME,
				target->path);

		int len = sformat(contents, sizeof(contents), "%d", version);

		if (!write_file_atomic(contents, len, pgversionPath))
		{
			log_error("Failed to write \"%s\"", pgversionPath);
			return false;
		}

		log_info("Wrote upstream Postgres version %d to \"%s\"",
				 version, pgversionPath);
	}

	if (systemIdentifierOut != NULL)
	{
		*systemIdentifierOut = identifier;
	}

	return true;
}
