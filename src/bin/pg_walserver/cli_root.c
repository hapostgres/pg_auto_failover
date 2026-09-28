/*
 * src/bin/pg_walserver/cli_root.c
 *   Top-level sub-command table for pg_walserver, built on this project's
 *   own command-line framework (src/bin/lib/subcommands.c/commandline.h),
 *   the same way pgaftest's own cli_root.c does for a similarly-sized
 *   standalone binary.
 *
 *   Nine sub-commands:
 *
 *     serve           Run the accept loop (accept_loop.h). This is
 *                     pg_walserver's *default* command: when no sub-command
 *                     name is given at all, main.c injects "serve" into
 *                     argv before calling commandline_run() (see pg_
 *                     walserver_default_argv() below), so `pg_walserver
 *                     --port ...` keeps working exactly as it did before
 *                     this file existed -- the framework itself has no
 *                     notion of a default sub-command, only this project's
 *                     own thin shim provides one.
 *     scram-secret    Print one pg_walserver_passwd line for a user.
 *     setup           Create or validate one pg_walserver.ini route --
 *                     the write/path/upstream/role-check/systemid wizard,
 *                     cli_setup.c, then reloads an already-running "serve"
 *                     for this --pgdata if there is one (never takes a
 *                     base backup itself: "serve" bootstraps one
 *                     automatically, see accept_loop.c's own
 *                     ws_bootstrap_missing_backups()).
 *     fetch-systemid  Fetch a route's upstream system identifier,
 *                     cli_fetch_systemid.c.
 *     basebackup      Take a base backup of a route's upstream,
 *                     cli_basebackup.c.
 *     create-cert     Create a self-signed TLS certificate for --pgdata,
 *                     cli_create_cert.c.
 *     archive-wal     `pg_walserver archive-wal %p %f`: push one WAL/
 *                     .backup file into a route via CHECK_FILE/
 *                     ARCHIVE_FILE, cli_archive.c -- meant to be used as
 *                     (part of) a Postgres archive_command. Named
 *                     "archive-wal", not the bare "archive", so it can't be
 *                     mistaken for something that might also cover base
 *                     backups (see cli_archive.h's own header comment for
 *                     the naming rationale, shared with restore-wal below).
 *     restore-wal     `pg_walserver restore-wal %f %p`: fetch one WAL/
 *                     .backup file from a route via FETCH_FILE
 *                     (src/bin/common/fetch_client.c's own
 *                     ws_fetch_file_client()), cli_restore_wal.c -- meant
 *                     to be used as (part of) a Postgres restore_command.
 *     reload          Send SIGHUP to a running "serve" instance (its pid
 *                     read from <pgdata>/pg_walserver.pid) to re-read
 *                     pg_walserver.ini/pg_walserver_hba.conf and reconcile the
 *                     embedded pull capturer set -- see accept_loop.c's
 *                     own ws_reload_config()/ws_capture_reload().
 *
 *   fetch-systemid/basebackup/create-cert are client-side, one-shot tools
 *   that connect *out*, to a route's own upstream, sharing cli_upstream.c's
 *   own --cluster/--path/--upstream/--host/--port/--user resolution;
 *   archive-wal and restore-wal instead connect to pg_walserver itself
 *   (see cli_archive.c's own header comment for why they do not reuse
 *   cli_upstream.c as-is). See README.md for the full design, including
 *   what plugs into pg_autoctl only in a later, separate PR (no "archiver"
 *   node kind, no monitor schema, no pg_autoctl archive/restore command
 *   with quorum participation).
 *
 *   The one-shot FETCH_FILE client (fetching a WAL segment, not a system
 *   identifier -- a different thing from fetch-systemid above) itself
 *   lives in src/bin/common/fetch_client.c as ws_fetch_file_client(),
 *   linked in-process by whatever needs it -- `restore-wal` above is its
 *   current, real caller; a later, separate "archiving" PR is expected to
 *   also call it directly from `pg_autoctl restore command` once that PR's
 *   own monitor-backed quorum/archiver-node bookkeeping exists on top of
 *   it. Either way, it is linked in-process, never exec'd as a
 *   pg_walserver sub-command of its own. pg_walserver itself only ever
 *   answers FETCH_FILE as a server (see cmd_fetch_file.h).
 *
 *   Every flag and behavior is unchanged from the previous hand-rolled
 *   argv[1] dispatch in main.c: only the dispatch mechanism moved.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "accept_loop.h"
#include "capture.h"
#include "cli_archive.h"
#include "cli_archive_cleanup.h"
#include "cli_basebackup.h"
#include "cli_create_cert.h"
#include "cli_fetch_systemid.h"
#include "cli_internal.h"
#include "cli_restore_wal.h"
#include "cli_setup.h"
#include "cli_upstream.h"
#include "cli_wal_target.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "hba.h"
#include "pidfile.h"
#include "routes.h"
#include "scram.h"
#include "string_utils.h"
#include "tls.h"

extern CommandLine ws_root;
extern char ** pg_walserver_default_argv(int argc, char **argv, int *newArgc);

/* -----------------------------------------------------------------------
 * pg_walserver serve [options]  (the default command)
 * ----------------------------------------------------------------------- */

static WsServerConfig serveConfig = { 0 };
static char servePgdata[MAXPGPATH] = { 0 };
static char serveSslCertFile[MAXPGPATH] = { 0 };
static char serveSslKeyFile[MAXPGPATH] = { 0 };
static char serveSslCaFile[MAXPGPATH] = { 0 };
static bool serveInsecure = false;
static char servePidfilePath[MAXPGPATH] = { 0 };

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


static struct option serveLongOptions[] = {
	{ "port", required_argument, NULL, 'p' },
	{ "pgdata", required_argument, NULL, 'D' },
	{ "ssl-cert-file", required_argument, NULL, 'C' },
	{ "ssl-key-file", required_argument, NULL, 'K' },
	{ "ssl-ca-file", required_argument, NULL, 'A' },
	{ "auth-timeout", required_argument, NULL, 'T' },
	{ "insecure", no_argument, NULL, 'I' },
	{ NULL, 0, NULL, 0 }
};

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

	/* --pgdata, parsed below, takes precedence over this default */
	(void) get_env_pgdata(servePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "p:D:", serveLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'p':
			{
				if (!stringToInt(optarg, &(serveConfig.port)))
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					exit(1);
				}
				break;
			}

			case 'D':
			{
				strlcpy(servePgdata, optarg, sizeof(servePgdata));
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
 * derives the routes/HBA/passwd paths under --pgdata, initializes TLS and
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

	if (serveConfig.port <= 0 || serveConfig.port > 65535)
	{
		log_fatal("Invalid --port value");
		exit(1);
	}

	/*
	 * Without --pgdata (or PGDATA) there is no HBA file, no routes and no
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
		sformat(serveConfig.routesPath, sizeof(serveConfig.routesPath),
				"%s/pg_walserver.ini", servePgdata);
		sformat(serveConfig.auth.hbaPath, sizeof(serveConfig.auth.hbaPath),
				"%s/pg_walserver_hba.conf", servePgdata);
		sformat(serveConfig.auth.passwdPath, sizeof(serveConfig.auth.passwdPath),
				"%s/pg_walserver_passwd", servePgdata);

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
		 * More than one *named* route (the "*" wildcard doesn't count: a
		 * single named route plus a wildcard fallback is still fully
		 * disambiguated by dbname alone) and no TLS: refuse to start.
		 * dbname-based routing cannot tell a real physical standby's
		 * connection apart from any other route once there is more than
		 * one -- every such standby's own walreceiver always sends the
		 * literal dbname "replication", never a real route key (see
		 * auth.c's own comment) -- so TLS SNI is the only way left to
		 * address more than one route by name. `pg_walserver setup` already
		 * creates a self-signed certificate the moment it writes a second
		 * route, precisely so this check never fires for a deployment
		 * built with it; it exists here too for a pg_walserver.ini
		 * hand-edited or driven some other way.
		 */

		/*
		 * Parse pg_walserver.ini and pg_walserver_hba.conf once, up front:
		 * both are cached in serveConfig (WsServerConfig.routes/routeCount,
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
		if (!routes_load(serveConfig.routesPath, &serveConfig.routes,
						 &serveConfig.routeCount))
		{
			log_fatal("Failed to parse \"%s\": refusing to start",
					  serveConfig.routesPath);
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

		int namedRouteCount = 0;

		for (int i = 0; i < serveConfig.routeCount; i++)
		{
			if (!streq(serveConfig.routes[i].key, WS_ROUTES_WILDCARD_KEY))
			{
				namedRouteCount++;
			}
		}

		if (namedRouteCount > 1 && !ws_tls_server_enabled())
		{
			log_fatal("\"%s\" has %d named routes but TLS is not "
					  "enabled: more than one route requires TLS (for "
					  "SNI-based routing) to be reachable by name at "
					  "all -- pass --ssl-cert-file/--ssl-key-file, or "
					  "create <pgdata>/server.crt and server.key "
					  "(\"pg_walserver setup\" already does this "
					  "automatically)", serveConfig.routesPath,
					  namedRouteCount);
			exit(1);
		}

		/*
		 * Every "capture = pull" route gets its own supervised
		 * embedded pg_receivewal child (capture.c) -- started here,
		 * once, now that pg_walserver.ini/HBA validation above has
		 * already succeeded, and before ws_accept_loop() (and thus
		 * before any connection child can be forked). See capture.h's
		 * own comment for the full startup/shutdown contract.
		 */
		(void) ws_capture_start_all(serveConfig.routes, serveConfig.routeCount);

		/*
		 * Now that every "capture = pull" route's own real capturer above
		 * has been started, check every route for a missing base backup
		 * and kick off an automatic bootstrap for it in the background --
		 * the first of the two trigger points documented in accept_loop.h's
		 * own ws_bootstrap_missing_backups() comment (the second being a
		 * successful SIGHUP reload, ws_reload_config(), accept_loop.c).
		 */
		ws_bootstrap_missing_backups(serveConfig.routes, serveConfig.routeCount);

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


static CommandLine serve_command =
	make_command("serve",
				 "Run the pg_walserver accept loop (the default command)",
				 "[--port <port>] [--pgdata <path> | --insecure] "
				 "[--ssl-cert-file <path> --ssl-key-file <path>] "
				 "[--ssl-ca-file <path>] "
				 "[--auth-timeout <seconds>]",
				 "  --port      port to listen on (default: 6543)\n"
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              the PGDATA environment variable); the "
				 "routes file mapping\n"
				 "              each route key (an opaque string; "
				 "pg_auto_failover's own\n"
				 "              convention is \"<formation>/<group>\") to "
				 "its own storage path\n"
				 "              is read from <pgdata>/pg_walserver.ini, "
				 "and access is\n"
				 "              decided by <pgdata>/pg_walserver_hba.conf; the "
				 "server refuses to\n"
				 "              start without it unless --insecure is "
				 "given\n"
				 "  --insecure  no --pgdata: accept any dbname WITHOUT ANY "
				 "authentication;\n"
				 "              for manual testing only, never on a "
				 "reachable network\n"
				 "  --ssl-cert-file / --ssl-key-file  server certificate "
				 "(default:\n"
				 "              <pgdata>/server.crt / <pgdata>/server.key)\n"
				 "  --ssl-ca-file  trusted CA bundle for TLS client "
				 "certificate verification\n"
				 "              (default: <pgdata>/ca.crt); required for a "
				 "\"clientcert=\n"
				 "              verify-full\" HBA line to have anything to "
				 "validate against\n"
				 "  --auth-timeout  absolute deadline in seconds for a "
				 "connection to\n"
				 "              complete startup, TLS, HBA and "
				 "authentication (default: 30)\n",
				 cli_serve_getopt, cli_serve_run);


/* -----------------------------------------------------------------------
 * pg_walserver scram-secret [--user <name>]   (password in PGPASSWORD)
 * ----------------------------------------------------------------------- */

static char scramUser[NAMEDATALEN] = PG_AUTOCTL_REPLICA_USERNAME;

static struct option scramSecretLongOptions[] = {
	{ "user", required_argument, NULL, 'U' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_scram_secret_getopt parses scram-secret's only flag, --user.
 */
static int
cli_scram_secret_getopt(int argc, char **argv)
{
	optind = 0;
	strlcpy(scramUser, PG_AUTOCTL_REPLICA_USERNAME, sizeof(scramUser));

	int c;

	while ((c = getopt_long(argc, argv, "U:", scramSecretLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'U':
			{
				strlcpy(scramUser, optarg, sizeof(scramUser));
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
 * cli_scram_secret_run reads the password from the PGPASSWORD environment
 * variable (never from the command line, where it would show up in the
 * process list), builds its SCRAM-SHA-256 verifier, and prints one
 * "user:secret" pg_walserver_passwd line to stdout.
 */
static void
cli_scram_secret_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	char password[512] = { 0 };

	if (!get_env_copy("PGPASSWORD", password, sizeof(password)) ||
		password[0] == '\0')
	{
		fprintf(stderr, /* IGNORE-BANNED */
				"scram-secret: set the password in PGPASSWORD\n");
		exit(1);
	}

	char secret[512];

	if (!scram_build_verifier(password, WS_SCRAM_ITERATIONS,
							  secret, sizeof(secret)))
	{
		fprintf(stderr, "scram-secret: failed to build the secret\n"); /* IGNORE-BANNED */
		exit(1);
	}

	printf("%s:%s\n", scramUser, secret); /* IGNORE-BANNED */

	exit(0);
}


static CommandLine scram_secret_command =
	make_command("scram-secret",
				 "Print one pg_walserver_passwd line for a user",
				 "[--user <name>]  (password read from PGPASSWORD)",
				 "  --user      role name (default: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
																				  "\n"
																				  "  The password is read from the PGPASSWORD environment "
																				  "variable, never\n"
																				  "  from the command line.\n",
				 cli_scram_secret_getopt, cli_scram_secret_run);


/* -----------------------------------------------------------------------
 * pg_walserver fetch-systemid --cluster <name> --pgdata <path> [--upstream ...]
 * ----------------------------------------------------------------------- */

static char fetchSystemidPgdata[MAXPGPATH] = { 0 };
static char fetchSystemidRoute[NAMEDATALEN + 16] = { 0 };
static char fetchSystemidPath[MAXPGPATH] = { 0 };
static char fetchSystemidUpstream[MAXCONNINFO] = { 0 };
static char fetchSystemidHost[_POSIX_HOST_NAME_MAX] = { 0 };
static char fetchSystemidPort[16] = { 0 };
static char fetchSystemidUser[NAMEDATALEN] = { 0 };
static bool fetchSystemidForce = false;

static struct option fetchSystemidLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_fetch_systemid_getopt(int argc, char **argv)
{
	optind = 0;
	(void) get_env_pgdata(fetchSystemidPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:c:P:u:h:p:U:f",
							fetchSystemidLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(fetchSystemidPgdata, optarg, sizeof(fetchSystemidPgdata));
				break;
			}

			case 'c':
			{
				strlcpy(fetchSystemidRoute, optarg, sizeof(fetchSystemidRoute));
				break;
			}

			case 'P':
			{
				strlcpy(fetchSystemidPath, optarg, sizeof(fetchSystemidPath));
				break;
			}

			case 'u':
			{
				strlcpy(fetchSystemidUpstream, optarg, sizeof(fetchSystemidUpstream));
				break;
			}

			case 'h':
			{
				strlcpy(fetchSystemidHost, optarg, sizeof(fetchSystemidHost));
				break;
			}

			case 'p':
			{
				strlcpy(fetchSystemidPort, optarg, sizeof(fetchSystemidPort));
				break;
			}

			case 'U':
			{
				strlcpy(fetchSystemidUser, optarg, sizeof(fetchSystemidUser));
				break;
			}

			case 'f':
			{
				fetchSystemidForce = true;
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


static void
cli_fetch_systemid_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(fetchSystemidPgdata, fetchSystemidRoute,
							  fetchSystemidPath, fetchSystemidUpstream,
							  fetchSystemidHost, fetchSystemidPort,
							  fetchSystemidUser, &target))
	{
		exit(1);
	}

	exit(cli_fetch_systemid_run(&target, fetchSystemidForce, NULL) ? 0 : 1);
}


static CommandLine fetch_systemid_command =
	make_command("fetch-systemid",
				 "Fetch a route's upstream system identifier",
				 "--cluster <name> --pgdata <path> | --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--force]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --cluster   the cluster name to fetch for (looked up in "
				 "pg_walserver.ini)\n"
				 "  --path      the route's own directory (overrides the "
				 "route's own \"path\")\n"
				 "  --upstream  a libpq connection string to connect with "
				 "(overrides the\n"
				 "              route's own \"upstream\")\n"
				 "  --host / --port / --user  further override individual "
				 "connection\n"
				 "              parameters (default port: 5432, default "
				 "user: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
													  "  --force     overwrite an already-recorded, different "
													  "system identifier\n",
				 cli_fetch_systemid_getopt, cli_fetch_systemid_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver basebackup --cluster <name> --pgdata <path> [--upstream ...]
 * ----------------------------------------------------------------------- */

static char basebackupPgdata[MAXPGPATH] = { 0 };
static char basebackupRoute[NAMEDATALEN + 16] = { 0 };
static char basebackupPath[MAXPGPATH] = { 0 };
static char basebackupUpstream[MAXCONNINFO] = { 0 };
static char basebackupHost[_POSIX_HOST_NAME_MAX] = { 0 };
static char basebackupPort[16] = { 0 };
static char basebackupUser[NAMEDATALEN] = { 0 };

static struct option basebackupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_basebackup_getopt(int argc, char **argv)
{
	optind = 0;
	(void) get_env_pgdata(basebackupPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:c:P:u:h:p:U:",
							basebackupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(basebackupPgdata, optarg, sizeof(basebackupPgdata));
				break;
			}

			case 'c':
			{
				strlcpy(basebackupRoute, optarg, sizeof(basebackupRoute));
				break;
			}

			case 'P':
			{
				strlcpy(basebackupPath, optarg, sizeof(basebackupPath));
				break;
			}

			case 'u':
			{
				strlcpy(basebackupUpstream, optarg, sizeof(basebackupUpstream));
				break;
			}

			case 'h':
			{
				strlcpy(basebackupHost, optarg, sizeof(basebackupHost));
				break;
			}

			case 'p':
			{
				strlcpy(basebackupPort, optarg, sizeof(basebackupPort));
				break;
			}

			case 'U':
			{
				strlcpy(basebackupUser, optarg, sizeof(basebackupUser));
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


static void
cli_basebackup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(basebackupPgdata, basebackupRoute,
							  basebackupPath, basebackupUpstream,
							  basebackupHost, basebackupPort,
							  basebackupUser, &target))
	{
		exit(1);
	}

	exit(cli_basebackup_run(&target, NULL, 0) ? 0 : 1);
}


static CommandLine basebackup_command =
	make_command("basebackup",
				 "Take a base backup of a route's upstream",
				 "--cluster <name> --pgdata <path> | --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --cluster   the cluster name to back up (looked up in "
				 "pg_walserver.ini)\n"
				 "  --path      the route's own directory (overrides the "
				 "route's own \"path\")\n"
				 "  --upstream  a libpq connection string to connect with "
				 "(overrides the\n"
				 "              route's own \"upstream\")\n"
				 "  --host / --port / --user  further override individual "
				 "connection\n"
				 "              parameters (default port: 5432, default "
				 "user: " PG_AUTOCTL_REPLICA_USERNAME ")\n",
				 cli_basebackup_getopt, cli_basebackup_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver setup --cluster <name> --path <dir> --pgdata <path>
 *                     [--upstream ...] [--force]
 * ----------------------------------------------------------------------- */

static WsSetupOptions setupOptions = { 0 };

/*
 * setup's own --cluster short flag is 'C' (uppercase), not 'c': lowercase
 * 'c' is already taken by --capture in this sub-command's own optstring
 * below, unlike fetch-systemid/basebackup/archive-wal/restore-wal, which
 * have no such conflict and use lowercase 'c'.
 */
static struct option setupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "cluster", required_argument, NULL, 'C' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "capture", required_argument, NULL, 'c' },
	{ "no-capture", no_argument, NULL, 'N' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_setup_getopt(int argc, char **argv)
{
	optind = 0;
	setupOptions = (WsSetupOptions) {
		0
	};
	(void) get_env_pgdata(setupOptions.pgdata);

	/*
	 * The embedded pull capturer is on by default now: running "setup"
	 * with no capture-related flag at all writes "capture = pull" (see
	 * write_route_section(), cli_setup.c). --capture none / --no-capture
	 * are the explicit opt-out for a push-only (archive_command-only)
	 * route; --capture pull still works too, a no-op given this default.
	 */
	setupOptions.capturePull = true;

	int c;

	while ((c = getopt_long(argc, argv, "D:C:P:u:h:p:U:n:c:fN",
							setupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(setupOptions.pgdata, optarg, sizeof(setupOptions.pgdata));
				break;
			}

			case 'C':
			{
				strlcpy(setupOptions.route, optarg, sizeof(setupOptions.route));
				break;
			}

			case 'P':
			{
				strlcpy(setupOptions.path, optarg, sizeof(setupOptions.path));
				break;
			}

			case 'u':
			{
				strlcpy(setupOptions.upstream, optarg,
						sizeof(setupOptions.upstream));
				break;
			}

			case 'h':
			{
				strlcpy(setupOptions.host, optarg, sizeof(setupOptions.host));
				break;
			}

			case 'p':
			{
				strlcpy(setupOptions.port, optarg, sizeof(setupOptions.port));
				break;
			}

			case 'U':
			{
				strlcpy(setupOptions.user, optarg, sizeof(setupOptions.user));
				break;
			}

			case 'n':
			{
				strlcpy(setupOptions.hostname, optarg,
						sizeof(setupOptions.hostname));
				break;
			}

			case 'c':
			{
				if (streq(optarg, "pull"))
				{
					setupOptions.capturePull = true;
				}
				else if (streq(optarg, "none"))
				{
					setupOptions.capturePull = false;
				}
				else
				{
					log_fatal("Invalid --capture value \"%s\": recognized "
							  "values are \"pull\" (the default) and "
							  "\"none\"", optarg);
					exit(1);
				}
				break;
			}

			case 'N':
			{
				setupOptions.capturePull = false;
				break;
			}

			case 'f':
			{
				setupOptions.force = true;
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
 * cli_setup_reload_running_server reloads an already-running "pg_walserver
 * serve" for the same --pgdata, if one is running, so it immediately picks
 * up the route "setup" just wrote/validated -- exactly "pg_walserver
 * reload"'s own read_pidfile()/SIGHUP shape (cli_reload_run() above), with
 * one difference: no running server at all is not an error here, only a
 * normal, expected case (e.g. setting up a route before "serve" has ever
 * been started for this --pgdata) -- logged, not fatal, and setup itself
 * still exits 0.
 */
static void
cli_setup_reload_running_server(const char *pgdata)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		return;
	}

	char pidfilePath[MAXPGPATH] = { 0 };

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", pgdata);

	/*
	 * Ignore SIGHUP in THIS one-shot process first, before ever sending it
	 * on -- see cli_reload_run()'s own comment just above for why.
	 */
	signal(SIGHUP, SIG_IGN);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_info("No running \"pg_walserver serve\" found at \"%s\": the "
				 "route just written will take effect the next time "
				 "\"serve\" starts", pidfilePath);
		return;
	}

	if (kill(pid, SIGHUP) != 0)
	{
		if (errno == ESRCH)
		{
			log_info("Pidfile \"%s\" names pid %d, which is not running: "
					 "the route just written will take effect the next "
					 "time \"serve\" starts", pidfilePath, pid);
		}
		else
		{
			log_warn("Failed to send SIGHUP to pg_walserver pid %d: %m", pid);
		}
		return;
	}

	log_info("Reloaded the running pg_walserver (pid %d): it will pick up "
			 "this route immediately", pid);
}


static void
cli_setup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (!cli_setup_run(&setupOptions))
	{
		exit(1);
	}

	cli_setup_reload_running_server(setupOptions.pgdata);

	exit(0);
}


static CommandLine setup_command =
	make_command("setup",
				 "Create or validate one pg_walserver.ini route",
				 "--cluster <name> --path <dir> --pgdata <path> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--hostname <fqdn>] "
				 "[--capture pull|none | --no-capture] "
				 "[--force]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --cluster   the cluster name to create or validate\n"
				 "  --path      the route's own directory, created if "
				 "missing\n"
				 "  --upstream  a libpq connection string, written into the "
				 "route's own\n"
				 "              \"upstream\" property\n"
				 "  --host / --port / --user  further override individual "
				 "connection\n"
				 "              parameters (default port: 5432, default "
				 "user: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
													  "  --hostname  the route's own TLS SNI hostname, written "
													  "into its\n"
													  "              \"hostname\" property -- the only way a "
													  "real physical\n"
													  "              standby can address this route by name "
													  "once more than\n"
													  "              one exists (dbname alone cannot, see "
													  "README.md's\n"
													  "              \"Routing beyond dbname: TLS SNI\" "
													  "section); creates a\n"
													  "              self-signed certificate for\n"
													  "              --pgdata automatically, the moment a "
													  "second route is\n"
													  "              added, if none exists yet\n"
													  "  --capture pull  write \"capture = pull\" into the "
													  "route's own section\n"
													  "              (the default now, even with no --capture "
													  "flag at all):\n"
													  "              the next \"pg_walserver serve\" forks a "
													  "supervised child\n"
													  "              running the embedded pg_receivewal "
													  "capturer against\n"
													  "              this route's own \"upstream\" (capture.c)"
													  " -- see README.md's\n"
													  "              \"The embedded pull capturer\" section\n"
													  "  --capture none / --no-capture  opt this route out of "
													  "the embedded pull\n"
													  "              capturer (push-only, archive_command-only)"
													  "\n"
													  "  --force     change an already-existing route's path, "
													  "or overwrite an\n"
													  "              already-recorded, different system "
													  "identifier\n"
													  "\n"
													  "Reloads an already-running \"pg_walserver serve\" for this "
													  "--pgdata, if one is\n"
													  "running, so it picks up this route immediately; with none "
													  "running, the\n"
													  "config just written takes effect the next time \"serve\" "
													  "starts. Either\n"
													  "way, \"serve\" itself takes this route's first base backup "
													  "automatically\n"
													  "if it doesn't have one yet -- \"setup\" never takes one "
													  "itself.\n",
				 cli_setup_getopt, cli_setup_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver create-cert --pgdata <path> --hostname <name> [--force]
 * ----------------------------------------------------------------------- */

static char createCertPgdata[MAXPGPATH] = { 0 };
static char createCertHostname[_POSIX_HOST_NAME_MAX] = { 0 };
static bool createCertForce = false;

static struct option createCertLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_create_cert_getopt(int argc, char **argv)
{
	optind = 0;
	createCertForce = false;
	(void) get_env_pgdata(createCertPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:n:f",
							createCertLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(createCertPgdata, optarg, sizeof(createCertPgdata));
				break;
			}

			case 'n':
			{
				strlcpy(createCertHostname, optarg, sizeof(createCertHostname));
				break;
			}

			case 'f':
			{
				createCertForce = true;
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


static void
cli_create_cert_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_create_cert_run(createCertPgdata, createCertHostname,
							createCertForce) ? 0 : 1);
}


static CommandLine create_cert_command =
	make_command("create-cert",
				 "Create a self-signed TLS certificate for --pgdata",
				 "--pgdata <path> --hostname <name> [--force]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); the certificate is written as "
				 "\"<pgdata>/server.crt\"\n"
				 "              and \"<pgdata>/server.key\"\n"
				 "  --hostname  the certificate's own CN/subject (the name "
				 "a client's\n"
				 "              TLS SNI, or a human, is expected to use to "
				 "reach this\n"
				 "              server)\n"
				 "  --force     overwrite an already-existing server.crt/"
				 "server.key\n",
				 cli_create_cert_getopt, cli_create_cert_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver archive <path-to-file> <filename>
 *                       --cluster <name> --host <host> [--port <port>]
 *                       [--user <name>] [--sslmode <mode>]
 * ----------------------------------------------------------------------- */

static WsWalServerTarget archiveTarget = { 0 };

static int
cli_archive_getopt(int argc, char **argv)
{
	return cli_wal_target_getopt(argc, argv, &archiveTarget);
}


/*
 * cli_archive_command_run reads the two positional arguments a Postgres
 * archive_command always passes -- %p (the file's real path) and %f (the
 * bare name to archive it under) -- left in argv by the framework once
 * cli_archive_getopt() has consumed every flag (commandline_run(),
 * src/bin/lib/subcommands.c/commandline.c), then runs ws_archive_run()'s
 * whole sequence. Exit code matches PostgreSQL's own archive_command
 * contract exactly: 0 on success (including "already there"), 1 on any
 * failure, so PostgreSQL retries.
 */
static void
cli_archive_command_run(int argc, char **argv)
{
	if (argc != 2)
	{
		log_fatal("archive requires exactly two arguments: <path-to-file> "
				  "<filename> (the \"%%p\" and \"%%f\" a Postgres "
				  "archive_command is invoked with)");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	if (archiveTarget.route[0] == '\0' || archiveTarget.host[0] == '\0')
	{
		log_fatal("archive-wal requires --cluster and --host");
		exit(1);
	}

	exit(ws_archive_run(&archiveTarget, argv[0], argv[1]) ? 0 : 1);
}


static CommandLine archive_command =
	make_command("archive-wal",
				 "Push one WAL/.backup file into a pg_walserver route "
				 "(archive_command)",
				 "<path-to-file> <filename> --cluster <name> --host <host> "
				 "[--port <port>] [--user <name>] [--sslmode <mode>]",
				 "  --cluster   the cluster to archive into (sent as "
				 "dbname)\n"
				 "  --host      the pg_walserver host to connect to\n"
				 "  --port      the pg_walserver port to connect to "
				 "(default: 6543)\n"
				 "  --user      role name (default: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
																				  "  --sslmode   libpq sslmode (default: libpq's own "
																				  "default, \"prefer\")\n"
																				  "\n"
																				  "  Meant to be used as (part of) a Postgres "
																				  "archive_command, e.g.:\n"
																				  "    archive_command = 'pg_walserver archive-wal %%p "
																				  "%%f --cluster mycluster \\\n"
																				  "                       --host archive.example.com "
																				  "--user archiver_repl'\n",
				 cli_archive_getopt, cli_archive_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver restore <filename> <destination-path>
 *                       --cluster <name> --host <host> [--port <port>]
 *                       [--user <name>] [--sslmode <mode>]
 * ----------------------------------------------------------------------- */

static WsWalServerTarget restoreTarget = { 0 };

/*
 * cli_restore_getopt parses restore-wal's flags (--cluster/--host/--port/
 * --user/--sslmode), the same shape and defaults cli_archive_getopt() above
 * uses -- both call the one shared cli_wal_target_getopt() (cli_wal_target.c).
 */
static int
cli_restore_getopt(int argc, char **argv)
{
	return cli_wal_target_getopt(argc, argv, &restoreTarget);
}


/*
 * cli_restore_command_run reads the two positional arguments a Postgres
 * restore_command is invoked with -- %f (the bare filename recovery wants
 * next) then %p (the local path it must be written to), the reverse order
 * of archive_command's own %p/%f (see cli_archive_command_run() above) --
 * left in argv once cli_restore_getopt() has consumed every flag, then
 * runs ws_restore_run(). Exit code matches PostgreSQL's own restore_command
 * contract exactly: 0 with the file written on success, 1 on any failure
 * (including the ordinary "not found" case at the end of recovery), so
 * PostgreSQL decides what to do next the same way it always does.
 */
static void
cli_restore_command_run(int argc, char **argv)
{
	if (argc != 2)
	{
		log_fatal("restore-wal requires exactly two arguments: <filename> "
				  "<destination-path> (the \"%%f\" and \"%%p\" a Postgres "
				  "restore_command is invoked with)");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	if (restoreTarget.route[0] == '\0' || restoreTarget.host[0] == '\0')
	{
		log_fatal("restore-wal requires --cluster and --host");
		exit(1);
	}

	exit(ws_restore_run(&restoreTarget, argv[0], argv[1]) ? 0 : 1);
}


static CommandLine restore_command =
	make_command("restore-wal",
				 "Fetch one WAL/.backup file from a pg_walserver route "
				 "(restore_command)",
				 "<filename> <destination-path> --cluster <name> --host <host> "
				 "[--port <port>] [--user <name>] [--sslmode <mode>]",
				 "  --cluster   the cluster to restore from (sent as "
				 "dbname)\n"
				 "  --host      the pg_walserver host to connect to\n"
				 "  --port      the pg_walserver port to connect to "
				 "(default: 6543)\n"
				 "  --user      role name (default: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
																				  "  --sslmode   libpq sslmode (default: libpq's own "
																				  "default, \"prefer\")\n"
																				  "\n"
																				  "  Meant to be used as (part of) a Postgres "
																				  "restore_command, e.g.:\n"
																				  "    restore_command = 'pg_walserver restore-wal %%f "
																				  "%%p --cluster mycluster \\\n"
																				  "                        --host archive.example.com "
																				  "--user archiver_repl'\n",
				 cli_restore_getopt, cli_restore_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver reload --pgdata <path>
 * ----------------------------------------------------------------------- */

static char reloadPgdata[MAXPGPATH] = { 0 };

static struct option reloadLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_reload_getopt(int argc, char **argv)
{
	optind = 0;
	reloadPgdata[0] = '\0';
	(void) get_env_pgdata(reloadPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", reloadLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(reloadPgdata, optarg, sizeof(reloadPgdata));
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
 * cli_reload_run sends SIGHUP to the "pg_walserver serve" instance whose
 * pid is recorded in <pgdata>/pg_walserver.pid -- the exact same shape as
 * "pg_ctl reload". Follows pg_autoctl's own reload precedent (cli_common.c's
 * cli_pg_autoctl_reload(), cli_service.c's cli_service_reload()): SIGHUP is
 * ignored in THIS process first, before sending it on, because a freshly
 * exec'd one-shot command like this one installs no SIGHUP handler of its
 * own, and can end up reusing the pid of a just-exited process -- the
 * default disposition for an unhandled SIGHUP is to terminate, so without
 * this a stray signal delivered to that reused pid in the narrow window
 * before this command exits could kill it before it ever sends anything.
 * read_pidfile() (src/bin/common/pidfile.h) already does the missing/
 * stale-pidfile detection (a kill(pid, 0) check, removing a stale file);
 * exits 0 once SIGHUP was actually delivered, nonzero with a clear error
 * otherwise.
 */
static void
cli_reload_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (reloadPgdata[0] == '\0')
	{
		log_fatal("reload requires --pgdata (or the PGDATA environment "
				  "variable)");
		exit(1);
	}

	char pidfilePath[MAXPGPATH];

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", reloadPgdata);

	signal(SIGHUP, SIG_IGN);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_fatal("Failed to reload pg_walserver: no running instance found "
				  "at \"%s\" (missing, stale, or unreadable pidfile)",
				  pidfilePath);
		exit(1);
	}

	if (kill(pid, SIGHUP) != 0)
	{
		if (errno == ESRCH)
		{
			log_fatal("Failed to reload pg_walserver: pid %d (from \"%s\") "
					  "is not running", pid, pidfilePath);
		}
		else
		{
			log_fatal("Failed to send SIGHUP to pg_walserver pid %d: %m", pid);
		}
		exit(1);
	}

	log_info("Sent SIGHUP to pg_walserver pid %d", pid);
	exit(0);
}


/* -----------------------------------------------------------------------
 * pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path <dir>
 *                               [--keep-count <N>] [--keep-age <interval>]
 *                               [--dry-run]
 * ----------------------------------------------------------------------- */

static char archiveCleanupPgdata[MAXPGPATH] = { 0 };
static char archiveCleanupRoute[NAMEDATALEN + 16] = { 0 };
static char archiveCleanupPath[MAXPGPATH] = { 0 };
static bool archiveCleanupHaveKeepCount = false;
static int archiveCleanupKeepCount = 0;
static bool archiveCleanupHaveKeepAge = false;
static WsRetentionAge archiveCleanupKeepAge = { 0 };
static bool archiveCleanupDryRun = false;

static struct option archiveCleanupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "keep-count", required_argument, NULL, 'k' },
	{ "keep-age", required_argument, NULL, 'a' },
	{ "dry-run", no_argument, NULL, 'n' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_archive_cleanup_getopt(int argc, char **argv)
{
	optind = 0;
	(void) get_env_pgdata(archiveCleanupPgdata);
	archiveCleanupRoute[0] = '\0';
	archiveCleanupPath[0] = '\0';
	archiveCleanupHaveKeepCount = false;
	archiveCleanupKeepCount = 0;
	archiveCleanupHaveKeepAge = false;
	archiveCleanupKeepAge = (WsRetentionAge) {
		0
	};
	archiveCleanupDryRun = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:c:P:k:a:n",
							archiveCleanupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(archiveCleanupPgdata, optarg, sizeof(archiveCleanupPgdata));
				break;
			}

			case 'c':
			{
				strlcpy(archiveCleanupRoute, optarg, sizeof(archiveCleanupRoute));
				break;
			}

			case 'P':
			{
				strlcpy(archiveCleanupPath, optarg, sizeof(archiveCleanupPath));
				break;
			}

			case 'k':
			{
				if (!stringToInt(optarg, &archiveCleanupKeepCount) ||
					archiveCleanupKeepCount <= 0)
				{
					log_fatal("Invalid --keep-count value \"%s\": expected "
							  "a positive whole number", optarg);
					exit(1);
				}
				archiveCleanupHaveKeepCount = true;
				break;
			}

			case 'a':
			{
				if (!ws_parse_retention_age(optarg, &archiveCleanupKeepAge))
				{
					/* error already logged */
					exit(1);
				}
				archiveCleanupHaveKeepAge = true;
				break;
			}

			case 'n':
			{
				archiveCleanupDryRun = true;
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


static void
cli_archive_cleanup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	char routePath[MAXPGPATH] = { 0 };

	if (archiveCleanupPath[0] != '\0')
	{
		strlcpy(routePath, archiveCleanupPath, sizeof(routePath));
	}
	else if (archiveCleanupPgdata[0] != '\0' && archiveCleanupRoute[0] != '\0')
	{
		char routesPath[MAXPGPATH] = { 0 };
		WsRoute *routes = NULL;
		int routeCount = 0;

		sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini",
				archiveCleanupPgdata);

		const WsRoute *route = NULL;

		if (routes_load(routesPath, &routes, &routeCount))
		{
			route = routes_find(routes, routeCount, archiveCleanupRoute);
		}

		if (route == NULL)
		{
			log_fatal("No route \"%s\" in \"%s\"", archiveCleanupRoute,
					  routesPath);
			routes_free(routes);
			exit(1);
		}

		strlcpy(routePath, route->path, sizeof(routePath));
		routes_free(routes);
	}
	else
	{
		log_fatal("archive-cleanup requires --path, or --cluster with "
				  "--pgdata pointing at a \"pg_walserver.ini\" that has "
				  "that route");
		exit(1);
	}

	exit(ws_archive_cleanup_run(routePath,
								archiveCleanupHaveKeepCount, archiveCleanupKeepCount,
								archiveCleanupHaveKeepAge, archiveCleanupKeepAge,
								archiveCleanupDryRun) ? 0 : 1);
}


static CommandLine archive_cleanup_command =
	make_command("archive-cleanup",
				 "Remove WAL/base backups this route no longer needs to "
				 "keep (operator/cron-driven, never automatic)",
				 "--cluster <name> --pgdata <path> | --path <dir> "
				 "[--keep-count <N>] [--keep-age <interval>] [--dry-run]",
				 "  --pgdata      where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --cluster     the cluster name to clean up (looked up "
				 "in pg_walserver.ini)\n"
				 "  --path        the route's own directory (overrides "
				 "the route's own \"path\")\n"
				 "  --keep-count  keep at least this many of the most "
				 "recent base backups\n"
				 "  --keep-age    keep anything from the last <N><unit> "
				 "(h/d/w/m -- hours,\n"
				 "                days, weeks, calendar months); at "
				 "least one of --keep-count/\n"
				 "                --keep-age is required, retention is "
				 "infinite otherwise\n"
				 "  --dry-run, -n print what would be removed without "
				 "removing anything\n",
				 cli_archive_cleanup_getopt, cli_archive_cleanup_command_run);


static CommandLine reload_command =
	make_command("reload",
				 "Ask a running pg_walserver to reload its configuration",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); sends SIGHUP to the pid recorded "
				 "in\n"
				 "              \"<pgdata>/pg_walserver.pid\"\n",
				 cli_reload_getopt, cli_reload_run);


/* -----------------------------------------------------------------------
 * Root command table
 * ----------------------------------------------------------------------- */

static CommandLine *root_subcommands[] = {
	&serve_command,
	&scram_secret_command,
	&fetch_systemid_command,
	&basebackup_command,
	&setup_command,
	&create_cert_command,
	&archive_command,
	&restore_command,
	&archive_cleanup_command,
	&reload_command,
	&internal_commands,
	NULL
};

CommandLine ws_root =
	make_command_set("pg_walserver",
					 "The archiver's own replication-protocol server",
					 "[serve options] | scram-secret ... | setup ... | "
					 "fetch-systemid ... | basebackup ... | create-cert ... | "
					 "archive-wal ... | restore-wal ... | archive-cleanup ... | "
					 "reload ...",
					 NULL, NULL, root_subcommands);


/*
 * pg_walserver_default_argv implements pg_walserver's "no sub-command means
 * serve" default: the command-line framework itself (commandline.h) has no
 * notion of an optional sub-command name, so main() calls this first and
 * uses whatever it returns instead of the original argv. When argv[1] is
 * already a known sub-command name, or --help/-h (which commandline_run()
 * itself intercepts before ever looking at sub-commands), argv is returned
 * unchanged; otherwise a new argv with "serve" spliced in right after
 * argv[0] is returned, e.g. { "pg_walserver", "--port", "5432" } becomes
 * { "pg_walserver", "serve", "--port", "5432" }. The returned array is
 * malloc'd and, other than the injected "serve", points back into the
 * original argv; it is never freed, living for the rest of the process.
 */
char **
pg_walserver_default_argv(int argc, char **argv, int *newArgc)
{
	if (argc >= 2 &&
		(streq(argv[1], "serve") ||
		 streq(argv[1], "scram-secret") ||
		 streq(argv[1], "setup") ||
		 streq(argv[1], "fetch-systemid") ||
		 streq(argv[1], "basebackup") ||
		 streq(argv[1], "create-cert") ||
		 streq(argv[1], "archive-wal") ||
		 streq(argv[1], "restore-wal") ||
		 streq(argv[1], "archive-cleanup") ||
		 streq(argv[1], "reload") ||
		 streq(argv[1], "internal") ||
		 streq(argv[1], "--help") ||
		 streq(argv[1], "-h")))
	{
		*newArgc = argc;
		return argv;
	}

	char **newArgv = (char **) malloc((size_t) (argc + 2) * sizeof(char *));

	if (newArgv == NULL)
	{
		log_fatal("Failed to allocate memory");
		exit(1);
	}

	newArgv[0] = argv[0];
	newArgv[1] = (char *) "serve";

	for (int i = 1; i < argc; i++)
	{
		newArgv[i + 1] = argv[i];
	}

	newArgv[argc + 1] = NULL;
	*newArgc = argc + 1;

	return newArgv;
}
