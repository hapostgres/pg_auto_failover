/*
 * src/bin/pg_walserver/cli_archive.c
 *   See cli_archive.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_archive.h"
#include "cli_root.h"
#include "defaults.h"
#include "log.h"
#include "push_client.h"

static int cli_archive_getopt(int argc, char **argv);
static void cli_archive_command_run(int argc, char **argv);


/*
 * pg_walserver archive <path-to-file> <filename>
 *                       --cluster <name> --host <host> [--port <port>]
 *                       [--user <name>] [--sslmode <mode>]
 */

static WsWalServerTarget archiveTarget = { 0 };

CommandLine archive_command =
	make_command("archive-wal",
				 "Push one WAL/.backup file into a pg_walserver cluster "
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


/*
 * ws_archive_push_file connects to target (pg_walserver itself) and pushes
 * localPath into it as filename via push_client.c's own
 * ws_push_file_client(), which does the CHECK_FILE/ARCHIVE_FILE round
 * trip. Returns true on success, false with an error already logged
 * (by ws_push_file_client() itself) on any failure, matching PostgreSQL's
 * own archive_command contract of "nonzero means retry me".
 */
bool
ws_archive_push_file(const WsWalServerTarget *target, const char *localPath,
					 const char *filename)
{
	return ws_push_file_client(target->host, target->port, target->user,
							   target->cluster, target->sslmode,
							   "pg_walserver_archive_wal",
							   localPath, filename) == 0;
}


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
 * src/bin/lib/subcommands.c/commandline.c), then runs ws_archive_push_
 * file()'s whole sequence. Exit code matches PostgreSQL's own
 * archive_command contract exactly: 0 on success (including "already
 * there"), 1 on any failure, so PostgreSQL retries.
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

	if (archiveTarget.cluster[0] == '\0' || archiveTarget.host[0] == '\0')
	{
		log_fatal("archive-wal requires --cluster and --host");
		exit(1);
	}

	exit(ws_archive_push_file(&archiveTarget, argv[0], argv[1]) ? 0 : 1);
}
