/*
 * src/bin/pg_walserver/cli_basebackup.c
 *   See cli_basebackup.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <getopt.h>
#include <string.h>
#include <time.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "cli_archive_cleanup.h"
#include "cli_basebackup.h"
#include "cli_common.h"
#include "cli_root.h"
#include "defaults.h"
#include "env_utils.h"
#include "file_utils.h"
#include "log.h"
#include "pgctl.h"
#include "string_utils.h"

#define WS_BASEBACKUP_LATEST_FILENAME "basebackups/.latest"
#define WS_PGVERSION_FILENAME "pg_walserver_pgversion"

/* local helpers */
static bool find_pg_basebackup_for_route(const WsUpstreamTarget *target,
										 char *pgBasebackupPathOut, size_t size);

static int cli_basebackup_getopt(int argc, char **argv);
static void cli_basebackup_command_run(int argc, char **argv);


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
static RetentionAge basebackupKeepAge = { 0 };
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

CommandLine basebackup_command =
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


/*
 * find_pg_basebackup_for_route picks the pg_basebackup binary to use for
 * target: when target->path has a "pg_walserver_pgversion" file (written
 * by `pg_walserver fetch-systemid`, see cli_fetch_systemid.c), uses
 * find_pg_basebackup_for_major_version() to pick one that is at least that
 * major version -- pg_basebackup's own compatibility rule is "same or
 * older major version [as the server]" only, so an older client against a
 * newer upstream is not safe to use.
 *
 * A route with no recorded version yet (created before this file existed,
 * or fetch-systemid was never run against it) falls back to the old blind
 * search_path_first() behaviour, with a clear warning: never a hard
 * failure just for missing this file, backward compatibility with
 * existing routes matters more here.
 */
static bool
find_pg_basebackup_for_route(const WsUpstreamTarget *target,
							 char *pgBasebackupPathOut, size_t size)
{
	char pgversionPath[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long fileSize = 0L;

	sformat(pgversionPath, sizeof(pgversionPath), "%s/" WS_PGVERSION_FILENAME,
			target->path);

	int version = 0;

	if (read_file_if_exists(pgversionPath, &contents, &fileSize) &&
		contents != NULL && fileSize > 0 && stringToInt(contents, &version))
	{
		free(contents);

		int targetMajor = version / 10000;

		return find_pg_basebackup_for_major_version(targetMajor,
													pgBasebackupPathOut,
													size);
	}

	if (contents != NULL)
	{
		free(contents);
	}

	log_warn("\"%s\" has no recorded upstream Postgres version "
			 "(\"%s\" not found or unreadable) -- picking whatever "
			 "pg_basebackup happens to be first in PATH, which may not be "
			 "version-safe against this upstream; run \"pg_walserver "
			 "fetch-systemid\" against this route to record its upstream "
			 "version and fix this",
			 target->path, pgversionPath);

	return search_path_first("pg_basebackup", pgBasebackupPathOut, LOG_ERROR);
}


/*
 * cli_basebackup_run takes a real pg_basebackup of target's upstream into a
 * freshly created "<target->path>/basebackups/<label>" directory (label:
 * a UTC timestamp, matching service_archiver_basebackup.c's own scheme so
 * both the standalone and pgaf-integrated backups sit side by side without
 * a naming collision), validates the result (backup_label and PG_VERSION
 * both present -- pg_basebackup itself already guarantees a well-formed
 * backup_label on a zero exit, this is a defense against a partial result
 * rather than a re-parse of it), and only then atomically swaps
 * "<target->path>/basebackups/.latest" to the new label. Never touches
 * .latest on failure: a route always keeps serving its previous,
 * known-good backup until a new one actually completes.
 *
 * Returns true on success (labelOut, when not NULL, receives the new
 * backup's own label), false with an error already logged otherwise.
 */
bool
cli_basebackup_run(const WsUpstreamTarget *target,
				   char *labelOut, size_t labelOutSize)
{
	char pgBasebackupPath[MAXPGPATH] = { 0 };

	if (!find_pg_basebackup_for_route(target, pgBasebackupPath,
									  sizeof(pgBasebackupPath)))
	{
		/* errors have already been logged */
		return false;
	}

	/* "basebackup-<UTC timestamp>", the same scheme
	 * service_archiver_basebackup.c uses, so both sit in the same
	 * basebackups/ directory without colliding */
	time_t now = time(NULL);
	struct tm nowUTC = { 0 };

	gmtime_r(&now, &nowUTC);

	char label[NAMEDATALEN] = { 0 };

	strftime(label, sizeof(label), "basebackup-%Y%m%dT%H%M%SZ", &nowUTC);

	char backupDir[MAXPGPATH] = { 0 };

	sformat(backupDir, sizeof(backupDir), "%s/basebackups/%s",
			target->path, label);

	log_info("Taking a base backup of %s:%d into \"%s\"",
			 target->node.host, target->node.port, backupDir);

	ReplicationSource replicationSource = { 0 };

	replicationSource.primaryNode = target->node;
	strlcpy(replicationSource.userName, target->userName,
			sizeof(replicationSource.userName));
	strlcpy(replicationSource.applicationName, "pg_walserver_basebackup",
			sizeof(replicationSource.applicationName));
	strlcpy(replicationSource.backupDir, backupDir,
			sizeof(replicationSource.backupDir));
	strlcpy(replicationSource.walMethod, "stream",
			sizeof(replicationSource.walMethod));
	strlcpy(replicationSource.label, label, sizeof(replicationSource.label));
	replicationSource.sslOptions = target->sslOptions;

	if (env_exists("PGPASSWORD"))
	{
		(void) get_env_copy("PGPASSWORD", replicationSource.password,
							sizeof(replicationSource.password));
	}

	/* pg_basebackup_fetch() only needs pg_ctl's own path to find
	 * pg_basebackup next to it (path_in_same_directory()); pg_walserver has
	 * no pg_ctl of its own to point at, so the resolved pg_basebackup path
	 * itself works exactly as well: same directory, same result. */
	if (!pg_basebackup_fetch(pgBasebackupPath, &replicationSource))
	{
		/* errors have already been logged */
		return false;
	}

	/* pg_basebackup itself already guarantees a well-formed backup_label
	 * on a zero exit; this is a defense against a partial/corrupt result
	 * making it this far, not a re-parse of what pg_basebackup wrote */
	char backupLabelPath[MAXPGPATH] = { 0 };
	char pgVersionPath[MAXPGPATH] = { 0 };

	sformat(backupLabelPath, sizeof(backupLabelPath), "%s/backup_label", backupDir);
	sformat(pgVersionPath, sizeof(pgVersionPath), "%s/PG_VERSION", backupDir);

	if (!file_exists(backupLabelPath) || !file_exists(pgVersionPath))
	{
		log_error("pg_basebackup reported success but \"%s\" is missing "
				  "\"backup_label\" or \"PG_VERSION\" -- not marking it "
				  "as the latest backup", backupDir);
		return false;
	}

	char latestPath[MAXPGPATH] = { 0 };

	sformat(latestPath, sizeof(latestPath), "%s/" WS_BASEBACKUP_LATEST_FILENAME,
			target->path);

	if (!write_file_atomic(label, strlen(label), latestPath))
	{
		log_error("Base backup \"%s\" completed but failed to update \"%s\"",
				  label, latestPath);
		return false;
	}

	log_info("Base backup \"%s\" is now the latest for \"%s\"",
			 label, target->path);

	if (labelOut != NULL)
	{
		strlcpy(labelOut, label, labelOutSize);
	}

	return true;
}


/*
 * cli_basebackup_route_has_backup returns true when
 * "<path>/basebackups/.latest" exists and is non-empty -- the same "does
 * this route already have a usable base backup" check cmd_base_backup.c's
 * own read_latest_basebackup_label() effectively makes (it additionally
 * validates the label's own character set, not needed for this plain
 * existence check). Used by accept_loop.c's own ws_bootstrap_missing_
 * backups() to decide which routes "pg_walserver serve" needs to take an
 * automatic bootstrap backup for.
 */
bool
cli_basebackup_route_has_backup(const char *path)
{
	char latestPath[MAXPGPATH] = { 0 };

	sformat(latestPath, sizeof(latestPath), "%s/" WS_BASEBACKUP_LATEST_FILENAME,
			path);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(latestPath, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool nonEmpty = fileSize > 0 && contents[0] != '\0';

	free(contents);

	return nonEmpty;
}


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
	basebackupKeepAge = (RetentionAge) {
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
				if (!stringToRetentionAge(optarg, &basebackupKeepAge))
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
