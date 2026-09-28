/*
 * src/bin/pg_walserver/main.c
 *   Entry point for pg_walserver, dispatching to the sub-commands defined
 *   in cli_root.c's CommandLine tree (serve, scram-secret) via this
 *   project's own command-line framework
 *   (src/bin/lib/subcommands.c/commandline.h), the same way pgaftest's own
 *   main.c does for a similarly-sized standalone binary.
 *
 *   No sub-command is ever implicit: `pg_walserver` with no arguments, or
 *   an unrecognized first argument, prints usage and exits non-zero, the
 *   same as any other sub-command-dispatching command in this project
 *   (`pg_autoctl` itself, `pgaftest`). Run the accept loop with
 *   `pg_walserver serve [options]` explicitly -- see cli_root.c's own
 *   header comment for the full sub-command list and every flag each one
 *   takes.
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
	log_set_level(LOG_INFO);

	/*
	 * receivewal.c's own supervised receivewal worker children fork()+execv() this
	 * same binary (see cli_internal.c's own header comment for why) --
	 * they need an absolute, exec()-able path, not whatever relative/bare
	 * argv[0] the shell happened to invoke us with (e.g. a bare
	 * "pg_walserver" found via $PATH). Falls back to argv[0] verbatim if
	 * the absolute path can't be resolved (matches pg_autoctl's own
	 * main.c); a route with "receivewal = pull" then fails to start its own
	 * receivewal worker with a clear "execv() failed" error rather than silently
	 * misbehaving.
	 */
	if (!set_program_absolute_path(pg_autoctl_program, sizeof(pg_autoctl_program)))
	{
		strlcpy(pg_autoctl_program, argv[0], sizeof(pg_autoctl_program));
	}
	init_ps_buffer(argc, argv);

	if (!commandline_run(&ws_root, argc, argv))
	{
		return 1;
	}

	return 0;
}
