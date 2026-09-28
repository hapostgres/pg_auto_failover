/*
 * src/bin/pg_walserver/cli_internal.c
 *   See cli_internal.h.
 *
 *   `pg_walserver internal service pg-receivewal --route <key> --upstream
 *   <conninfo> --path <dir>` is the subprocess entry point receivewal.c's own
 *   start_one_receivewal_child() forks and execv()s into, mirroring
 *   pg_autoctl's own "pg_autoctl internal service postgres|listener|
 *   node-active" pattern (cli_do_root.c) exactly: fork()+execv() of the
 *   *same binary*, not a bare fork() with no exec(), and not exec() of a
 *   separately-installed pg_receivewal binary. This is a deliberate choice
 *   for the same reason pg_autoctl's own header comment there gives: when
 *   a service needs restarting, execv()-ing this project's own binary
 *   from disk means a restarted receivewal worker automatically picks up whatever
 *   binary is currently installed, without the supervising process
 *   (pg_walserver's own "serve" process) needing to be replaced or
 *   restarted itself -- the same property that makes pg_autoctl "safe to
 *   use as PID 1 in Docker/Kubernetes containers where replacing the
 *   binary and sending SIGTERM would lose the container", now true of
 *   pg_walserver too. pg_receivewal_main() itself (the vendored entry
 *   point, src/bin/common/vendor/pg_receivewal/) still runs in-process
 *   inside *this* subprocess -- no third process, no second binary on
 *   $PATH required.
 *
 *   Hidden from --help (make_hidden_command_set(), matching pg_autoctl's
 *   own internal_service_commands) so an operator does not accidentally
 *   invoke it directly; routable so receivewal.c's own execv() calls work.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_internal.h"
#include "file_utils.h"
#include "log.h"
#include "pg_receivewal_entry.h"
#include "pgsql.h"
#include "string_utils.h"

static char internalPgReceivewalRoute[NAMEDATALEN + 16] = { 0 };
static char internalPgReceivewalUpstream[MAXCONNINFO] = { 0 };
static char internalPgReceivewalPath[MAXPGPATH] = { 0 };

static struct option internalPgReceivewalLongOptions[] = {
	{ "route", required_argument, NULL, 'r' },
	{ "upstream", required_argument, NULL, 'u' },
	{ "path", required_argument, NULL, 'p' },
	{ NULL, 0, NULL, 0 }
};

static int
cli_internal_pg_receivewal_getopt(int argc, char **argv)
{
	optind = 0;

	int c;

	while ((c = getopt_long(argc, argv, "r:u:p:",
							internalPgReceivewalLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'r':
			{
				strlcpy(internalPgReceivewalRoute, optarg,
						sizeof(internalPgReceivewalRoute));
				break;
			}

			case 'u':
			{
				strlcpy(internalPgReceivewalUpstream, optarg,
						sizeof(internalPgReceivewalUpstream));
				break;
			}

			case 'p':
			{
				strlcpy(internalPgReceivewalPath, optarg,
						sizeof(internalPgReceivewalPath));
				break;
			}

			default:
			{
				commandline_print_usage(&internal_commands, stderr);
				exit(1);
			}
		}
	}

	return optind;
}


/*
 * cli_internal_pg_receivewal_run calls pg_receivewal_main() in-process
 * against --upstream/--path, exactly the argv shape "pg_receivewal -w -d
 * <upstream> -D <path>" would build (see receivewal.c's own comment on why
 * no --slot/--synchronous: this is a plain, unsupervised-by-a-monitor
 * receivewal worker, not the pgaf-integrated archiver's own quorum-aware one).
 * Never returns: pg_receivewal_main() always exit()s on its own.
 */
static void
cli_internal_pg_receivewal_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	if (internalPgReceivewalUpstream[0] == '\0' ||
		internalPgReceivewalPath[0] == '\0')
	{
		log_fatal("internal service pg-receivewal requires --upstream "
				  "and --path");
		exit(1);
	}

	char *args[7];
	int argsIndex = 0;

	args[argsIndex++] = "pg_receivewal";
	args[argsIndex++] = "-w";               /* never prompt for a password */
	args[argsIndex++] = "-d";
	args[argsIndex++] = internalPgReceivewalUpstream;
	args[argsIndex++] = "-D";
	args[argsIndex++] = internalPgReceivewalPath;
	args[argsIndex] = NULL;

	char title[256];

	sformat(title, sizeof(title), "pg_walserver: receivewal %s",
			internalPgReceivewalRoute);
	set_ps_title(title);

	/*
	 * pg_receivewal_main() runs its own getopt_long() over the args[]
	 * array just built above, starting from index 1 -- but glibc's
	 * getopt_long() tracks its scan position in a single, process-global
	 * optind, which cli_internal_pg_receivewal_getopt() above already
	 * advanced past the end of *this* array while parsing pg_walserver's
	 * own --route/--upstream/--path flags. Without resetting it here,
	 * pg_receivewal_main()'s own getopt_long() call starts scanning
	 * args[] from a stale, out-of-bounds index -- undefined behavior, a
	 * crash. optind = 0 is glibc's documented way to force a full
	 * re-initialization (plain optind = 1 is not enough); the same fix
	 * this project's own service_archiver_pgreceivewal_ctl.c already
	 * applies for the identical reason, right before its own call to
	 * pg_receivewal_main().
	 */
	optind = 0;

	/*
	 * Both SIGINT and SIGTERM must make pg_receivewal stop cleanly:
	 * receivewal.c's own ws_receivewal_stop_all() sends SIGINT (matching
	 * upstream pg_receivewal's own documented clean-stop signal), and
	 * this fresh exec() otherwise starts with default dispositions for
	 * both.
	 */
	pgaf_install_stop_handlers();

	int rc = pg_receivewal_main(argsIndex, args);

	exit(rc);
}


static CommandLine service_pg_receivewal_command =
	make_command("pg-receivewal",
				 "Subprocess entry point for the embedded receivewal worker",
				 "--route <key> --upstream <conninfo> --path <dir>",
				 "  --route     the route this receivewal worker belongs to "
				 "(process title only)\n"
				 "  --upstream  a libpq connection string to receive WAL "
				 "from\n"
				 "  --path      the route's own directory to receive "
				 "into\n",
				 cli_internal_pg_receivewal_getopt,
				 cli_internal_pg_receivewal_run);


static CommandLine *internal_service_subcommands[] = {
	&service_pg_receivewal_command,
	NULL
};

static CommandLine internal_service_commands =
	make_hidden_command_set("service",
							"Subprocess entry points for receivewal.c's own "
							"supervisor",
							NULL, NULL, NULL, internal_service_subcommands);

static CommandLine *internal_subcommands[] = {
	&internal_service_commands,
	NULL
};

CommandLine internal_commands =
	make_hidden_command_set("internal",
							"Internal subprocess entry points -- not for "
							"direct use",
							NULL, NULL, NULL, internal_subcommands);
