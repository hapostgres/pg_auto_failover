/*
 * src/bin/pg_walserver/cli_setup.c
 *   See cli_setup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "cli_create_cert.h"
#include "cli_setup.h"
#include "file_utils.h"
#include "hba.h"
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
		log_error("setup requires --pgdata (this instance's own data root)");
		return false;
	}

	if (!directory_exists(options->pgdata) &&
		pg_mkdir_p((char *) options->pgdata, 0700) == -1)
	{
		log_error("Failed to create \"%s\": %m", options->pgdata);
		return false;
	}

	char configPath[MAXPGPATH] = { 0 };

	config_file_path(options->pgdata, options->configFile,
					 configPath, sizeof(configPath));

	bool wroteAnything = false;

	if (options->havePort)
	{
		char portStr[16] = { 0 };

		sformat(portStr, sizeof(portStr), "%d", options->port);

		if (!config_set_global_property(configPath, "port", portStr))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslCertFile[0] != '\0')
	{
		if (!config_set_global_property(configPath, "ssl-cert-file",
										options->sslCertFile))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslKeyFile[0] != '\0')
	{
		if (!config_set_global_property(configPath, "ssl-key-file",
										options->sslKeyFile))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (options->sslCaFile[0] != '\0')
	{
		if (!config_set_global_property(configPath, "ssl-ca-file",
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

		if (!config_set_global_property(configPath, "auth-timeout",
										authTimeoutStr))
		{
			return false;
		}

		wroteAnything = true;
	}

	if (wroteAnything)
	{
		log_info("setup complete: \"%s\" is ready", configPath);
	}
	else
	{
		log_info("setup: \"%s\" already exists; no --port/--ssl-*/"
				 "--auth-timeout flag was given, nothing to persist -- "
				 "\"pg_walserver serve\"'s own built-in defaults apply "
				 "unless overridden on its own command line",
				 configPath);
	}

	/*
	 * TLS certificate: the same self-signed facility "cluster register"
	 * itself already uses (cli_create_cert.c) -- created here too so a
	 * first "serve" already has TLS ready, rather than only once a second
	 * route forces the issue. Never overwrites an existing certificate.
	 */
	if (!options->noCert)
	{
		char certPath[MAXPGPATH] = { 0 };
		char keyPath[MAXPGPATH] = { 0 };

		sformat(certPath, sizeof(certPath), "%s/server.crt", options->pgdata);
		sformat(keyPath, sizeof(keyPath), "%s/server.key", options->pgdata);

		if (!file_exists(certPath) || !file_exists(keyPath))
		{
			char localHostname[_POSIX_HOST_NAME_MAX] = "pg_walserver";

			(void) gethostname(localHostname, sizeof(localHostname));

			if (!ws_create_cert_run(options->pgdata, localHostname, false))
			{
				log_warn("Failed to create a self-signed certificate for "
						 "\"%s\" -- pass --ssl-cert-file/--ssl-key-file to "
						 "\"serve\", or create \"%s\"/\"%s\" yourself (\"pg_"
						 "walserver create-cert\"), before starting it",
						 options->pgdata, certPath, keyPath);
			}
		}
	}

	/*
	 * HBA file: written with one real, active rule open to this machine's
	 * own local network when ws_setup_autodetect_cidr() finds one (hba.c),
	 * else the same commented-out placeholder "serve"'s own bootstrap path
	 * already falls back to -- never a hard error either way, and never
	 * overwrites an already-existing HBA file.
	 */
	if (!options->noHba)
	{
		char hbaPath[MAXPGPATH] = { 0 };

		sformat(hbaPath, sizeof(hbaPath), "%s/pg_walserver_hba.conf",
				options->pgdata);

		if (!file_exists(hbaPath))
		{
			char certPath[MAXPGPATH] = { 0 };

			sformat(certPath, sizeof(certPath), "%s/server.crt",
					options->pgdata);

			bool tlsAvailable = file_exists(certPath);

			char localCIDR[64] = { 0 };
			bool haveCIDR = ws_setup_autodetect_cidr(localCIDR,
													 sizeof(localCIDR));

			if (!hba_write_setup_default(hbaPath, tlsAvailable,
										 haveCIDR ? localCIDR : NULL))
			{
				log_warn("Failed to create \"%s\"", hbaPath);
			}
			else if (haveCIDR)
			{
				log_info("HBA: admitting \"%s\" (this machine's own local "
						 "network, auto-discovered) in \"%s\" -- review and "
						 "adjust it before running on a reachable network",
						 localCIDR, hbaPath);
			}
			else
			{
				log_info("HBA: could not auto-discover a local network "
						 "CIDR to admit -- \"%s\" was created with its "
						 "default commented-out placeholder; add a rule by "
						 "hand", hbaPath);
			}
		}
	}

	return true;
}
