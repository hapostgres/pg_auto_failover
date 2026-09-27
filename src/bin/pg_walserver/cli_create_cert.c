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

/*
 * ws_create_cert_run writes <pgdata>/server.crt and server.key via
 * pg_create_self_signed_cert() (src/bin/common/pgctl.c), refusing to
 * overwrite already-existing files unless force is set. The one shared
 * helper both `pg_walserver create-cert` (by hand) and cli_setup.c's own
 * ensure_tls_for_multiple_routes() (automatically, the moment a second
 * named route needs a certificate) call, so the create-and-log sequence
 * isn't duplicated between the two call sites. Returns true on success,
 * false with an error already logged otherwise.
 */
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
