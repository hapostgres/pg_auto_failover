/*
 * src/bin/pg_walserver/cli_root.c
 *   Top-level sub-command table for pg_walserver, built on this project's
 *   own command-line framework (src/bin/lib/subcommands.c/commandline.h),
 *   the same way pgaftest's own cli_root.c does for a similarly-sized
 *   standalone binary.
 *
 *   Five sub-commands:
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
 *     scram-secret    Print one archiver-passwd line for a user.
 *     setup           Create or validate one pg_walserver.ini route --
 *                     the write/path/upstream/role-check/systemid/
 *                     optional-basebackup wizard, cli_setup.c.
 *     fetch-systemid  Fetch a route's upstream system identifier,
 *                     cli_fetch_systemid.c.
 *     basebackup      Take a base backup of a route's upstream,
 *                     cli_basebackup.c.
 *
 *   The last three are client-side, one-shot tools (they connect *out*, to
 *   a route's own upstream), sharing cli_upstream.c's own --route/--path/
 *   --upstream/--host/--port/--user resolution. See DESIGN-standalone-
 *   archiving.md for the full design and what's deliberately not built
 *   yet (the ARCHIVE_FILE/CHECK_FILE push side, the embedded pull
 *   capturer).
 *
 *   pg_walserver has no FETCH_FILE *client* sub-command: the one-shot
 *   FETCH_FILE client (fetching a WAL segment, not a system identifier --
 *   a different thing from fetch-systemid above) lives in src/bin/common/
 *   fetch_client.c, linked in-process by whatever needs it (`pg_autoctl
 *   restore command`, in the later archiving PR) rather than exec'd as a
 *   pg_walserver sub-command. pg_walserver itself only ever answers
 *   FETCH_FILE as a server (see cmd_fetch_file.h).
 *
 *   Every flag and behavior is unchanged from the previous hand-rolled
 *   argv[1] dispatch in main.c: only the dispatch mechanism moved.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "accept_loop.h"
#include "cli_basebackup.h"
#include "cli_fetch_systemid.h"
#include "cli_setup.h"
#include "cli_upstream.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "hba.h"
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
static bool serveInsecure = false;

static struct option serveLongOptions[] = {
	{ "port", required_argument, NULL, 'p' },
	{ "pgdata", required_argument, NULL, 'D' },
	{ "ssl-cert-file", required_argument, NULL, 'C' },
	{ "ssl-key-file", required_argument, NULL, 'K' },
	{ "auth-timeout", required_argument, NULL, 'T' },
	{ "insecure", no_argument, NULL, 'I' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_serve_getopt parses every server-mode flag (--port, --pgdata,
 * --ssl-cert-file, --ssl-key-file, --auth-timeout, --insecure) into the
 * file-scope serveConfig/servePgdata/... variables that cli_serve_run() then
 * acts on. Returns optind, the number of argv slots consumed, exactly as
 * commandline_run() expects from a command_getopt callback.
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
				"%s/archiver-hba.conf", servePgdata);
		sformat(serveConfig.auth.passwdPath, sizeof(serveConfig.auth.passwdPath),
				"%s/archiver-passwd", servePgdata);

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
		WsRoute *routes = NULL;
		int routeCount = 0;

		if (routes_load(serveConfig.routesPath, &routes, &routeCount))
		{
			int namedRouteCount = 0;

			for (int i = 0; i < routeCount; i++)
			{
				if (!streq(routes[i].key, WS_ROUTES_WILDCARD_KEY))
				{
					namedRouteCount++;
				}
			}

			routes_free(routes);

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
		}
	}

	/* before any fork: every connection must see the same mock secret */
	(void) scram_mock_init();

	if (!ws_accept_loop(&serveConfig))
	{
		exit(1);
	}

	exit(0);
}


static CommandLine serve_command =
	make_command("serve",
				 "Run the pg_walserver accept loop (the default command)",
				 "[--port <port>] [--pgdata <path> | --insecure] "
				 "[--ssl-cert-file <path> --ssl-key-file <path>] "
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
				 "              decided by <pgdata>/archiver-hba.conf; the "
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
 * "user:secret" archiver-passwd line to stdout.
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
				 "Print one archiver-passwd line for a user",
				 "[--user <name>]  (password read from PGPASSWORD)",
				 "  --user      role name (default: " PG_AUTOCTL_REPLICA_USERNAME ")\n"
																				  "\n"
																				  "  The password is read from the PGPASSWORD environment "
																				  "variable, never\n"
																				  "  from the command line.\n",
				 cli_scram_secret_getopt, cli_scram_secret_run);


/* -----------------------------------------------------------------------
 * pg_walserver fetch-systemid --route <key> --pgdata <path> [--upstream ...]
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
	{ "route", required_argument, NULL, 'r' },
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

	while ((c = getopt_long(argc, argv, "D:r:P:u:h:p:U:f",
							fetchSystemidLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(fetchSystemidPgdata, optarg, sizeof(fetchSystemidPgdata));
				break;
			}

			case 'r':
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
				 "--route <key> --pgdata <path> | --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--force]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --route     the route key to fetch for (looked up in "
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
 * pg_walserver basebackup --route <key> --pgdata <path> [--upstream ...]
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
	{ "route", required_argument, NULL, 'r' },
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

	while ((c = getopt_long(argc, argv, "D:r:P:u:h:p:U:",
							basebackupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(basebackupPgdata, optarg, sizeof(basebackupPgdata));
				break;
			}

			case 'r':
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
				 "--route <key> --pgdata <path> | --path <dir> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --route     the route key to back up (looked up in "
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
 * pg_walserver setup --route <key> --path <dir> --pgdata <path>
 *                     [--upstream ...] [--force] [--with-basebackup]
 * ----------------------------------------------------------------------- */

static WsSetupOptions setupOptions = { 0 };

static struct option setupLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "route", required_argument, NULL, 'r' },
	{ "path", required_argument, NULL, 'P' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "host", required_argument, NULL, 'h' },
	{ "port", required_argument, NULL, 'p' },
	{ "user", required_argument, NULL, 'U' },
	{ "hostname", required_argument, NULL, 'n' },
	{ "force", no_argument, NULL, 'f' },
	{ "with-basebackup", no_argument, NULL, 'b' },
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

	int c;

	while ((c = getopt_long(argc, argv, "D:r:P:u:h:p:U:n:fb",
							setupLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(setupOptions.pgdata, optarg, sizeof(setupOptions.pgdata));
				break;
			}

			case 'r':
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

			case 'f':
			{
				setupOptions.force = true;
				break;
			}

			case 'b':
			{
				setupOptions.withBasebackup = true;
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
cli_setup_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_setup_run(&setupOptions) ? 0 : 1);
}


static CommandLine setup_command =
	make_command("setup",
				 "Create or validate one pg_walserver.ini route",
				 "--route <key> --path <dir> --pgdata <path> "
				 "[--upstream <conninfo> | --host <host> [--port <port>] "
				 "[--user <name>]] [--hostname <fqdn>] [--force] "
				 "[--with-basebackup]",
				 "  --pgdata    where <pgdata>/pg_walserver.ini lives "
				 "(defaults to PGDATA)\n"
				 "  --route     the route key to create or validate\n"
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
													  "DESIGN-standalone-\n"
													  "              archiving.md); creates a self-signed "
													  "certificate for\n"
													  "              --pgdata automatically, the moment a "
													  "second route is\n"
													  "              added, if none exists yet\n"
													  "  --force     change an already-existing route's path, "
													  "or overwrite an\n"
													  "              already-recorded, different system "
													  "identifier\n"
													  "  --with-basebackup  take the route's first base backup "
													  "before returning\n",
				 cli_setup_getopt, cli_setup_command_run);


/* -----------------------------------------------------------------------
 * Root command table
 * ----------------------------------------------------------------------- */

static CommandLine *root_subcommands[] = {
	&serve_command,
	&scram_secret_command,
	&fetch_systemid_command,
	&basebackup_command,
	&setup_command,
	NULL
};

CommandLine ws_root =
	make_command_set("pg_walserver",
					 "The archiver's own replication-protocol server",
					 "[serve options] | scram-secret ... | setup ... | "
					 "fetch-systemid ... | basebackup ...",
					 "  serve           Run the accept loop (default "
					 "command, used when no\n"
					 "                  sub-command name is given at all)\n"
					 "  scram-secret    Print one archiver-passwd line for "
					 "a user\n"
					 "  setup           Create or validate one "
					 "pg_walserver.ini route\n"
					 "  fetch-systemid  Fetch a route's upstream system "
					 "identifier\n"
					 "  basebackup      Take a base backup of a route's "
					 "upstream\n",
					 NULL, root_subcommands);


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
