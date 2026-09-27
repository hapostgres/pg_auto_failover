/*
 * src/bin/pg_walserver/cli_create_cert.c
 *   See cli_create_cert.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cli_create_cert.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "pgsetup.h"

bool
ws_create_cert_run(const char *pgdata, const char *hostname, bool force)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("create-cert requires --pgdata");
		return false;
	}

	if (hostname == NULL || hostname[0] == '\0')
	{
		log_error("create-cert requires --hostname");
		return false;
	}

	char certPath[MAXPGPATH] = { 0 };
	char keyPath[MAXPGPATH] = { 0 };

	sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
	sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);

	if (!force && (file_exists(certPath) || file_exists(keyPath)))
	{
		log_error("\"%s\" and/or \"%s\" already exist -- pass --force to "
				  "overwrite them", certPath, keyPath);
		return false;
	}

	PostgresSetup pgSetup = { 0 };

	strlcpy(pgSetup.pgdata, pgdata, sizeof(pgSetup.pgdata));

	if (!pg_create_self_signed_cert(&pgSetup, hostname))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Created a self-signed certificate for \"%s\" (\"%s\"/\"%s\", "
			 "CN=%s) -- replace it with a real one before running on a "
			 "reachable network", pgdata, certPath, keyPath, hostname);

	return true;
}
