/*
 * src/bin/pg_walsender/main.c
 *   Entry point for pg_walsender. Two modes, dispatched on argv[1]:
 *
 *     pg_walsender --port <port> [--routes <path>]
 *       Runs the accept loop (see accept_loop.h). Exec'd by pg_autoctl's
 *       `archiver serve` supervisor (service_archiver_serve.c), but fully
 *       runnable and testable on its own against real psql/pg_basebackup/
 *       pg_receivewal.
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
#include "fetch_client.h"
#include "file_utils.h"
#include "log.h"

/*
 * Globals required by shared common/ sources (file_utils.c's
 * init_ps_buffer/set_ps_title in particular) -- pg_walsender owns these
 * stub definitions itself, exactly like pgaftest's main.c does, since it
 * doesn't link pg_autoctl's own main.c.
 */
char pg_autoctl_argv0[MAXPGPATH] = "pg_walsender";
char pg_autoctl_program[MAXPGPATH] = "pg_walsender";
int pgconnect_timeout = 2;
char *ps_buffer;
size_t ps_buffer_size;
size_t last_status_len;
Semaphore log_semaphore = { 0 };


static void
usage(const char *argv0)
{
	fprintf(stderr,
			"Usage: %s --port <port> [--routes <path>]\n"
			"       %s fetch-file --host <h> --port <p> --route <fmtn>/<grp> "
			"--filename <name> --output <path>\n\n"
			"  --port      port to listen on (server mode default: %d)\n"
			"  --routes    path to the routes INI file mapping "
			"\"<formation>/<group>\" to\n"
			"              { walcache, basebackup, allowed_hosts } -- "
			"omit only for manual\n"
			"              standalone testing (accepts any dbname, no "
			"host restriction)\n"
			"  fetch-file  one-shot FETCH_FILE client, for use as a "
			"restore_command\n",
			argv0, argv0, WS_DEFAULT_PORT);
}


static int
main_fetch_file(int argc, char **argv)
{
	char host[256] = { 0 };
	int port = WS_DEFAULT_PORT;
	char route[256] = { 0 };
	char filename[256] = { 0 };
	char output[MAXPGPATH] = { 0 };

	static struct option longOptions[] = {
		{ "host", required_argument, NULL, 'H' },
		{ "port", required_argument, NULL, 'p' },
		{ "route", required_argument, NULL, 'r' },
		{ "filename", required_argument, NULL, 'f' },
		{ "output", required_argument, NULL, 'o' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ((c = getopt_long(argc, argv, "H:p:r:f:o:", longOptions, NULL)) != -1)
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
				port = atoi(optarg);
				break;
			}

			case 'r':
			{
				strlcpy(route, optarg, sizeof(route));
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
		fprintf(stderr, "fetch-file: --host, --route, --filename, and "
						"--output are all required\n");
		usage(argv[0]);
		return 1;
	}

	return ws_fetch_file_client(host, port, route, filename, output);
}


int
main(int argc, char **argv)
{
	strlcpy(pg_autoctl_program, argv[0], sizeof(pg_autoctl_program));
	init_ps_buffer(argc, argv);

	log_set_level(LOG_INFO);

	if (argc >= 2 && strcmp(argv[1], "fetch-file") == 0)
	{
		/* shift argv so getopt_long in main_fetch_file() skips "fetch-file" */
		return main_fetch_file(argc - 1, argv + 1);
	}

	WsServerConfig config = { 0 };

	config.port = WS_DEFAULT_PORT;

	static struct option longOptions[] = {
		{ "port", required_argument, NULL, 'p' },
		{ "routes", required_argument, NULL, 'r' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int c;

	while ((c = getopt_long(argc, argv, "p:r:h", longOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'p':
			{
				config.port = atoi(optarg);
				break;
			}

			case 'r':
			{
				strlcpy(config.routesPath, optarg, sizeof(config.routesPath));
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

	if (!ws_accept_loop(&config))
	{
		return 1;
	}

	return 0;
}
