/*
 * src/bin/pg_walserver/cli_restore_wal.c
 *   See cli_restore_wal.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_restore_wal.h"
#include "cli_root.h"
#include "defaults.h"
#include "fetch_client.h"
#include "log.h"
#include "string_utils.h"

/*
 * ws_restore_run connects to target (pg_walserver itself, never a Postgres
 * primary -- see this file's own header comment for why it doesn't reuse
 * cli_upstream.c) and fetches filename into outputPath via
 * src/bin/common/fetch_client.c's own ws_fetch_file_client(), which does
 * the actual FETCH_FILE round trip and the same-directory-temp-file-plus-
 * rename dance that keeps a killed/interrupted restore from leaving a
 * partial file at outputPath. Returns true on success, false with an
 * error already logged (by ws_fetch_file_client() itself) on any failure,
 * including the ordinary "not found" case restore_command hits at the end
 * of recovery -- this function does not try to tell that apart from any
 * other failure, exactly matching PostgreSQL's own restore_command
 * contract of "nonzero means try the next thing" either way.
 */
bool
ws_restore_run(const WsWalServerTarget *target,
			   const char *filename, const char *outputPath)
{
	return ws_fetch_file_client(target->host, target->port, target->user,
								target->route, target->sslmode,
								filename, outputPath) == 0;
}


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


CommandLine restore_command =
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
