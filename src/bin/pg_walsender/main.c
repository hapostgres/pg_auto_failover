/*
 * src/bin/pg_walsender/main.c
 *   Entry point for pg_walsender. Two modes, dispatched on argv[1]:
 *
 *     pg_walsender --port <port> [--pgdata <path>]
 *       Runs the accept loop (see accept_loop.h). Exec'd by pg_autoctl's
 *       `archiver serve` supervisor (service_archiver_serve.c), but fully
 *       runnable and testable on its own against real psql/pg_basebackup/
 *       pg_receivewal. --pgdata (or the PGDATA environment variable) names
 *       the archiver's own top-level storage root -- the same value given
 *       as --pgdata to `pg_autoctl create archiver` -- from which the
 *       routes file's own path is derived directly (<pgdata>/archiver-
 *       routes.ini, written by service_archiver_reconciler.c): a single,
 *       trivially-derivable value, unlike pg_autoctl's own XDG-based
 *       config-file path, which lives outside PGDATA entirely and
 *       pg_walsender has no way to recompute on its own (see routes.h's
 *       own header comment for the full rationale).
 *
 *     pg_walsender fetch-file --host <h> --port <p> --route <formation>/
 *                  <group> --filename <name> --output <path>
 *       Runs the FETCH_FILE client (fetch_client.h) once and exits --
 *       pg_autoctl's restore_command shells out to this, the same way it
 *       already shells out to real pg_receivewal/pg_basebackup elsewhere
 *       in this project.
 *
 * Standalone binary (see the Makefile's own header comment) -- links
 * neither of these modes against pg_autoctl's own sources.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "lock_utils.h"

#include "accept_loop.h"
#include "defaults.h"
#include "env_utils.h"
#include "fetch_client.h"
#include "file_utils.h"
#include "log.h"
#include "hba.h"
#include "scram.h"
#include "string_utils.h"
#include "tls.h"

/*
 * Globals required by shared common/ sources (file_utils.c's
 * init_ps_buffer/set_ps_title in particular) -- pg_walsender owns these
 * stub definitions itself, exactly like pgaftest's main.c does, since it
 * doesn't link pg_autoctl's own main.c.
 */
char pg_autoctl_argv0[MAXPGPATH] = "pg_walsender";
char pg_autoctl_program[MAXPGPATH] = "pg_walsender";
int pgconnect_timeout = 2;

#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))
char *ps_buffer;
size_t ps_buffer_size;
size_t last_status_len;
Semaphore log_semaphore = { 0 };


static void
usage(const char *argv0)
{
	fprintf(stderr, /* IGNORE-BANNED */
			"Usage: %s --port <port> [--pgdata <path>]\n"
			"          [--ssl-cert-file <path> --ssl-key-file <path>]\n"
			"       %s scram-secret [ --user <name> ]  (password in PGPASSWORD)\n"
			"       %s fetch-file --host <h> --port <p> --route <fmtn>/<grp> "
			"--filename <name> --output <path> [--user <role>]\n\n"
			"  --port      port to listen on (server mode default: %d)\n"
			"  --pgdata    the archiver's own top-level storage root "
			"(defaults to\n"
			"              the PGDATA environment variable); the routes "
			"file mapping\n"
			"              \"<formation>/<group>\" to { path, "
			"allowed_hosts } is read\n"
			"              from <pgdata>/archiver-routes.ini, and access is "
			"decided by\n"
			"              <pgdata>/archiver-hba.conf -- omit both only for "
			"manual\n"
			"              standalone testing (accepts any dbname, no "
			"authentication)\n"
			"  fetch-file  one-shot FETCH_FILE client, for use as a "
			"restore_command\n",
			argv0, argv0, argv0, WS_DEFAULT_PORT);
}


static int
main_fetch_file(int argc, char **argv)
{
	char host[256] = { 0 };
	int port = WS_DEFAULT_PORT;
	char route[256] = { 0 };
	char user[NAMEDATALEN] = PG_AUTOCTL_REPLICA_USERNAME;
	char filename[256] = { 0 };
	char output[MAXPGPATH] = { 0 };

	static struct option longOptions[] = {
		{ "host", required_argument, NULL, 'H' },
		{ "port", required_argument, NULL, 'p' },
		{ "route", required_argument, NULL, 'r' },
		{ "user", required_argument, NULL, 'U' },
		{ "filename", required_argument, NULL, 'f' },
		{ "output", required_argument, NULL, 'o' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ((c = getopt_long(argc, argv, "H:p:r:U:f:o:", longOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'H':
			{
				strlcpy(host, optarg, sizeof(host));
				break;
			}

			case 'p':
			{
				if (!stringToInt(optarg, &port))
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					return 1;
				}
				break;
			}

			case 'r':
			{
				strlcpy(route, optarg, sizeof(route));
				break;
			}

			case 'U':
			{
				strlcpy(user, optarg, sizeof(user));
				break;
			}

			case 'f':
			{
				strlcpy(filename, optarg, sizeof(filename));
				break;
			}

			case 'o':
			{
				strlcpy(output, optarg, sizeof(output));
				break;
			}

			default:
			{
				usage(argv[0]);
				return 1;
			}
		}
	}

	if (host[0] == '\0' || route[0] == '\0' || filename[0] == '\0' ||
		output[0] == '\0')
	{
		fprintf(stderr, /* IGNORE-BANNED */
				"fetch-file: --host, --route, --filename, and "
				"--output are all required\n");
		usage(argv[0]);
		return 1;
	}

	return ws_fetch_file_client(host, port, user, route, filename, output);
}


/*
 * main_scram_secret prints one archiver-passwd line for a user, reading the
 * password from the PGPASSWORD environment variable (never from the command
 * line, where it would show up in the process list).
 */
static int
main_scram_secret(int argc, char **argv)
{
	char user[NAMEDATALEN] = PG_AUTOCTL_REPLICA_USERNAME;
	char password[512] = { 0 };

	if (argc >= 3 && streq(argv[1], "--user"))
	{
		strlcpy(user, argv[2], sizeof(user));
	}
	else if (argc != 1)
	{
		fprintf(stderr, /* IGNORE-BANNED */
				"Usage: PGPASSWORD=... %s scram-secret [ --user <name> ]\n",
				argv[0]);
		return 1;
	}

	if (!get_env_copy("PGPASSWORD", password, sizeof(password)) ||
		password[0] == '\0')
	{
		fprintf(stderr, /* IGNORE-BANNED */
				"scram-secret: set the password in PGPASSWORD\n");
		return 1;
	}

	char secret[512];

	if (!scram_build_verifier(password, SCRAM_DEFAULT_ITERATIONS,
							  secret, sizeof(secret)))
	{
		fprintf(stderr, "scram-secret: failed to build the secret\n"); /* IGNORE-BANNED */
		return 1;
	}

	printf("%s:%s\n", user, secret); /* IGNORE-BANNED */

	return 0;
}


int
main(int argc, char **argv)
{
	strlcpy(pg_autoctl_program, argv[0], sizeof(pg_autoctl_program));
	init_ps_buffer(argc, argv);

	log_set_level(LOG_INFO);

	if (argc >= 2 && streq(argv[1], "scram-secret"))
	{
		return main_scram_secret(argc - 1, argv + 1);
	}

	if (argc >= 2 && streq(argv[1], "fetch-file"))
	{
		/* shift argv so getopt_long in main_fetch_file() skips "fetch-file" */
		return main_fetch_file(argc - 1, argv + 1);
	}

	WsServerConfig config = { 0 };

	config.port = WS_DEFAULT_PORT;

	char pgdata[MAXPGPATH] = { 0 };
	char sslCertFile[MAXPGPATH] = { 0 };
	char sslKeyFile[MAXPGPATH] = { 0 };

	(void) get_env_pgdata(pgdata);

	static struct option longOptions[] = {
		{ "port", required_argument, NULL, 'p' },
		{ "pgdata", required_argument, NULL, 'D' },
		{ "ssl-cert-file", required_argument, NULL, 'C' },
		{ "ssl-key-file", required_argument, NULL, 'K' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ((c = getopt_long(argc, argv, "p:D:h", longOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'p':
			{
				if (!stringToInt(optarg, &(config.port)))
				{
					log_fatal("Invalid --port value \"%s\"", optarg);
					return 1;
				}
				break;
			}

			case 'D':
			{
				strlcpy(pgdata, optarg, sizeof(pgdata));
				break;
			}

			case 'C':
			{
				strlcpy(sslCertFile, optarg, sizeof(sslCertFile));
				break;
			}

			case 'K':
			{
				strlcpy(sslKeyFile, optarg, sizeof(sslKeyFile));
				break;
			}

			case 'h':
			{
				usage(argv[0]);
				return 0;
			}

			default:
			{
				usage(argv[0]);
				return 1;
			}
		}
	}

	if (config.port <= 0 || config.port > 65535)
	{
		log_fatal("Invalid --port value");
		return 1;
	}

	/*
	 * pgdata left empty (neither --pgdata nor PGDATA given) is not an
	 * error: it's the manual/standalone-testing mode routes.h's own header
	 * comment describes -- config.routesPath stays empty, accept_loop.c
	 * treats that as "no routing, accept any dbname, no host restriction".
	 */
	if (pgdata[0] != '\0')
	{
		sformat(config.routesPath, sizeof(config.routesPath),
				"%s/archiver-routes.ini", pgdata);
		sformat(config.auth.hbaPath, sizeof(config.auth.hbaPath),
				"%s/archiver-hba.conf", pgdata);
		sformat(config.auth.passwdPath, sizeof(config.auth.passwdPath),
				"%s/archiver-passwd", pgdata);

		sformat(config.auth.monitorUriPath, sizeof(config.auth.monitorUriPath),
				"%s/archiver-monitor.uri", pgdata);

		/* the certificate given with --ssl-*-file, else <pgdata>/server.* */
		char certPath[MAXPGPATH], keyPath[MAXPGPATH];

		if (sslCertFile[0] != '\0' && sslKeyFile[0] != '\0')
		{
			strlcpy(certPath, sslCertFile, sizeof(certPath));
			strlcpy(keyPath, sslKeyFile, sizeof(keyPath));
		}
		else
		{
			sformat(certPath, sizeof(certPath), "%s/server.crt", pgdata);
			sformat(keyPath, sizeof(keyPath), "%s/server.key", pgdata);
		}

		if (ws_tls_server_init(certPath, keyPath))
		{
			log_info("TLS is enabled (\"%s\")", certPath);
		}
		else
		{
			log_warn("TLS is not enabled: no usable server.crt/server.key in "
					 "\"%s\"; \"hostssl\" HBA lines will not match", pgdata);
		}

		if (!hba_write_default_if_missing(config.auth.hbaPath,
										  ws_tls_server_enabled()))
		{
			log_fatal("Failed to create \"%s\"", config.auth.hbaPath);
			return 1;
		}
	}

	if (!ws_accept_loop(&config))
	{
		return 1;
	}

	return 0;
}
