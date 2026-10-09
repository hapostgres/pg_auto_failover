/*
 * src/bin/pg_walserver/cli_serve.c
 *   See cli_serve.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "accept_loop.h"
#include "cli_common.h"
#include "cli_root.h"
#include "cli_serve.h"
#include "defaults.h"
#include "file_utils.h"
#include "hba.h"
#include "log.h"
#include "pidfile.h"
#include "receivewal.h"
#include "clusters.h"
#include "scram.h"
#include "string_utils.h"
#include "tls.h"

/*
 * pg_walserver serve [options]  (the default command)
 */

static WsServerConfig serveConfig = { 0 };
static char servePgdata[MAXPGPATH] = { 0 };
static char serveConfigFile[MAXPGPATH] = { 0 };
static char serveSslCertFile[MAXPGPATH] = { 0 };
static char serveSslKeyFile[MAXPGPATH] = { 0 };
static char serveSslCaFile[MAXPGPATH] = { 0 };
static bool serveInsecure = false;
static char servePidfilePath[MAXPGPATH] = { 0 };
static bool serveHavePort = false;
static bool serveHaveAuthTimeout = false;

/* local helpers */
static bool ws_write_pidfile(const char *pidfile, pid_t pid);

static struct option serveLongOptions[] = {
	{ "port", required_argument, NULL, 'p' },
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "ssl-cert-file", required_argument, NULL, 'C' },
	{ "ssl-key-file", required_argument, NULL, 'K' },
	{ "ssl-ca-file", required_argument, NULL, 'A' },
	{ "auth-timeout", required_argument, NULL, 'T' },
	{ "insecure", no_argument, NULL, 'I' },
	{ NULL, 0, NULL, 0 }
};

static int cli_serve_getopt(int argc, char **argv);
static void cli_serve_run(int argc, char **argv);

CommandLine serve_command =
	make_command("serve",
				 "Run the pg_walserver accept loop",
				 "[--port <port>] [--pgdata <path> | --insecure] "
				 "[--config <path>] "
				 "[--ssl-cert-file <path> --ssl-key-file <path>] "
				 "[--ssl-ca-file <path>] "
				 "[--auth-timeout <seconds>]",
				 "  --port      port to listen on (default: 6543, or "
				 "whatever \"pg_walserver\n"
				 "              setup\" persisted)\n"
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              the PGDATA environment variable); every "
				 "cluster's own storage\n"
				 "              (WAL, base backups) lives under it, and "
				 "so does\n"
				 "              <pgdata>/pg_walserver_hba.conf, unless "
				 "--insecure is given\n"
				 "  --config  where the config file mapping each "
				 "cluster key (an opaque\n"
				 "              string; pg_auto_failover's own convention "
				 "is\n"
				 "              \"<formation>/<group>\") to its own "
				 "storage path lives --\n"
				 "              defaults to <pgdata>/pg_walserver.ini, or "
				 "the\n"
				 "              PG_WALSERVER_CONFIG_FILE environment "
				 "variable; independent\n"
				 "              of --pgdata, for a Debian-style "
				 "deployment (config under\n"
				 "              /etc/pg_walserver/, data under "
				 "/var/lib/pg_walserver/)\n"
				 "  --insecure  no --pgdata: accept any dbname WITHOUT ANY "
				 "authentication;\n"
				 "              for manual testing only, never on a "
				 "reachable network\n"
				 "  --ssl-cert-file / --ssl-key-file  server certificate "
				 "(default:\n"
				 "              <pgdata>/server.crt / <pgdata>/server.key, "
				 "or whatever\n"
				 "              \"pg_walserver setup\" persisted)\n"
				 "  --ssl-ca-file  trusted CA bundle for TLS client "
				 "certificate verification\n"
				 "              (default: <pgdata>/ca.crt); required for a "
				 "\"clientcert=\n"
				 "              verify-full\" HBA line to have anything to "
				 "validate against\n"
				 "  --auth-timeout  absolute deadline in seconds for a "
				 "connection to\n"
				 "              complete startup, TLS, HBA and "
				 "authentication (default: 30,\n"
				 "              or whatever \"pg_walserver setup\" "
				 "persisted)\n",
				 cli_serve_getopt, cli_serve_run);


/*
 * ws_write_pidfile writes this process's own pid to pidfile, one line,
 * "%d\n" -- exactly the shape src/bin/common/pidfile.h's own
 * read_pidfile()/remove_pidfile() expect (read_pidfile() only ever parses
 * the first line, then does a kill(pid, 0) staleness check). Deliberately
 * NOT that same file's create_pidfile(): that one writes pg_autoctl's own
 * multi-line supervisor pidfile format (data directory, pg_autoctl version,
 * extension version, log semaphore id) and requires the PGDATA environment
 * variable to be set, neither of which fits pg_walserver's own single
 * --pgdata-driven, single-long-lived-process model. read_pidfile() and
 * remove_pidfile() are reused as-is: they are already the generic,
 * single-PID-focused half of that API.
 */
static bool
ws_write_pidfile(const char *pidfile, pid_t pid)
{
	char content[32];
	int len = sformat(content, sizeof(content), "%d\n", (int) pid);

	return write_file(content, (size_t) len, pidfile);
}


/*
 * cli_serve_getopt parses every server-mode flag (--port, --pgdata,
 * --ssl-cert-file, --ssl-key-file, --ssl-ca-file, --auth-timeout,
 * --insecure) into the file-scope serveConfig/servePgdata/... variables
 * that cli_serve_run() then acts on. Returns optind, the number of argv
 * slots consumed, exactly as commandline_run() expects from a
 * command_getopt callback.
 */
static int
cli_serve_getopt(int argc, char **argv)
{
	optind = 0;

	serveConfig.port = WS_DEFAULT_PORT;
	serveConfig.authTimeout = WS_DEFAULT_AUTH_TIMEOUT;
	serveHavePort = false;
	serveHaveAuthTimeout = false;

	/* --pgdata, parsed below, takes precedence over this default */
	ws_prefill_pgdata_from_env(servePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "p:D:f:", serveLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'p':
			{
				if (!stringToInt(optarg, &(serveConfig.port)) ||
					serveConfig.port <= 0 || serveConfig.port > 65535)
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					exit(1);
				}
				serveHavePort = true;
				break;
			}

			case 'D':
			{
				strlcpy(servePgdata, optarg, sizeof(servePgdata));
				break;
			}

			case 'f':
			{
				strlcpy(serveConfigFile, optarg, sizeof(serveConfigFile));
				break;
			}

			case 'C':
			{
				strlcpy(serveSslCertFile, optarg, sizeof(serveSslCertFile));
				break;
			}

			case 'K':
			{
				strlcpy(serveSslKeyFile, optarg, sizeof(serveSslKeyFile));
				break;
			}

			case 'A':
			{
				strlcpy(serveSslCaFile, optarg, sizeof(serveSslCaFile));
				break;
			}

			case 'T':
			{
				if (!stringToInt(optarg, &(serveConfig.authTimeout)) ||
					serveConfig.authTimeout <= 0 ||
					serveConfig.authTimeout > 3600)
				{
					log_fatal("Invalid --auth-timeout value \"%s\"", optarg);
					exit(1);
				}
				serveHaveAuthTimeout = true;
				break;
			}

			case 'I':
			{
				serveInsecure = true;
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
 * cli_serve_run brings up the accept loop: it validates --pgdata/--insecure,
 * derives the clusters/HBA/passwd paths under --pgdata, initializes TLS and
 * the default HBA file, seeds the SCRAM mock secret (before any fork, so
 * every connection sees the same one), and calls ws_accept_loop(), which
 * only returns once the server is asked to stop. Never returns on success
 * other than through exit() at process end.
 */
static void
cli_serve_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	/*
	 * Without --pgdata (or PGDATA) there is no HBA file, no clusters and no
	 * authentication at all: refuse to start unless --insecure says that is
	 * what is wanted (manual testing).
	 */
	if (servePgdata[0] == '\0' && !serveInsecure)
	{
		log_fatal("Neither --pgdata nor PGDATA is set: refusing to start "
				  "without authentication; pass --insecure for manual "
				  "testing only");
		exit(1);
	}

	if (servePgdata[0] != '\0' && serveInsecure)
	{
		log_warn("--insecure is ignored: --pgdata is set");
	}

	if (servePgdata[0] != '\0')
	{
		strlcpy(serveConfig.pgdata, servePgdata, sizeof(serveConfig.pgdata));

		config_file_path(servePgdata, serveConfigFile,
						 serveConfig.clustersPath, sizeof(serveConfig.clustersPath));
		sformat(serveConfig.auth.hbaPath, sizeof(serveConfig.auth.hbaPath),
				"%s/pg_walserver_hba.conf", servePgdata);
		sformat(serveConfig.auth.passwdPath, sizeof(serveConfig.auth.passwdPath),
				"%s/pg_walserver_passwd", servePgdata);

		/*
		 * "pg_walserver setup" (cli_setup.c) writes the config file's own
		 * global section (config_load_global(), clusters.c): whichever of
		 * port/ssl-cert-file/ssl-key-file/ssl-ca-file/auth-timeout was
		 * persisted there becomes this instance's own default from here on,
		 * still always overridden by the same flag given directly on this
		 * very command line above (an explicit flag beats a persisted
		 * default, the same precedent this project already follows
		 * everywhere else).
		 */
		WsGlobalConfig globalConfig = { 0 };

		if (!config_load_global(serveConfig.clustersPath, &globalConfig))
		{
			log_fatal("Failed to parse \"%s\": refusing to start",
					  serveConfig.clustersPath);
			exit(1);
		}

		if (!serveHavePort && globalConfig.havePort)
		{
			serveConfig.port = globalConfig.port;
		}

		if (serveConfig.port <= 0 || serveConfig.port > 65535)
		{
			log_fatal("Invalid port %d in \"%s\"", serveConfig.port,
					  serveConfig.clustersPath);
			exit(1);
		}

		if (!serveHaveAuthTimeout && globalConfig.haveAuthTimeout)
		{
			serveConfig.authTimeout = globalConfig.authTimeout;
		}

		if (serveSslCertFile[0] == '\0' && globalConfig.sslCertFile[0] != '\0')
		{
			strlcpy(serveSslCertFile, globalConfig.sslCertFile,
					sizeof(serveSslCertFile));
		}

		if (serveSslKeyFile[0] == '\0' && globalConfig.sslKeyFile[0] != '\0')
		{
			strlcpy(serveSslKeyFile, globalConfig.sslKeyFile,
					sizeof(serveSslKeyFile));
		}

		if (serveSslCaFile[0] == '\0' && globalConfig.sslCaFile[0] != '\0')
		{
			strlcpy(serveSslCaFile, globalConfig.sslCaFile,
					sizeof(serveSslCaFile));
		}

		/* the certificate given with --ssl-*-file, else <pgdata>/server.* */
		char certPath[MAXPGPATH], keyPath[MAXPGPATH];

		if (serveSslCertFile[0] != '\0' && serveSslKeyFile[0] != '\0')
		{
			strlcpy(certPath, serveSslCertFile, sizeof(certPath));
			strlcpy(keyPath, serveSslKeyFile, sizeof(keyPath));
		}
		else
		{
			sformat(certPath, sizeof(certPath), "%s/server.crt", servePgdata);
			sformat(keyPath, sizeof(keyPath), "%s/server.key", servePgdata);
		}

		if (ws_tls_server_init(certPath, keyPath))
		{
			log_info("TLS is enabled (\"%s\")", certPath);

			/*
			 * The CA file, if any, given with --ssl-ca-file, else
			 * <pgdata>/ca.crt (the same --ssl-cert-file/--ssl-key-file
			 * default-path convention above, applied to the CA). Optional:
			 * with no usable CA file, TLS still works exactly as before,
			 * only "clientcert=verify-full" HBA lines cannot be satisfied
			 * (checked below, once the HBA file itself is parsed).
			 */
			char caPath[MAXPGPATH];

			if (serveSslCaFile[0] != '\0')
			{
				strlcpy(caPath, serveSslCaFile, sizeof(caPath));
			}
			else
			{
				sformat(caPath, sizeof(caPath), "%s/ca.crt", servePgdata);
			}

			if (file_exists(caPath))
			{
				if (ws_tls_server_load_ca(caPath))
				{
					log_info("TLS client certificate verification is enabled "
							 "(\"%s\")", caPath);
				}
				else
				{
					log_fatal("Failed to load the TLS CA file \"%s\"", caPath);
					exit(1);
				}
			}
			else if (serveSslCaFile[0] != '\0')
			{
				/* an explicit --ssl-ca-file that does not exist is a
				 * startup error, unlike the default path silently absent */
				log_fatal("The TLS CA file \"%s\" does not exist", caPath);
				exit(1);
			}
		}
		else
		{
			log_warn("TLS is not enabled: no usable server.crt/server.key in "
					 "\"%s\"; \"hostssl\" HBA lines will not match", servePgdata);
		}

		if (!hba_write_default_if_missing(serveConfig.auth.hbaPath,
										  ws_tls_server_enabled()))
		{
			log_fatal("Failed to create \"%s\"", serveConfig.auth.hbaPath);
			exit(1);
		}

		/*
		 * More than one *named* cluster (the "*" wildcard doesn't count: a
		 * single named cluster plus a wildcard fallback is still fully
		 * disambiguated by dbname alone) and no TLS: refuse to start.
		 * dbname-based addressing cannot tell a real physical standby's
		 * connection apart from any other cluster once there is more than
		 * one -- every such standby's own walreceiver always sends the
		 * literal dbname "replication", never a real cluster key (see
		 * auth.c's own comment) -- so TLS SNI is the only way left to
		 * address more than one cluster by name. `pg_walserver setup` already
		 * creates a self-signed certificate the moment it writes a second
		 * cluster, precisely so this check never fires for a deployment
		 * built with it; it exists here too for a pg_walserver.ini
		 * hand-edited or driven some other way.
		 */

		/*
		 * Parse pg_walserver.ini and pg_walserver_hba.conf once, up front:
		 * both are cached in serveConfig (WsServerConfig.clusters/clusterCount,
		 * WsAuthConfig.hbaRuleSet) and installed only once they parse
		 * cleanly -- every connection reads this same in-memory snapshot
		 * from here on, never the files themselves (see accept_loop.c's
		 * handle_connection()). A SIGHUP later re-parses both and swaps
		 * them in atomically, the same way, only if both still parse
		 * (ws_reload_config(), accept_loop.c) -- refusing to start on an
		 * unparsable file here is the same "fail closed" policy applied at
		 * startup instead of leaving every future connection to discover
		 * it on its own.
		 */
		if (!clusters_load(serveConfig.clustersPath, &serveConfig.clusters,
						   &serveConfig.clusterCount))
		{
			log_fatal("Failed to parse \"%s\": refusing to start",
					  serveConfig.clustersPath);
			exit(1);
		}

		if (!hba_parse_file(serveConfig.auth.hbaPath, &serveConfig.auth.hbaRuleSet))
		{
			log_fatal("Failed to parse \"%s\": refusing to start",
					  serveConfig.auth.hbaPath);
			exit(1);
		}

		if (hba_ruleset_requires_client_cert(&serveConfig.auth.hbaRuleSet) &&
			!ws_tls_client_verification_enabled())
		{
			log_fatal("\"%s\" has a \"clientcert=verify-full\" rule but no "
					  "usable TLS CA file: pass --ssl-ca-file, or create "
					  "<pgdata>/ca.crt", serveConfig.auth.hbaPath);
			exit(1);
		}

		int namedClusterCount = 0;

		for (int i = 0; i < serveConfig.clusterCount; i++)
		{
			if (!streq(serveConfig.clusters[i].key, WS_CLUSTERS_WILDCARD_KEY))
			{
				namedClusterCount++;
			}
		}

		if (namedClusterCount > 1 && !ws_tls_server_enabled())
		{
			log_fatal("\"%s\" has %d named clusters but TLS is not "
					  "enabled: more than one cluster requires TLS (for "
					  "SNI-based addressing) to be reachable by name at "
					  "all -- pass --ssl-cert-file/--ssl-key-file, or "
					  "create <pgdata>/server.crt and server.key "
					  "(\"pg_walserver setup\" already does this "
					  "automatically)", serveConfig.clustersPath,
					  namedClusterCount);
			exit(1);
		}

		/*
		 * Every "receivewal = pull" cluster gets its own supervised
		 * embedded pg_receivewal child (receivewal.c) -- started here,
		 * once, now that pg_walserver.ini/HBA validation above has
		 * already succeeded, and before ws_accept_loop() (and thus
		 * before any connection child can be forked). See receivewal.h's
		 * own comment for the full startup/shutdown contract.
		 */
		(void) ws_receivewal_start_all(serveConfig.clusters, serveConfig.clusterCount);

		/*
		 * Now that every "receivewal = pull" cluster's own real receivewal worker above
		 * has been started, check every cluster for a missing base backup
		 * and kick off an automatic bootstrap for it in the background --
		 * the first of the two trigger points documented in accept_loop.h's
		 * own ws_bootstrap_missing_backups() comment (the second being a
		 * successful SIGHUP reload, ws_reload_config(), accept_loop.c).
		 */
		ws_bootstrap_missing_backups(serveConfig.clusters, serveConfig.clusterCount);

		/*
		 * The pidfile is what "pg_walserver reload"/"pg_ctl reload"-style
		 * tooling signals -- written only now, after every other startup
		 * validation above has already succeeded, so a pidfile only ever
		 * exists for a pg_walserver that is genuinely about to serve.
		 * Removed again on clean shutdown, below.
		 */
		sformat(servePidfilePath, sizeof(servePidfilePath), "%s/pg_walserver.pid",
				servePgdata);

		if (!ws_write_pidfile(servePidfilePath, getpid()))
		{
			log_fatal("Failed to write pidfile \"%s\"", servePidfilePath);
			exit(1);
		}
	}

	/* before any fork: every connection must see the same mock secret */
	(void) scram_mock_init();

	bool ok = ws_accept_loop(&serveConfig);

	if (servePidfilePath[0] != '\0')
	{
		(void) remove_pidfile(servePidfilePath);
	}

	exit(ok ? 0 : 1);
}
