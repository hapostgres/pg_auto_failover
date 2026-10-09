/*
 * src/bin/pg_walserver/cli_setup.c
 *   See cli_setup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_common.h"
#include "cli_create_cert.h"
#include "cli_root.h"
#include "cli_setup.h"
#include "file_utils.h"
#include "hba.h"
#include "log.h"
#include "clusters.h"
#include "string_utils.h"

static int cli_setup_getopt(int argc, char **argv);
static void cli_setup_command_run(int argc, char **argv);


/* -----------------------------------------------------------------------
 * pg_walserver setup --pgdata <path> [--port <port>]
 *                     [--ssl-cert-file <path>] [--ssl-key-file <path>]
 *                     [--ssl-ca-file <path>] [--auth-timeout <seconds>]
 * ----------------------------------------------------------------------- */

static WsSetupOptions setupOptions = { 0 };

static struct option setupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "port", required_argument, NULL, 'p' },
	{ "ssl-cert-file", required_argument, NULL, 'C' },
	{ "ssl-key-file", required_argument, NULL, 'K' },
	{ "ssl-ca-file", required_argument, NULL, 'A' },
	{ "auth-timeout", required_argument, NULL, 'T' },
	{ "no-hba", no_argument, NULL, 'H' },
	{ "no-cert", no_argument, NULL, 'N' },
	{ NULL, 0, NULL, 0 }
};

CommandLine setup_command =
	make_command("setup",
				 "Configure pg_walserver itself (port, TLS, auth-timeout, HBA)",
				 "--pgdata <path> [--config <path>] [--port <port>] "
				 "[--ssl-cert-file <path>] [--ssl-key-file <path>] "
				 "[--ssl-ca-file <path>] [--auth-timeout <seconds>] "
				 "[--no-hba] [--no-cert]",
				 "  --pgdata    this instance's own data root, created if "
				 "missing\n"
				 "              (defaults to PGDATA); the config file "
				 "itself lives at\n"
				 "              <pgdata>/pg_walserver.ini unless "
				 "--config overrides it\n"
				 "  --config  where the config file itself lives, "
				 "independent of\n"
				 "              --pgdata (or the PG_WALSERVER_CONFIG_FILE "
				 "environment\n"
				 "              variable) -- a Debian-style deployment's "
				 "own split, e.g.\n"
				 "              /etc/pg_walserver/pg_walserver.ini for "
				 "config, --pgdata\n"
				 "              at /var/lib/pg_walserver for data; every "
				 "other pg_walserver\n"
				 "              command below takes this same flag\n"
				 "  --port      \"serve\"'s own default port when its own "
				 "--port isn't\n"
				 "              given (defaults to 6543)\n"
				 "  --ssl-cert-file / --ssl-key-file / --ssl-ca-file  "
				 "\"serve\"'s own\n"
				 "              defaults when its own equivalent flag "
				 "isn't given\n"
				 "  --auth-timeout  \"serve\"'s own default auth-timeout "
				 "when its own\n"
				 "              --auth-timeout isn't given\n"
				 "  --no-hba    skip auto-creating <pgdata>/pg_walserver_"
				 "hba.conf\n"
				 "              (see below)\n"
				 "  --no-cert   skip auto-creating a self-signed TLS "
				 "certificate\n"
				 "              (see below)\n"
				 "\n"
				 "Every --port/--ssl-*/--auth-timeout flag here is "
				 "optional and independent:\n"
				 "only whichever ones are given get written; each is "
				 "\"serve\"'s own default\n"
				 "from then on, still overridden by the same flag given "
				 "directly to \"serve\"\n"
				 "itself. Unconditionally, unless skipped: a self-signed "
				 "TLS certificate is\n"
				 "created (the same facility \"cluster register\" itself "
				 "uses, --no-cert skips\n"
				 "it), and <pgdata>/pg_walserver_hba.conf is created with "
				 "one real, active\n"
				 "rule open to this machine's own local network, "
				 "auto-discovered the same\n"
				 "way pg_autoctl discovers its own LAN CIDR (--no-hba "
				 "skips it, falling back\n"
				 "to a commented-out placeholder, same as when discovery "
				 "itself finds\n"
				 "nothing to use) -- review and adjust either default "
				 "before running on a\n"
				 "reachable network. Neither step ever overwrites a file "
				 "that already exists.\n"
				 "Nothing about any one archived cluster -- see "
				 "\"pg_walserver cluster\"\n"
				 "for registering, dropping, listing, or re-pointing "
				 "those.\n",
				 cli_setup_getopt, cli_setup_command_run);


/*
 * ws_setup_execute writes whichever of options's own fields were actually
 * given into the config file's own global section (config_set_global_
 * property(), clusters.c), then auto-provisions the certificate and HBA
 * file -- see this file's own header comment. Creates --pgdata if it
 * doesn't exist yet. Returns true on success, false with an error already
 * logged otherwise. Never touches any cluster's own section.
 */
bool
ws_setup_execute(const WsSetupOptions *options)
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
	 * cluster forces the issue. Never overwrites an existing certificate.
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


/*
 * cli_setup_getopt parses "pg_walserver setup"'s own flags into the
 * file-scope statics above.
 */
static int
cli_setup_getopt(int argc, char **argv)
{
	optind = 0;
	setupOptions = (WsSetupOptions) {
		0
	};
	ws_prefill_pgdata_from_env(setupOptions.pgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:p:C:K:A:T:HN",
							setupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(setupOptions.pgdata, optarg, sizeof(setupOptions.pgdata));
				break;
			}

			case 'f':
			{
				strlcpy(setupOptions.configFile, optarg,
						sizeof(setupOptions.configFile));
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &(setupOptions.port)) ||
					setupOptions.port <= 0 || setupOptions.port > 65535)
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					exit(1);
				}
				setupOptions.havePort = true;
				break;
			}

			case 'C':
			{
				strlcpy(setupOptions.sslCertFile, optarg,
						sizeof(setupOptions.sslCertFile));
				break;
			}

			case 'K':
			{
				strlcpy(setupOptions.sslKeyFile, optarg,
						sizeof(setupOptions.sslKeyFile));
				break;
			}

			case 'A':
			{
				strlcpy(setupOptions.sslCaFile, optarg,
						sizeof(setupOptions.sslCaFile));
				break;
			}

			case 'T':
			{
				if (!stringToInt(optarg, &(setupOptions.authTimeout)) ||
					setupOptions.authTimeout <= 0 ||
					setupOptions.authTimeout > 3600)
				{
					log_fatal("Invalid --auth-timeout value \"%s\"", optarg);
					exit(1);
				}
				setupOptions.haveAuthTimeout = true;
				break;
			}

			case 'H':
			{
				setupOptions.noHba = true;
				break;
			}

			case 'N':
			{
				setupOptions.noCert = true;
				break;
			}

			default:
			{
				commandline_print_usage(&ws_root, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_setup_command_run runs "pg_walserver setup" against the options
 * cli_setup_getopt parsed above, then exit()s with its own result.
 */
static void
cli_setup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_setup_execute(&setupOptions) ? 0 : 1);
}
