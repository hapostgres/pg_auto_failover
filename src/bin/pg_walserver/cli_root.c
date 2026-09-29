/*
 * src/bin/pg_walserver/cli_root.c
 *   Top-level sub-command table for pg_walserver, built on this project's
 *   own command-line framework (src/bin/lib/subcommands.c/commandline.h),
 *   the same way pgaftest's own cli_root.c does for a similarly-sized
 *   standalone binary.
 *
 *   Sub-commands:
 *
 *     serve           Run the accept loop (accept_loop.h). No sub-command
 *                     is ever implicit: `pg_walserver` alone (or any
 *                     unrecognized/missing sub-command) only prints usage
 *                     and exits non-zero -- `pg_walserver serve ...` must
 *                     always be spelled out. Applies the config file's own
 *                     global section (config_load_global(), routes.c, see
 *                     "setup" below) as its own port/TLS/auth-timeout
 *                     defaults whenever the equivalent flag isn't given
 *                     directly on this command line.
 *     scram-secret    Print one pg_walserver_passwd line for a user.
 *     setup           Configure pg_walserver *itself* (port, TLS,
 *                     auth-timeout) -- writes pg_walserver.ini's own
 *                     global section, cli_setup.c. Nothing about any one
 *                     archived cluster, see "cluster" below for that.
 *     cluster         register/drop/list/set-upstream: create, remove,
 *                     list, and re-point the clusters (routes) this
 *                     instance archives, cli_cluster.c -- split out of
 *                     what used to be "setup" above. "cluster register"
 *                     reloads an already-running "serve" for this
 *                     --pgdata if there is one (never takes a base backup
 *                     itself: "serve" bootstraps one automatically, see
 *                     accept_loop.c's own ws_bootstrap_missing_backups()).
 *     fetch-systemid  Fetch a route's upstream system identifier,
 *                     cli_fetch_systemid.c.
 *     basebackup      Take a base backup of a route's upstream,
 *                     cli_basebackup.c. With --keep-count/--keep-age, also
 *                     runs archive-cleanup's own retention pass (ws_
 *                     archive_cleanup_run(), cli_archive_cleanup.c) against
 *                     the route right after -- one cron line that both
 *                     backs up and prunes; a cleanup refusal never takes
 *                     back the backup that was just taken.
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
 *                     embedded receivewal worker set -- see accept_loop.c's
 *                     own ws_reload_config()/ws_receivewal_reload().
 *     archive-cleanup Retention: remove WAL/.history/.backup files, and any
 *                     base backup no longer restorable once they are gone,
 *                     older than --keep-age or beyond --keep-count (never
 *                     both -- the more conservative of the two always
 *                     wins), cli_archive_cleanup.c. Before deleting
 *                     anything, a pre-flight check (accounting for
 *                     legitimate timeline switches) refuses the whole
 *                     operation if it would leave a kept backup with a
 *                     WAL gap; --force bypasses that refusal only, never
 *                     to be passed blindly by an unattended cron job.
 *                     Never run automatically otherwise; an operator's
 *                     own cron job, exactly like real pg_archivecleanup.
 *     ps              Process-level view: "serve"'s own pid, each
 *                     supervised embedded receivewal worker child, any in-flight
 *                     bootstrap backup -- cli_ps.c, reading the small state
 *                     file "serve" itself keeps current (a separate
 *                     process cannot see another process's own in-memory
 *                     structs), cross-checked with a real kill(pid, 0)
 *                     liveness probe.
 *     ls              pg_walserver's own on-disk footprint under --pgdata
 *                     (pg_walserver.ini, pg_walserver_hba.conf,
 *                     pg_walserver_passwd, server.crt/key, pg_walserver.pid,
 *                     ...) -- cli_ls.c. Not the archived data itself, see
 *                     "list" below for that.
 *     status          One-screen dashboard: running or not, pid, cluster/
 *                     backup/receivewal worker counts, pending bootstrap backups --
 *                     cli_status.c.
 *     list            clusters/backups/wal sub-targets over the archived
 *                     data itself (not pg_walserver's own bookkeeping
 *                     files, see "ls" above) -- cli_list.c. "list wal"
 *                     defaults to aggregate stats, "--segments" lists
 *                     every individual file.
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
#include "receivewal.h"
#include "cli_archive.h"
#include "cli_archive_cleanup.h"
#include "cli_basebackup.h"
#include "cli_cluster.h"
#include "cli_create_cert.h"
#include "cli_fetch_systemid.h"
#include "cli_internal.h"
#include "cli_list.h"
#include "cli_ls.h"
#include "cli_ps.h"
#include "cli_restore_wal.h"
#include "cli_setup.h"
#include "cli_status.h"
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


/*
 * ws_prefill_pgdata_from_env fills pgdata from the PGDATA environment
 * variable, before this sub-command's own getopt loop parses --pgdata
 * (which then overrides whatever this wrote, if given). PGDATA being
 * unset here is entirely normal -- every caller below only ever uses this
 * as an optional default, never a requirement -- so this deliberately
 * does NOT call env_utils.c's own get_env_pgdata(): that function
 * unconditionally log_error()s when the variable is unset (right, for
 * its own other callers in this codebase, e.g. pidfile.c's own
 * create_pidfile(), which genuinely cannot proceed without it), which
 * would otherwise print a scary, misleading ERROR on every single
 * pg_walserver invocation that passes --pgdata explicitly and simply
 * never has PGDATA set in its environment at all -- the common case for
 * a cron job or a one-off command.
 */
static void
ws_prefill_pgdata_from_env(char *pgdata)
{
	if (env_exists("PGDATA"))
	{
		(void) get_env_copy("PGDATA", pgdata, MAXPGPATH);
	}
}


/* -----------------------------------------------------------------------
 * pg_walserver serve [options]  (the default command)
 * ----------------------------------------------------------------------- */

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
	{ "config", required_argument, NULL, 'f' },
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
		strlcpy(serveConfig.pgdata, servePgdata, sizeof(serveConfig.pgdata));

		config_file_path(servePgdata, serveConfigFile,
						 serveConfig.routesPath, sizeof(serveConfig.routesPath));
		sformat(serveConfig.auth.hbaPath, sizeof(serveConfig.auth.hbaPath),
				"%s/pg_walserver_hba.conf", servePgdata);
		sformat(serveConfig.auth.passwdPath, sizeof(serveConfig.auth.passwdPath),
				"%s/pg_walserver_passwd", servePgdata);

		/*
		 * "pg_walserver setup" (cli_setup.c) writes the config file's own
		 * global section (config_load_global(), routes.c): whichever of
		 * port/ssl-cert-file/ssl-key-file/ssl-ca-file/auth-timeout was
		 * persisted there becomes this instance's own default from here on,
		 * still always overridden by the same flag given directly on this
		 * very command line above (an explicit flag beats a persisted
		 * default, the same precedent this project already follows
		 * everywhere else).
		 */
		WsGlobalConfig globalConfig = { 0 };

		if (!config_load_global(serveConfig.routesPath, &globalConfig))
		{
			log_fatal("Failed to parse \"%s\": refusing to start",
					  serveConfig.routesPath);
			exit(1);
		}

		if (!serveHavePort && globalConfig.havePort)
		{
			serveConfig.port = globalConfig.port;
		}

		if (serveConfig.port <= 0 || serveConfig.port > 65535)
		{
			log_fatal("Invalid port %d in \"%s\"", serveConfig.port,
					  serveConfig.routesPath);
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
		 * Every "receivewal = pull" route gets its own supervised
		 * embedded pg_receivewal child (receivewal.c) -- started here,
		 * once, now that pg_walserver.ini/HBA validation above has
		 * already succeeded, and before ws_accept_loop() (and thus
		 * before any connection child can be forked). See receivewal.h's
		 * own comment for the full startup/shutdown contract.
		 */
		(void) ws_receivewal_start_all(serveConfig.routes, serveConfig.routeCount);

		/*
		 * Now that every "receivewal = pull" route's own real receivewal worker above
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
				 "route's own storage\n"
				 "              (WAL, base backups) lives under it, and "
				 "so does\n"
				 "              <pgdata>/pg_walserver_hba.conf, unless "
				 "--insecure is given\n"
				 "  --config  where the config file mapping each "
				 "route key (an opaque\n"
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
static char fetchSystemidConfigFile[MAXPGPATH] = { 0 };
static char fetchSystemidRoute[NAMEDATALEN + 16] = { 0 };
static char fetchSystemidPath[MAXPGPATH] = { 0 };
static char fetchSystemidUpstream[MAXCONNINFO] = { 0 };
static char fetchSystemidHost[_POSIX_HOST_NAME_MAX] = { 0 };
static char fetchSystemidPort[16] = { 0 };
static char fetchSystemidUser[NAMEDATALEN] = { 0 };
static bool fetchSystemidForce = false;

static struct option fetchSystemidLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'F' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_fetch_systemid_getopt parses "pg_walserver fetch-systemid"'s own flags into the
 * file-scope statics above.
 */
static int
cli_fetch_systemid_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(fetchSystemidPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:F:c:P:u:h:p:U:f",
							fetchSystemidLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(fetchSystemidPgdata, optarg, sizeof(fetchSystemidPgdata));
				break;
			}

			case 'F':
			{
				strlcpy(fetchSystemidConfigFile, optarg,
						sizeof(fetchSystemidConfigFile));
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


/*
 * cli_fetch_systemid_command_run runs "pg_walserver fetch-systemid" against
 * the options cli_fetch_systemid_getopt parsed above, then exit()s with its
 * own result.
 */
static void
cli_fetch_systemid_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(fetchSystemidPgdata, fetchSystemidConfigFile,
							  fetchSystemidRoute,
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
				 "--cluster <name> --pgdata <path> [--config <path>] "
				 "| --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--force]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   the cluster name to fetch for (looked up in "
				 "the config file)\n"
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
static char basebackupConfigFile[MAXPGPATH] = { 0 };
static char basebackupRoute[NAMEDATALEN + 16] = { 0 };
static char basebackupPath[MAXPGPATH] = { 0 };
static char basebackupUpstream[MAXCONNINFO] = { 0 };
static char basebackupHost[_POSIX_HOST_NAME_MAX] = { 0 };
static char basebackupPort[16] = { 0 };
static char basebackupUser[NAMEDATALEN] = { 0 };
static bool basebackupHaveKeepCount = false;
static int basebackupKeepCount = 0;
static bool basebackupHaveKeepAge = false;
static WsRetentionAge basebackupKeepAge = { 0 };
static bool basebackupDryRun = false;
static bool basebackupForce = false;

static struct option basebackupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'F' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "keep-count", required_argument, NULL, 'k' },
	{ "keep-age", required_argument, NULL, 'a' },
	{ "dry-run", no_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_basebackup_getopt parses "pg_walserver basebackup"'s own flags into the
 * file-scope statics above.
 */
static int
cli_basebackup_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(basebackupPgdata);
	basebackupHaveKeepCount = false;
	basebackupKeepCount = 0;
	basebackupHaveKeepAge = false;
	basebackupKeepAge = (WsRetentionAge) {
		0
	};
	basebackupDryRun = false;
	basebackupForce = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:F:c:P:u:h:p:U:k:a:nf",
							basebackupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(basebackupPgdata, optarg, sizeof(basebackupPgdata));
				break;
			}

			case 'F':
			{
				strlcpy(basebackupConfigFile, optarg,
						sizeof(basebackupConfigFile));
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

			case 'k':
			{
				if (!stringToInt(optarg, &basebackupKeepCount) ||
					basebackupKeepCount <= 0)
				{
					log_fatal("Invalid --keep-count value \"%s\": expected "
							  "a positive whole number", optarg);
					exit(1);
				}
				basebackupHaveKeepCount = true;
				break;
			}

			case 'a':
			{
				if (!ws_parse_retention_age(optarg, &basebackupKeepAge))
				{
					/* error already logged */
					exit(1);
				}
				basebackupHaveKeepAge = true;
				break;
			}

			case 'n':
			{
				basebackupDryRun = true;
				break;
			}

			case 'f':
			{
				basebackupForce = true;
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
 * cli_basebackup_command_run runs "pg_walserver basebackup" against the
 * options cli_basebackup_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_basebackup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	WsUpstreamTarget target = { 0 };

	if (!cli_resolve_upstream(basebackupPgdata, basebackupConfigFile,
							  basebackupRoute,
							  basebackupPath, basebackupUpstream,
							  basebackupHost, basebackupPort,
							  basebackupUser, &target))
	{
		exit(1);
	}

	if (!cli_basebackup_run(&target, NULL, 0))
	{
		/* errors have already been logged; never attempt retention against
		 * a failed/partial backup attempt */
		exit(1);
	}

	/* --keep-count/--keep-age are optional: with neither given, basebackup
	 * behaves exactly as it always has (just takes the backup). When
	 * either is given, run the exact same retention-and-cleanup logic
	 * "pg_walserver archive-cleanup" itself uses (count/age union math,
	 * the always-protect-".latest" rule, and its WAL-continuity pre-flight
	 * safety check) against the route we just backed up. A cleanup refusal
	 * (e.g. the continuity check finds a problem and --force wasn't given)
	 * only logs an error here -- it must never undo or unreport the base
	 * backup that was just taken and kept: a cron job wired to this command
	 * should always end up with one more good backup on disk, even on a
	 * run where its own retention pass couldn't safely prune anything. */
	if (basebackupHaveKeepCount || basebackupHaveKeepAge)
	{
		if (!ws_archive_cleanup_run(target.path,
									basebackupHaveKeepCount, basebackupKeepCount,
									basebackupHaveKeepAge, basebackupKeepAge,
									basebackupDryRun, basebackupForce))
		{
			log_error("basebackup: the new base backup succeeded and has "
					  "been kept, but the retention cleanup pass that "
					  "followed it did not complete -- see the error(s) "
					  "logged above");
		}
	}

	exit(0);
}


static CommandLine basebackup_command =
	make_command("basebackup",
				 "Take a base backup of a route's upstream",
				 "--cluster <name> --pgdata <path> [--config <path>] "
				 "| --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--keep-count <N>] [--keep-age <interval>] "
				 "[--dry-run] [--force]",
				 "  --pgdata      this instance's own data root (defaults "
				 "to PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "                <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster     the cluster name to back up (looked up in "
				 "the config file)\n"
				 "  --path        the route's own directory (overrides the "
				 "route's own \"path\")\n"
				 "  --upstream    a libpq connection string to connect with "
				 "(overrides the\n"
				 "                route's own \"upstream\")\n"
				 "  --host / --port / --user  further override individual "
				 "connection\n"
				 "                parameters (default port: 5432, default "
				 "user: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
													  "  --keep-count  after taking the backup, also run "
													  "archive-cleanup's own\n"
													  "                retention pass, keeping at least this "
													  "many of the most\n"
													  "                recent base backups (optional; with "
													  "neither --keep-count\n"
													  "                nor --keep-age, no cleanup is attempted)\n"
													  "  --keep-age    ... keeping every base backup taken "
													  "within this long\n"
													  "                (\"72h\"/\"30d\"/\"4w\"/\"3m\"); the more "
													  "conservative of\n"
													  "                --keep-count/--keep-age wins when both "
													  "are given\n"
													  "  --dry-run     with --keep-count/--keep-age, report what "
													  "the cleanup\n"
													  "                pass would remove without removing "
													  "anything (the backup\n"
													  "                itself is always taken for real)\n"
													  "  --force       with --keep-count/--keep-age, bypass the "
													  "cleanup pass's\n"
													  "                WAL-continuity refusal (same meaning as "
													  "archive-cleanup's\n"
													  "                own --force); never bypasses the backup "
													  "itself, and never\n"
													  "                turns a cleanup refusal into a lost "
													  "backup\n",
				 cli_basebackup_getopt, cli_basebackup_command_run);


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

	exit(cli_setup_run(&setupOptions) ? 0 : 1);
}


static CommandLine setup_command =
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


/* -----------------------------------------------------------------------
 * pg_walserver cluster register <name> | drop <name> | list | set-upstream <name>
 * ----------------------------------------------------------------------- */

/*
 * The pidfile/SIGHUP reload logic that used to be a private static
 * function right here now lives in cli_cluster.c as ws_cluster_reload_
 * running_server() (cli_cluster.h), shared with commands there that need
 * to reload a running server at a point other than "after the whole
 * command finished" (ws_cluster_drop_run()'s own --purge path, in
 * particular).
 */


static WsClusterRegisterOptions clusterRegisterOptions = { 0 };

static struct option clusterRegisterLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "path", required_argument, NULL, 'P' },
	{ "pguri", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "receivewal", required_argument, NULL, 'c' },
	{ "no-receivewal", no_argument, NULL, 'N' },
	{ "force", no_argument, NULL, 'F' },
	{ "ssl-self-signed", no_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_register_getopt parses "pg_walserver cluster register"'s
 * own flags (everything except the cluster name itself, a positional
 * argument left in argv for cli_cluster_register_command_run() below)
 * into the file-scope statics above.
 */
static int
cli_cluster_register_getopt(int argc, char **argv)
{
	optind = 0;
	clusterRegisterOptions = (WsClusterRegisterOptions) {
		0
	};
	ws_prefill_pgdata_from_env(clusterRegisterOptions.pgdata);

	/*
	 * The embedded receivewal worker is on by default now: running
	 * "cluster register" with no receivewal-related flag at all writes
	 * "receivewal = pull" (see write_route_section(), cli_cluster.c).
	 * --receivewal none / --no-receivewal are the explicit opt-out for a
	 * push-only (archive_command-only) route; --receivewal pull still
	 * works too, a no-op given this default.
	 */
	clusterRegisterOptions.receivewalPull = true;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:P:u:h:p:U:n:c:NFs",
							clusterRegisterLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterRegisterOptions.pgdata, optarg,
						sizeof(clusterRegisterOptions.pgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterRegisterOptions.configFile, optarg,
						sizeof(clusterRegisterOptions.configFile));
				break;
			}

			case 'P':
			{
				strlcpy(clusterRegisterOptions.path, optarg,
						sizeof(clusterRegisterOptions.path));
				break;
			}

			case 'u':
			{
				strlcpy(clusterRegisterOptions.pguri, optarg,
						sizeof(clusterRegisterOptions.pguri));
				break;
			}

			case 'h':
			{
				strlcpy(clusterRegisterOptions.host, optarg,
						sizeof(clusterRegisterOptions.host));
				break;
			}

			case 'p':
			{
				strlcpy(clusterRegisterOptions.port, optarg,
						sizeof(clusterRegisterOptions.port));
				break;
			}

			case 'U':
			{
				strlcpy(clusterRegisterOptions.user, optarg,
						sizeof(clusterRegisterOptions.user));
				break;
			}

			case 'n':
			{
				strlcpy(clusterRegisterOptions.hostname, optarg,
						sizeof(clusterRegisterOptions.hostname));
				break;
			}

			case 'c':
			{
				if (streq(optarg, "pull"))
				{
					clusterRegisterOptions.receivewalPull = true;
				}
				else if (streq(optarg, "none"))
				{
					clusterRegisterOptions.receivewalPull = false;
				}
				else
				{
					log_fatal("Invalid --receivewal value \"%s\": recognized "
							  "values are \"pull\" (the default) and "
							  "\"none\"", optarg);
					exit(1);
				}
				break;
			}

			case 'N':
			{
				clusterRegisterOptions.receivewalPull = false;
				break;
			}

			case 'F':
			{
				clusterRegisterOptions.force = true;
				break;
			}

			case 's':
			{
				clusterRegisterOptions.sslSelfSigned = true;
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
 * cli_cluster_register_command_run reads the one positional argument
 * "cluster register" takes -- the cluster's own name, left in argv once
 * cli_cluster_register_getopt() has consumed every flag -- then runs
 * ws_cluster_register_run() against it and the options that getopt call
 * parsed above, and exit()s with its own result.
 */
static void
cli_cluster_register_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster register requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster register "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterRegisterOptions.cluster, argv[0],
			sizeof(clusterRegisterOptions.cluster));

	if (!ws_cluster_register_run(&clusterRegisterOptions))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterRegisterOptions.pgdata);

	exit(0);
}


static CommandLine cluster_register_command =
	make_command("register",
				 "Register (or validate) one cluster this pg_walserver "
				 "archives",
				 "<name> --pgdata <path> [--config <path>] "
				 "[--path <dir>] "
				 "[--pguri <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--hostname <fqdn>] "
				 "[--receivewal pull|none | --no-receivewal] "
				 "[--ssl-self-signed] [--force]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives, "
				 "independent of\n"
				 "              --pgdata (defaults to "
				 "<pgdata>/pg_walserver.ini, or\n"
				 "              PG_WALSERVER_CONFIG_FILE)\n"
				 "  --path      the route's own directory, created if "
				 "missing; defaults\n"
				 "              to <pgdata>/<name>\n"
				 "  --pguri     a libpq connection string, written into the "
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
													  "              added, if none exists yet (or right away "
													  "with\n"
													  "              --ssl-self-signed, below)\n"
													  "  --receivewal pull  write \"receivewal = pull\" into the "
													  "route's own section\n"
													  "              (the default now, even with no --receivewal "
													  "flag at all):\n"
													  "              the next \"pg_walserver serve\" forks a "
													  "supervised child\n"
													  "              running the embedded pg_receivewal "
													  "worker against\n"
													  "              this route's own \"upstream\" (receivewal.c)"
													  " -- see README.md's\n"
													  "              \"The embedded receivewal worker\" section\n"
													  "  --receivewal none / --no-receivewal  opt this route out of "
													  "the embedded\n"
													  "              receivewal worker (push-only, archive_command-only)"
													  "\n"
													  "  --ssl-self-signed  create a self-signed certificate "
													  "for --pgdata right\n"
													  "              away, whether or not this is the only "
													  "route -- skips a\n"
													  "              separate \"pg_walserver create-cert\" call "
													  "entirely; an\n"
													  "              already-existing certificate is left "
													  "untouched\n"
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
													  "if it doesn't have one yet -- \"cluster register\" never "
													  "takes one itself.\n",
				 cli_cluster_register_getopt, cli_cluster_register_command_run);


static char clusterDropPgdata[MAXPGPATH] = { 0 };
static char clusterDropConfigFile[MAXPGPATH] = { 0 };
static char clusterDropRoute[NAMEDATALEN + 16] = { 0 };
static bool clusterDropPurge = false;

static struct option clusterDropLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "purge", no_argument, NULL, 'P' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_drop_getopt parses "pg_walserver cluster drop"'s own flags
 * (everything except the cluster name itself, a positional argument left
 * in argv for cli_cluster_drop_command_run() below) into the file-scope
 * statics above.
 */
static int
cli_cluster_drop_getopt(int argc, char **argv)
{
	optind = 0;
	clusterDropPgdata[0] = '\0';
	clusterDropConfigFile[0] = '\0';
	clusterDropRoute[0] = '\0';
	clusterDropPurge = false;
	ws_prefill_pgdata_from_env(clusterDropPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:P", clusterDropLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterDropPgdata, optarg, sizeof(clusterDropPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterDropConfigFile, optarg,
						sizeof(clusterDropConfigFile));
				break;
			}

			case 'P':
			{
				clusterDropPurge = true;
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
 * cli_cluster_drop_command_run reads the one positional argument
 * "cluster drop" takes -- the cluster's own name -- then runs ws_
 * cluster_drop_run() against it and the options cli_cluster_drop_getopt
 * parsed above, and exit()s with its own result.
 */
static void
cli_cluster_drop_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster drop requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster drop "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterDropRoute, argv[0], sizeof(clusterDropRoute));

	if (!ws_cluster_drop_run(clusterDropPgdata, clusterDropConfigFile,
							 clusterDropRoute, clusterDropPurge))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterDropPgdata);

	exit(0);
}


static CommandLine cluster_drop_command =
	make_command("drop",
				 "Drop (disable) one cluster, or fully remove it with "
				 "--purge",
				 "<name> --pgdata <path> [--config <path>] [--purge]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --purge     also remove the registration and the "
				 "route's own on-disk\n"
				 "              data (every base backup and WAL segment "
				 "it holds) -- without\n"
				 "              it, the route is only marked disabled: "
				 "its embedded\n"
				 "              receivewal worker is stopped, it refuses "
				 "every connection\n"
				 "              and command (basebackup, fetch-systemid, "
				 "set-upstream,\n"
				 "              CHECK_FILE/ARCHIVE_FILE/archive-wal/"
				 "restore-wal), and its\n"
				 "              own data is left in place -- see \"cluster "
				 "list --disabled\"\n"
				 "              to find it again, \"cluster enable\" to "
				 "bring it back, or\n"
				 "              \"cluster prune\" to remove every dropped "
				 "cluster at once\n",
				 cli_cluster_drop_getopt, cli_cluster_drop_command_run);


static char clusterEnablePgdata[MAXPGPATH] = { 0 };
static char clusterEnableConfigFile[MAXPGPATH] = { 0 };
static char clusterEnableRoute[NAMEDATALEN + 16] = { 0 };

static struct option clusterEnableLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_enable_getopt parses "pg_walserver cluster enable"'s own
 * flags (everything except the cluster name itself, a positional
 * argument left in argv for cli_cluster_enable_command_run() below) into
 * the file-scope statics above.
 */
static int
cli_cluster_enable_getopt(int argc, char **argv)
{
	optind = 0;
	clusterEnablePgdata[0] = '\0';
	clusterEnableConfigFile[0] = '\0';
	clusterEnableRoute[0] = '\0';
	ws_prefill_pgdata_from_env(clusterEnablePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", clusterEnableLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterEnablePgdata, optarg,
						sizeof(clusterEnablePgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterEnableConfigFile, optarg,
						sizeof(clusterEnableConfigFile));
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
 * cli_cluster_enable_command_run reads the one positional argument
 * "cluster enable" takes -- the cluster's own name -- then runs ws_
 * cluster_enable_run() against it and the options cli_cluster_enable_
 * getopt parsed above, and exit()s with its own result.
 */
static void
cli_cluster_enable_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster enable requires exactly one argument: the "
				  "cluster's own name (\"pg_walserver cluster enable "
				  "<name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterEnableRoute, argv[0], sizeof(clusterEnableRoute));

	if (!ws_cluster_enable_run(clusterEnablePgdata, clusterEnableConfigFile,
							   clusterEnableRoute))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterEnablePgdata);

	exit(0);
}


static CommandLine cluster_enable_command =
	make_command("enable",
				 "Bring a dropped (disabled) cluster back",
				 "<name> --pgdata <path> [--config <path>]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "\n"
				 "The symmetric counterpart to \"cluster drop\" (without "
				 "--purge): clears\n"
				 "the route's own \"disabled\" property, nothing else -- "
				 "its own \"path\"/\n"
				 "\"upstream\"/\"hostname\" are already on file, so, "
				 "unlike re-running\n"
				 "\"cluster register\" to the same end, no connection "
				 "URI needs to be\n"
				 "re-supplied. Reloads an already-running \"pg_walserver "
				 "serve\" for the\n"
				 "same --pgdata immediately afterward, so its embedded "
				 "receivewal worker\n"
				 "(if \"receivewal = pull\") starts again right away. A "
				 "cluster that was\n"
				 "already active is a safe no-op.\n",
				 cli_cluster_enable_getopt, cli_cluster_enable_command_run);


static char clusterListPgdata[MAXPGPATH] = { 0 };
static char clusterListConfigFile[MAXPGPATH] = { 0 };
static bool clusterListShowUpstream = false;
static bool clusterListShowDisabled = false;

static struct option clusterListLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "upstream", no_argument, NULL, 'u' },
	{ "disabled", no_argument, NULL, 'x' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_list_getopt parses "pg_walserver cluster list"'s own flags
 * into the file-scope statics above.
 */
static int
cli_cluster_list_getopt(int argc, char **argv)
{
	optind = 0;
	clusterListPgdata[0] = '\0';
	clusterListConfigFile[0] = '\0';
	clusterListShowUpstream = false;
	clusterListShowDisabled = false;
	ws_prefill_pgdata_from_env(clusterListPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:ux", clusterListLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterListPgdata, optarg, sizeof(clusterListPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterListConfigFile, optarg,
						sizeof(clusterListConfigFile));
				break;
			}

			case 'u':
			{
				clusterListShowUpstream = true;
				break;
			}

			case 'x':
			{
				clusterListShowDisabled = true;
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
 * cli_cluster_list_command_run runs "pg_walserver cluster list" against
 * the options cli_cluster_list_getopt parsed above, then exit()s with
 * its own result.
 */
static void
cli_cluster_list_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_cluster_list_run(clusterListPgdata, clusterListConfigFile,
							 clusterListShowUpstream,
							 clusterListShowDisabled) ? 0 : 1);
}


static CommandLine cluster_list_command =
	make_command("list",
				 "List every cluster this pg_walserver has registered",
				 "[--pgdata <path> | --config <path>] [--upstream] "
				 "[--disabled]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives; either "
				 "this or\n"
				 "              --pgdata is enough (defaults to "
				 "<pgdata>/pg_walserver.ini,\n"
				 "              or PG_WALSERVER_CONFIG_FILE)\n"
				 "  --upstream  also print each cluster's own upstream "
				 "connection\n"
				 "              string, pivoted into one block per "
				 "cluster instead of\n"
				 "              a table column (skipped by default -- "
				 "these are often\n"
				 "              too wide for a readable row)\n"
				 "  --disabled  list dropped (disabled) clusters instead "
				 "of active ones\n"
				 "              -- \"cluster drop\" (without --purge) "
				 "marks a cluster\n"
				 "              this way rather than removing it; see "
				 "\"cluster prune\"\n"
				 "              to remove every one of them at once\n",
				 cli_cluster_list_getopt, cli_cluster_list_command_run);


static char clusterPrunePgdata[MAXPGPATH] = { 0 };
static char clusterPruneConfigFile[MAXPGPATH] = { 0 };

static struct option clusterPruneLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_prune_getopt parses "pg_walserver cluster prune"'s own
 * flags into the file-scope statics above.
 */
static int
cli_cluster_prune_getopt(int argc, char **argv)
{
	optind = 0;
	clusterPrunePgdata[0] = '\0';
	clusterPruneConfigFile[0] = '\0';
	ws_prefill_pgdata_from_env(clusterPrunePgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", clusterPruneLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterPrunePgdata, optarg, sizeof(clusterPrunePgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterPruneConfigFile, optarg,
						sizeof(clusterPruneConfigFile));
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
 * cli_cluster_prune_command_run runs "pg_walserver cluster prune" against
 * the options cli_cluster_prune_getopt parsed above, then exit()s with
 * its own result.
 */
static void
cli_cluster_prune_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_cluster_prune_run(clusterPrunePgdata,
							  clusterPruneConfigFile) ? 0 : 1);
}


static CommandLine cluster_prune_command =
	make_command("prune",
				 "Remove every dropped (disabled) cluster's registration "
				 "and on-disk data",
				 "[--pgdata <path> | --config <path>]",
				 "  --pgdata    this instance's own data root. Either "
				 "this or --config\n"
				 "              is enough\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "\n"
				 "The bulk equivalent of \"cluster drop --purge <name>\" "
				 "run once per\n"
				 "cluster \"cluster list --disabled\" shows -- every "
				 "dropped cluster's own\n"
				 "registration and on-disk data (every base backup and "
				 "WAL segment it\n"
				 "holds) is removed. Never touches an active cluster.\n",
				 cli_cluster_prune_getopt, cli_cluster_prune_command_run);


static char clusterSetUpstreamPgdata[MAXPGPATH] = { 0 };
static char clusterSetUpstreamConfigFile[MAXPGPATH] = { 0 };
static char clusterSetUpstreamRoute[NAMEDATALEN + 16] = { 0 };
static char clusterSetUpstreamUpstream[MAXCONNINFO] = { 0 };
static bool clusterSetUpstreamForceBasebackup = false;

static struct option clusterSetUpstreamLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "pguri", required_argument, NULL, 'u' },
	{ "force-basebackup", no_argument, NULL, 'F' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_cluster_set_upstream_getopt parses "pg_walserver cluster
 * set-upstream"'s own flags (everything except the cluster name itself,
 * a positional argument left in argv for cli_cluster_set_upstream_
 * command_run() below) into the file-scope statics above.
 */
static int
cli_cluster_set_upstream_getopt(int argc, char **argv)
{
	optind = 0;
	clusterSetUpstreamPgdata[0] = '\0';
	clusterSetUpstreamConfigFile[0] = '\0';
	clusterSetUpstreamRoute[0] = '\0';
	clusterSetUpstreamUpstream[0] = '\0';
	clusterSetUpstreamForceBasebackup = false;
	ws_prefill_pgdata_from_env(clusterSetUpstreamPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:f:u:F",
							clusterSetUpstreamLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(clusterSetUpstreamPgdata, optarg,
						sizeof(clusterSetUpstreamPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(clusterSetUpstreamConfigFile, optarg,
						sizeof(clusterSetUpstreamConfigFile));
				break;
			}

			case 'u':
			{
				strlcpy(clusterSetUpstreamUpstream, optarg,
						sizeof(clusterSetUpstreamUpstream));
				break;
			}

			case 'F':
			{
				clusterSetUpstreamForceBasebackup = true;
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
 * cli_cluster_set_upstream_command_run reads the one positional argument
 * "cluster set-upstream" takes -- the cluster's own name -- then runs
 * ws_cluster_set_upstream_run() against it and the options cli_cluster_
 * set_upstream_getopt parsed above, and exit()s with its own result.
 */
static void
cli_cluster_set_upstream_command_run(int argc, char **argv)
{
	if (argc != 1)
	{
		log_fatal("cluster set-upstream requires exactly one argument: "
				  "the cluster's own name (\"pg_walserver cluster "
				  "set-upstream <name> ...\")");
		commandline_print_usage(&ws_root, stderr);
		exit(1);
	}

	strlcpy(clusterSetUpstreamRoute, argv[0], sizeof(clusterSetUpstreamRoute));

	if (!ws_cluster_set_upstream_run(clusterSetUpstreamPgdata,
									 clusterSetUpstreamConfigFile,
									 clusterSetUpstreamRoute,
									 clusterSetUpstreamUpstream,
									 clusterSetUpstreamForceBasebackup))
	{
		exit(1);
	}

	ws_cluster_reload_running_server(clusterSetUpstreamPgdata);

	exit(0);
}


static CommandLine cluster_set_upstream_command =
	make_command("set-upstream",
				 "Point an already-registered cluster at a new upstream "
				 "(e.g. after a failover)",
				 "<name> --pgdata <path> [--config <path>] "
				 "--pguri <conninfo> [--force-basebackup]",
				 "  <name>      the cluster's own name, given positionally "
				 "(never a flag)\n"
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --pguri     the new libpq connection string, replacing "
				 "the route's\n"
				 "              own \"upstream\" property\n"
				 "  --force-basebackup  also take a fresh base backup "
				 "against the new\n"
				 "              upstream right away, rather than waiting "
				 "for the next\n"
				 "              scheduled \"pg_walserver basebackup\"\n"
				 "\n"
				 "Reloads an already-running \"pg_walserver serve\" for "
				 "this --pgdata, if\n"
				 "one is running: its own reconciliation already detects "
				 "the \"upstream\"\n"
				 "change and restarts this route's embedded receivewal "
				 "worker against\n"
				 "the new one -- no separate step needed to \"move\" it.\n",
				 cli_cluster_set_upstream_getopt,
				 cli_cluster_set_upstream_command_run);


static CommandLine *cluster_subcommands[] = {
	&cluster_register_command,
	&cluster_drop_command,
	&cluster_enable_command,
	&cluster_list_command,
	&cluster_set_upstream_command,
	&cluster_prune_command,
	NULL
};

static CommandLine cluster_commands =
	make_command_set("cluster",
					 "Register, drop, enable, list, re-point, or prune "
					 "the clusters this pg_walserver archives",
					 NULL, NULL, NULL, cluster_subcommands);

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

/*
 * cli_create_cert_getopt parses "pg_walserver create-cert"'s own flags into the
 * file-scope statics above.
 */
static int
cli_create_cert_getopt(int argc, char **argv)
{
	optind = 0;
	createCertForce = false;
	ws_prefill_pgdata_from_env(createCertPgdata);

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


/*
 * cli_create_cert_command_run runs "pg_walserver create-cert" against the
 * options cli_create_cert_getopt parsed above, then exit()s with its own
 * result.
 */
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

/*
 * cli_archive_getopt parses "pg_walserver archive-wal"'s own flags into the
 * file-scope statics above.
 */
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

/*
 * cli_reload_getopt parses "pg_walserver reload"'s own flags into the
 * file-scope statics above.
 */
static int
cli_reload_getopt(int argc, char **argv)
{
	optind = 0;
	reloadPgdata[0] = '\0';
	ws_prefill_pgdata_from_env(reloadPgdata);

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
static char archiveCleanupConfigFile[MAXPGPATH] = { 0 };
static char archiveCleanupRoute[NAMEDATALEN + 16] = { 0 };
static char archiveCleanupPath[MAXPGPATH] = { 0 };
static bool archiveCleanupHaveKeepCount = false;
static int archiveCleanupKeepCount = 0;
static bool archiveCleanupHaveKeepAge = false;
static WsRetentionAge archiveCleanupKeepAge = { 0 };
static bool archiveCleanupDryRun = false;
static bool archiveCleanupForce = false;

static struct option archiveCleanupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'F' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "path", required_argument, NULL, 'P' },
	{ "keep-count", required_argument, NULL, 'k' },
	{ "keep-age", required_argument, NULL, 'a' },
	{ "dry-run", no_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_archive_cleanup_getopt parses "pg_walserver archive-cleanup"'s own flags into the
 * file-scope statics above.
 */
static int
cli_archive_cleanup_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(archiveCleanupPgdata);
	archiveCleanupConfigFile[0] = '\0';
	archiveCleanupRoute[0] = '\0';
	archiveCleanupPath[0] = '\0';
	archiveCleanupHaveKeepCount = false;
	archiveCleanupKeepCount = 0;
	archiveCleanupHaveKeepAge = false;
	archiveCleanupKeepAge = (WsRetentionAge) {
		0
	};
	archiveCleanupDryRun = false;
	archiveCleanupForce = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:F:c:P:k:a:nf",
							archiveCleanupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(archiveCleanupPgdata, optarg, sizeof(archiveCleanupPgdata));
				break;
			}

			case 'F':
			{
				strlcpy(archiveCleanupConfigFile, optarg,
						sizeof(archiveCleanupConfigFile));
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

			case 'f':
			{
				archiveCleanupForce = true;
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
 * cli_archive_cleanup_command_run runs "pg_walserver archive-cleanup" against
 * the options cli_archive_cleanup_getopt parsed above, then exit()s with its
 * own result.
 */
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

		config_file_path(archiveCleanupPgdata, archiveCleanupConfigFile,
						 routesPath, sizeof(routesPath));

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
								archiveCleanupDryRun, archiveCleanupForce) ? 0 : 1);
}


static CommandLine archive_cleanup_command =
	make_command("archive-cleanup",
				 "Remove WAL/base backups this route no longer needs to "
				 "keep (operator/cron-driven, never automatic)",
				 "--cluster <name> --pgdata <path> [--config <path>] "
				 "| --path <dir> "
				 "[--keep-count <N>] [--keep-age <interval>] [--dry-run] "
				 "[--force]",
				 "  --pgdata      this instance's own data root (defaults "
				 "to PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "                <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster     the cluster name to clean up (looked up "
				 "in the config file)\n"
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
				 "removing anything -- still runs\n"
				 "                and reports the WAL-continuity check "
				 "below, pass or fail\n"
				 "  --force, -f   before deleting anything, a pre-flight "
				 "check refuses the whole\n"
				 "                operation if any kept backup would be "
				 "left with a WAL gap, or\n"
				 "                if removing a backup would leave a "
				 "time range with no gap-free\n"
				 "                newer backup to cover it; --force "
				 "bypasses that refusal only (it\n"
				 "                does not change what --keep-count/"
				 "--keep-age decide to remove) --\n"
				 "                a default, unattended cron job should "
				 "NEVER blindly pass this;\n"
				 "                only use it once you've independently "
				 "verified proceeding is\n"
				 "                safe (e.g. an independent backup, or "
				 "an accepted/expected gap)\n",
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
 * pg_walserver stop --pgdata <path>
 * ----------------------------------------------------------------------- */

static char stopPgdata[MAXPGPATH] = { 0 };

static struct option stopLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_stop_getopt parses "pg_walserver stop"'s own flags into the
 * file-scope statics above.
 */
static int
cli_stop_getopt(int argc, char **argv)
{
	optind = 0;
	stopPgdata[0] = '\0';
	ws_prefill_pgdata_from_env(stopPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", stopLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(stopPgdata, optarg, sizeof(stopPgdata));
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
 * cli_stop_run sends SIGTERM to the "pg_walserver serve" instance whose
 * pid is recorded in <pgdata>/pg_walserver.pid -- the exact same shape as
 * "pg_ctl stop" (or, in this project's own vocabulary, "reload"'s own
 * cli_reload_run() just above, SIGTERM instead of SIGHUP): accept_loop.c's
 * own signal handler treats SIGTERM as a clean shutdown request (stop
 * accepting new connections, let in-flight ones finish, then exit), the
 * same disposition every other supervised process in this project already
 * gives it. Does not wait for the process to actually exit -- the signal
 * was delivered, that is this command's whole job, the same as "pg_ctl
 * stop -m fast" without "--wait" would be.
 */
static void
cli_stop_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (stopPgdata[0] == '\0')
	{
		log_fatal("stop requires --pgdata (or the PGDATA environment "
				  "variable)");
		exit(1);
	}

	char pidfilePath[MAXPGPATH];

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", stopPgdata);

	pid_t pid = 0;

	if (!read_pidfile(pidfilePath, &pid))
	{
		log_fatal("Failed to stop pg_walserver: no running instance found "
				  "at \"%s\" (missing, stale, or unreadable pidfile)",
				  pidfilePath);
		exit(1);
	}

	if (kill(pid, SIGTERM) != 0)
	{
		if (errno == ESRCH)
		{
			log_fatal("Failed to stop pg_walserver: pid %d (from \"%s\") "
					  "is not running", pid, pidfilePath);
		}
		else
		{
			log_fatal("Failed to send SIGTERM to pg_walserver pid %d: %m", pid);
		}
		exit(1);
	}

	log_info("Sent SIGTERM to pg_walserver pid %d", pid);
	exit(0);
}


static CommandLine stop_command =
	make_command("stop",
				 "Stop a running pg_walserver cleanly",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA); sends SIGTERM to the pid recorded "
				 "in\n"
				 "              \"<pgdata>/pg_walserver.pid\"\n",
				 cli_stop_getopt, cli_stop_run);


/* -----------------------------------------------------------------------
 * pg_walserver ps --pgdata <path>
 * ----------------------------------------------------------------------- */

static char psPgdata[MAXPGPATH] = { 0 };

static struct option psLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_ps_getopt parses "pg_walserver ps"'s own flags into the
 * file-scope statics above.
 */
static int
cli_ps_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(psPgdata);

	int c;

	while ((c = getopt_long(argc, argv, "D:", psLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(psPgdata, optarg, sizeof(psPgdata));
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
 * cli_ps_command_run runs "pg_walserver ps" against the options cli_ps_getopt
 * parsed above, then exit()s with its own result.
 */
static void
cli_ps_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_ps_run(psPgdata) ? 0 : 1);
}


static CommandLine ps_command =
	make_command("ps",
				 "Show pg_walserver serve's own process-level status "
				 "(pid, receivewal workers, bootstrap jobs)",
				 "--pgdata <path>",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n",
				 cli_ps_getopt, cli_ps_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver ls --pgdata <path>
 * ----------------------------------------------------------------------- */

static char lsPgdata[MAXPGPATH] = { 0 };
static char lsConfigFile[MAXPGPATH] = { 0 };
static bool lsIncludeConfigFiles = false;

static struct option lsLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "all", no_argument, NULL, 'a' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_ls_getopt parses "pg_walserver ls"'s own flags into the
 * file-scope statics above.
 */
static int
cli_ls_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(lsPgdata);
	lsConfigFile[0] = '\0';
	lsIncludeConfigFiles = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:a", lsLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(lsPgdata, optarg, sizeof(lsPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(lsConfigFile, optarg, sizeof(lsConfigFile));
				break;
			}

			case 'a':
			{
				lsIncludeConfigFiles = true;
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
 * cli_ls_command_run runs "pg_walserver ls" against the options cli_ls_getopt
 * parsed above, then exit()s with its own result.
 */
static void
cli_ls_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_ls_run(lsPgdata, lsConfigFile, lsIncludeConfigFiles) ? 0 : 1);
}


static CommandLine ls_command =
	make_command("ls",
				 "Per-cluster storage summary: base backups, WAL, disk usage",
				 "--pgdata <path> [--config <path>] [--all]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n"
				 "  --config    where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --all, -a   list the config/credential/certificate "
				 "files instead\n"
				 "              (rarely change, rarely interesting day "
				 "to day)\n",
				 cli_ls_getopt, cli_ls_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver status --pgdata <path>
 * ----------------------------------------------------------------------- */

static char statusPgdata[MAXPGPATH] = { 0 };
static char statusConfigFile[MAXPGPATH] = { 0 };

static struct option statusLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_status_getopt parses "pg_walserver status"'s own flags into the
 * file-scope statics above.
 */
static int
cli_status_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(statusPgdata);
	statusConfigFile[0] = '\0';

	int c;

	while ((c = getopt_long(argc, argv, "D:f:", statusLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(statusPgdata, optarg, sizeof(statusPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(statusConfigFile, optarg, sizeof(statusConfigFile));
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
 * cli_status_command_run runs "pg_walserver status" against the options
 * cli_status_getopt parsed above, then exit()s with its own result.
 */
static void
cli_status_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_status_run(statusPgdata, statusConfigFile) ? 0 : 1);
}


static CommandLine status_command =
	make_command("status",
				 "Show a short pg_walserver status dashboard",
				 "--pgdata <path> [--config <path>]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n",
				 cli_status_getopt, cli_status_command_run);


/* -----------------------------------------------------------------------
 * pg_walserver list clusters|backups|wal [--cluster <name>] [--segments]
 * ----------------------------------------------------------------------- */

static char listPgdata[MAXPGPATH] = { 0 };
static char listConfigFile[MAXPGPATH] = { 0 };
static char listCluster[NAMEDATALEN + 16] = { 0 };
static bool listWalSegments = false;

static struct option listClustersLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "cluster", required_argument, NULL, 'c' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_list_clusters_getopt parses "pg_walserver list clusters"'s own flags into the
 * file-scope statics above.
 */
static int
cli_list_clusters_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(listPgdata);
	listConfigFile[0] = '\0';
	listCluster[0] = '\0';

	int c;

	while ((c = getopt_long(argc, argv, "D:f:c:", listClustersLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(listPgdata, optarg, sizeof(listPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(listConfigFile, optarg, sizeof(listConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(listCluster, optarg, sizeof(listCluster));
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
 * cli_list_clusters_command_run runs "pg_walserver list clusters" against the
 * options cli_list_clusters_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_list_clusters_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_clusters_run(listPgdata, listConfigFile, listCluster) ? 0 : 1);
}


static CommandLine list_clusters_command =
	make_command("clusters",
				 "List every route, its backup/receivewal status, and the "
				 "WAL range it covers",
				 "--pgdata <path> [--config <path>] [--cluster <name>]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single route\n",
				 cli_list_clusters_getopt, cli_list_clusters_command_run);


/*
 * cli_list_backups_command_run runs "pg_walserver list backups" against the
 * options cli_list_backups_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_list_backups_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_backups_run(listPgdata, listConfigFile, listCluster) ? 0 : 1);
}


static CommandLine list_backups_command =
	make_command("backups",
				 "List base backups per cluster (label, size, which is "
				 ".latest)",
				 "--pgdata <path> [--config <path>] [--cluster <name>]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single route\n",
				 cli_list_clusters_getopt, cli_list_backups_command_run);


static struct option listWalLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "segments", no_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_list_wal_getopt parses "pg_walserver list wal"'s own flags into the
 * file-scope statics above.
 */
static int
cli_list_wal_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(listPgdata);
	listConfigFile[0] = '\0';
	listCluster[0] = '\0';
	listWalSegments = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:c:s", listWalLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(listPgdata, optarg, sizeof(listPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(listConfigFile, optarg, sizeof(listConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(listCluster, optarg, sizeof(listCluster));
				break;
			}

			case 's':
			{
				listWalSegments = true;
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
 * cli_list_wal_command_run runs "pg_walserver list wal" against the options
 * cli_list_wal_getopt parsed above, then exit()s with its own result.
 */
static void
cli_list_wal_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_wal_run(listPgdata, listConfigFile, listCluster,
						  listWalSegments) ? 0 : 1);
}


static CommandLine list_wal_command =
	make_command("wal",
				 "List WAL cache aggregate stats per cluster, or every "
				 "file with --segments",
				 "--pgdata <path> [--config <path>] [--cluster <name>] "
				 "[--segments]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single route\n"
				 "  --segments  list every individual WAL/.history/.backup "
				 "file instead\n"
				 "              of the default aggregate stats\n",
				 cli_list_wal_getopt, cli_list_wal_command_run);


static CommandLine *list_subcommands[] = {
	&list_clusters_command,
	&list_backups_command,
	&list_wal_command,
	NULL
};

static CommandLine list_commands =
	make_command_set("list",
					 "List clusters, base backups, or WAL cache contents",
					 NULL, NULL, NULL, list_subcommands);


/* -----------------------------------------------------------------------
 * pg_walserver help
 * ----------------------------------------------------------------------- */

/*
 * cli_help_run prints the whole sub-command tree at once, the exact same
 * "pg_autoctl help" facility (commandline_print_command_tree(),
 * src/bin/lib/subcommands.c/commandline.h) already provides.
 */
static void
cli_help_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	commandline_print_command_tree(&ws_root, stdout);
}


static CommandLine help_command =
	make_command("help", "Print this whole sub-command tree at once", "",
				 "", NULL, cli_help_run);


/* -----------------------------------------------------------------------
 * Root command table
 * ----------------------------------------------------------------------- */

static CommandLine *root_subcommands[] = {
	&serve_command,
	&scram_secret_command,
	&fetch_systemid_command,
	&basebackup_command,
	&setup_command,
	&cluster_commands,
	&create_cert_command,
	&archive_command,
	&restore_command,
	&archive_cleanup_command,
	&reload_command,
	&stop_command,
	&ps_command,
	&ls_command,
	&status_command,
	&list_commands,
	&help_command,
	&internal_commands,
	NULL
};

CommandLine ws_root =
	make_command_set("pg_walserver",
					 "The archiver's own replication-protocol server",
					 "serve ... | scram-secret ... | setup ... | "
					 "cluster ... | fetch-systemid ... | basebackup ... | "
					 "create-cert ... | archive-wal ... | restore-wal ... | "
					 "archive-cleanup ... | reload ... | stop ... | ps ... | "
					 "ls ... | status ... | list ... | help",
					 NULL, NULL, root_subcommands);
