/*
 * src/bin/pg_walserver/main.c
 *   Entry point for pg_walserver, dispatching to the sub-commands defined
 *   in cli_root.c's CommandLine tree (serve, scram-secret) via this
 *   project's own command-line framework
 *   (src/bin/lib/subcommands.c/commandline.h), the same way pgaftest's own
 *   main.c does for a similarly-sized standalone binary.
 *
 *   "serve" (the accept loop, see accept_loop.h) is the *default* command:
 *   when no sub-command name is given at all, pg_walserver_default_argv()
 *   (cli_root.c) splices "serve" into argv before commandline_run() ever
 *   sees it, so `pg_walserver --port <port> [--pgdata <path> | --insecure]
 *   ...` keeps working exactly as it did before this file existed. See
 *   cli_root.c's own header comment for the full sub-command list and every
 *   flag each one takes.
 *
 * Standalone binary (see the Makefile's own header comment) -- links
 * neither pg_autoctl's own sources nor pgaftest's.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "lock_utils.h"

#include "commandline.h"
#include "file_utils.h"
#include "log.h"

extern CommandLine ws_root;
extern char ** pg_walserver_default_argv(int argc, char **argv, int *newArgc);

/*
 * Globals required by shared common/ sources (file_utils.c's
 * init_ps_buffer/set_ps_title in particular) -- pg_walserver owns these
 * stub definitions itself, exactly like pgaftest's main.c does, since it
 * doesn't link pg_autoctl's own main.c.
 */
char pg_autoctl_argv0[MAXPGPATH] = "pg_walserver";
char pg_autoctl_program[MAXPGPATH] = "pg_walserver";
int pgconnect_timeout = 2;

char *ps_buffer;
size_t ps_buffer_size;
size_t last_status_len;
Semaphore log_semaphore = { 0 };


int
main(int argc, char **argv)
{
	strlcpy(pg_autoctl_program, argv[0], sizeof(pg_autoctl_program));
	init_ps_buffer(argc, argv);

	log_set_level(LOG_INFO);

	int dispatchArgc = argc;
	char **dispatchArgv = pg_walserver_default_argv(argc, argv, &dispatchArgc);

	if (!commandline_run(&ws_root, dispatchArgc, dispatchArgv))
	{
		return 1;
	}

	return 0;
}
