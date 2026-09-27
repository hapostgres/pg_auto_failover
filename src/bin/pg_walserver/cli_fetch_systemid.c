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

#define WS_SYSTEMID_FILENAME "archiver-systemid"


/*
 * read_existing_systemid reads "<path>/archiver-systemid" if it exists,
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
	uint64_t existing = 0;

	if (read_existing_systemid(target->path, &existing))
	{
		if (existing == identifier)
		{
			log_info("\"%s\" already has the correct system identifier "
					 "(%" PRIu64 ")", target->path, identifier);

			if (systemIdentifierOut != NULL)
			{
				*systemIdentifierOut = identifier;
			}

			return true;
		}

		if (!force)
		{
			log_error("Refusing to overwrite the system identifier already "
					  "recorded for this route: %" PRIu64 " on disk, "
														  "%" PRIu64
					  " from %s:%d -- this route's identity would "
					  "be changing under it, which usually means the wrong "
					  "upstream was given, or this route needs a fresh path "
					  "instead of reusing an old one",
					  existing, identifier, target->node.host,
					  target->node.port);
			return false;
		}

		log_warn("Overwriting the system identifier recorded for \"%s\": "
				 "%" PRIu64 " -> %" PRIu64 " (--force)",
				 target->path, existing, identifier);
	}

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

	if (systemIdentifierOut != NULL)
	{
		*systemIdentifierOut = identifier;
	}

	return true;
}
