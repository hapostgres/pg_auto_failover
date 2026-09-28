/*
 * src/bin/pg_walserver/cli_setup.c
 *   See cli_setup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "cli_setup.h"
#include "file_utils.h"
#include "log.h"
#include "routes.h"
#include "string_utils.h"


/*
 * cli_setup_run -- see cli_setup.h's own comment.
 */
bool
cli_setup_run(const WsSetupOptions *options)
{
	if (options->pgdata[0] == '\0')
	{
		log_error("setup requires --pgdata (where pg_walserver.ini lives)");
		return false;
	}

	if (!directory_exists(options->pgdata) &&
		pg_mkdir_p((char *) options->pgdata, 0700) == -1)
	{
		log_error("Failed to create \"%s\": %m", options->pgdata);
		return false;
	}

	char routesPath[MAXPGPATH] = { 0 };

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini",
			options->pgdata);

	bool wroteAnything = false;

	if (options->havePort)
	{
		char portStr[16] = { 0 };

		sformat(portStr, sizeof(portStr), "%d", options->port);

		if (!routes_set_global_property(routesPath, "port", portStr))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslCertFile[0] != '\0')
	{
		if (!routes_set_global_property(routesPath, "ssl-cert-file",
										options->sslCertFile))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslKeyFile[0] != '\0')
	{
		if (!routes_set_global_property(routesPath, "ssl-key-file",
										options->sslKeyFile))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslCaFile[0] != '\0')
	{
		if (!routes_set_global_property(routesPath, "ssl-ca-file",
										options->sslCaFile))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->haveAuthTimeout)
	{
		char authTimeoutStr[16] = { 0 };

		sformat(authTimeoutStr, sizeof(authTimeoutStr), "%d",
				options->authTimeout);

		if (!routes_set_global_property(routesPath, "auth-timeout",
										authTimeoutStr))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (wroteAnything)
	{
		log_info("setup complete: \"%s\" is ready", options->pgdata);
	}
	else
	{
		log_info("setup: \"%s\" already exists; no --port/--ssl-*/"
				 "--auth-timeout flag was given, nothing to persist -- "
				 "\"pg_walserver serve\"'s own built-in defaults apply "
				 "unless overridden on its own command line",
				 options->pgdata);
	}

	return true;
}
