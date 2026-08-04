/*
 * src/bin/pg_autoctl/service_archiver_basebackup.c
 *   Archiving & Disaster Recovery: base backup generation, `live` source
 *   only (Milestone 5's own first half, per the Build order in
 *   ~/dev/temp/archiving-disaster-recovery.md: "live first, then
 *   replay/volatile").
 *
 * Trigger scope for this pass: bootstrap only -- a group with zero
 * existing base backups gets one immediately, sourced live. Scheduled/
 * timeline-change/retention-driven triggers all need basebackup_policy
 * wired through the CLI first (a later milestone); get_archiver_policy()
 * already resolves a default policy row today, but nothing here reads its
 * frequency yet.
 *
 * Target selection follows the design doc's own `live` precedence, minus
 * its warm-standby tier (a later milestone, nothing to select from yet):
 * the first healthy secondary in the group, falling back to the primary
 * when none exists. "Healthy" here just means "reachable via
 * pgautofailover.get_nodes()", not "least-loaded" -- picking between
 * several healthy secondaries by load is a refinement, not required for
 * base-backup generation to work at all.
 *
 * Base backup generation itself is a one-shot forked child (tracked via
 * basebackupPid, the same pattern service_archiver.c uses for
 * pgReceivewalPid), not a persistent supervised service: it runs to
 * completion and exits, so it must not block service_archiver_loop()'s own
 * per-tick node_active()/WAL-report cycle for however long pg_basebackup
 * takes.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <errno.h>
#include <ftw.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "service_archiver_basebackup.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "signals.h"

/*
 * One base backup generation child at a time, mirroring
 * service_archiver.c's own pgReceivewalPid tracking pattern.
 */
static pid_t basebackupPid = -1;

/* accumulator for directory_size()'s nftw() callback -- nftw() has no
 * user-data parameter, so this has to be file-scope */
static uint64_t directorySizeAccumulator = 0;


static bool
basebackup_child_is_running(void)
{
	if (basebackupPid <= 0)
	{
		return false;
	}

	int status = 0;
	pid_t ret = waitpid(basebackupPid, &status, WNOHANG);

	if (ret == 0)
	{
		/* still running */
		return true;
	}

	if (ret == basebackupPid)
	{
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		{
			log_warn("Base backup generation process (pid %d) exited "
					 "with status %d", basebackupPid, status);
		}
	}
	else
	{
		log_warn("Failed to check on base backup generation process "
				 "(pid %d): %m", basebackupPid);
	}

	basebackupPid = -1;
	return false;
}


/*
 * select_basebackup_source picks the `live` target: the first healthy
 * secondary in the group, falling back to the primary when none exists.
 * Rows with port == 0 are ARCHIVING memberships (this node's own row among
 * them, per the port == 0 sentinel documented in pgautofailover.sql) --
 * never a valid pg_basebackup source, so they are skipped outright.
 */
static bool
select_basebackup_source(Keeper *keeper, NodeAddress *source)
{
	NodeAddressArray nodeArray = { 0 };

	if (!monitor_get_nodes(&(keeper->monitor),
						   keeper->config.formation,
						   keeper->config.groupId,
						   &nodeArray))
	{
		/* errors already logged */
		return false;
	}

	NodeAddress *primary = NULL;

	for (int i = 0; i < nodeArray.count; i++)
	{
		NodeAddress *node = &(nodeArray.nodes[i]);

		if (node->port == 0)
		{
			continue;
		}

		if (node->isPrimary)
		{
			primary = node;
			continue;
		}

		*source = *node;
		return true;
	}

	if (primary != NULL)
	{
		*source = *primary;
		return true;
	}

	return false;
}


/*
 * read_basebackup_label extracts "START WAL LOCATION" and "START TIMELINE"
 * from a just-completed pg_basebackup's own backup_label file -- the
 * authoritative start position, matching pg_walsender/cmd_base_backup.c's
 * own read_backup_label() (duplicated rather than shared: pg_autoctl
 * doesn't link that standalone binary's code, see this project's Makefile
 * split).
 */
static bool
read_basebackup_label(const char *backupDir, char *lsnOut, size_t lsnOutSize,
					  int *timelineOut)
{
	char path[MAXPGPATH];

	sformat(path, sizeof(path), "%s/backup_label", backupDir);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool foundLsn = false;
	bool foundTimeline = false;
	char *line = contents;

	while (line != NULL && *line != '\0')
	{
		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		const char *lsnPrefix = "START WAL LOCATION: ";
		const char *tliPrefix = "START TIMELINE: ";

		if (strncmp(line, lsnPrefix, strlen(lsnPrefix)) == 0)
		{
			const char *value = line + strlen(lsnPrefix);
			const char *end = value;

			while (*end && !isspace((unsigned char) *end))
			{
				end++;
			}

			size_t len = Min((size_t) (end - value), lsnOutSize - 1);

			memcpy(lsnOut, value, len);
			lsnOut[len] = '\0';
			foundLsn = true;
		}
		else if (strncmp(line, tliPrefix, strlen(tliPrefix)) == 0)
		{
			*timelineOut = atoi(line + strlen(tliPrefix));
			foundTimeline = true;
		}

		line = (nl != NULL) ? nl + 1 : NULL;
	}

	free(contents);

	return foundLsn && foundTimeline;
}


/*
 * query_wal_position runs a single ad hoc query against sourceConnInfo,
 * used right after a base backup finishes to capture the source's current
 * WAL write position (primary) or replay position (standby) -- recorded as
 * the backup's endlsn. Not the exact internal stop-backup LSN real
 * pg_basebackup computes server-side (not observable from a plain CLI
 * wrapper around it), but a reasonable upper bound: "WAL up to at least
 * this point must be replayed to reach consistency."
 */
static bool
query_wal_position(const char *sourceConnInfo, bool isPrimary,
				   char *lsn, size_t lsnSize)
{
	PGSQL client = { 0 };

	if (!pgsql_init(&client, (char *) sourceConnInfo, PGSQL_CONN_UPSTREAM))
	{
		return false;
	}

	const char *sql = isPrimary
		? "SELECT pg_current_wal_lsn()::text"
		: "SELECT pg_last_wal_replay_lsn()::text";
	SingleValueResultContext context = { { 0 }, PGSQL_RESULT_STRING, false };

	bool result = pgsql_execute_with_params(&client, sql, 0, NULL, NULL,
											&context, &parseSingleValueResult);

	PQfinish(client.connection);

	if (!result || !context.parsedOk || context.strVal == NULL)
	{
		return false;
	}

	strlcpy(lsn, context.strVal, lsnSize);
	free(context.strVal);

	return true;
}


static int
accumulate_file_size(const char *path, const struct stat *sb,
					 int typeflag, struct FTW *ftwbuf)
{
	if (typeflag == FTW_F)
	{
		directorySizeAccumulator += (uint64_t) sb->st_size;
	}

	return 0;
}


/*
 * directory_size adds up the apparent size of every regular file under
 * dirPath. Best effort: sizebytes is informational only (nothing in the
 * monitor schema's own logic -- prune_archiver_wal() included -- reads it
 * back), so a failure here is not worth failing an otherwise-successful
 * base backup over.
 */
static uint64_t
directory_size(const char *dirPath)
{
	directorySizeAccumulator = 0;

	(void) nftw(dirPath, accumulate_file_size, 16, FTW_PHYS);

	return directorySizeAccumulator;
}


/*
 * run_pg_basebackup execs the real, unmodified pg_basebackup client
 * against source, writing into backupDir. --wal-method=none: this backup
 * is deliberately not self-consistent on its own -- the archiver's already
 * -running WAL capture (service_archiver.c) is what supplies the WAL
 * needed to reach consistency on replay, so bundling a second independent
 * copy of it into every single backup would be pure waste.
 */
static bool
run_pg_basebackup(KeeperConfig *config, NodeAddress *source,
				  const char *backupDir, const char *label)
{
	char pgBasebackupPath[MAXPGPATH] = { 0 };

	path_in_same_directory(config->pgSetup.pg_ctl, "pg_basebackup",
						   pgBasebackupPath);

	if (!file_exists(pgBasebackupPath))
	{
		log_error("Failed to find pg_basebackup at \"%s\"", pgBasebackupPath);
		return false;
	}

	log_info("Generating a live base backup from %s:%d into \"%s\"",
			 source->host, source->port, backupDir);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork pg_basebackup: %m");
		return false;
	}

	if (pid == 0)
	{
		char portStr[NAMEDATALEN];

		sformat(portStr, sizeof(portStr), "%d", source->port);

		char *args[16];
		int argsIndex = 0;

		args[argsIndex++] = pgBasebackupPath;
		args[argsIndex++] = "-h";
		args[argsIndex++] = source->host;
		args[argsIndex++] = "-p";
		args[argsIndex++] = portStr;
		args[argsIndex++] = "-U";
		args[argsIndex++] = PG_AUTOCTL_REPLICA_USERNAME;
		args[argsIndex++] = "-D";
		args[argsIndex++] = (char *) backupDir;
		args[argsIndex++] = "--format=plain";
		args[argsIndex++] = "--wal-method=none";
		args[argsIndex++] = "--checkpoint=fast";
		args[argsIndex++] = "--label";
		args[argsIndex++] = (char *) label;
		args[argsIndex++] = "--no-password";
		args[argsIndex] = NULL;

		execv(pgBasebackupPath, args);

		/* execv only returns on failure */
		log_fatal("execv(\"%s\"): %m", pgBasebackupPath);
		_exit(127);
	}

	int status = 0;

	if (waitpid(pid, &status, 0) == -1)
	{
		log_error("Failed to wait for pg_basebackup (pid %d): %m", pid);
		return false;
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		log_error("pg_basebackup failed while generating base backup \"%s\"",
				 label);
		return false;
	}

	return true;
}


/*
 * generate_basebackup is the forked child's own body: run pg_basebackup to
 * completion, then report start and completion to the monitor from the
 * authoritative backup_label it wrote. Runs in its own process, with its
 * own monitor connection (the parent's keeper->monitor is not fork-safe to
 * share, exactly as service_archiver_run.c's own supervised children
 * already document).
 */
static bool
generate_basebackup(Keeper *keeper, NodeAddress *source,
					const char *backupDir, const char *label)
{
	KeeperConfig *config = &(keeper->config);

	if (!run_pg_basebackup(config, source, backupDir, label))
	{
		return false;
	}

	char startLsn[PG_LSN_MAXLENGTH] = { 0 };
	int timeline = 1;

	if (!read_basebackup_label(backupDir, startLsn, sizeof(startLsn),
							   &timeline))
	{
		log_error("Failed to read backup_label from \"%s\" after "
				 "pg_basebackup completed", backupDir);
		return false;
	}

	if (!monitor_init(&(keeper->monitor), config->monitor_pguri))
	{
		log_error("Failed to contact the monitor to report base backup "
				 "\"%s\"", label);
		return false;
	}

	int64_t basebackupId = 0;

	if (!monitor_report_basebackup_started(&(keeper->monitor),
										   config->archiverId,
										   config->formation,
										   config->groupId,
										   label, timeline, startLsn,
										   &basebackupId))
	{
		/* errors already logged */
		return false;
	}

	/*
	 * dbname is otherwise unknown here -- an ARCHIVING node has no real
	 * PostgresSetup of its own to read one from (haspgdata's own design
	 * comment). DEFAULT_DATABASE_NAME ("postgres") is what every ordinary
	 * node defaults its own --dbname to (cli_create_node.c), and is always
	 * present regardless of that default, so it is a safe target for a
	 * plain read-only SQL query.
	 */
	char sourceConnInfo[MAXCONNINFO] = { 0 };

	sformat(sourceConnInfo, sizeof(sourceConnInfo),
			"host=%s port=%d user=%s dbname=%s application_name=%s",
			source->host, source->port,
			PG_AUTOCTL_REPLICA_USERNAME, DEFAULT_DATABASE_NAME, config->name);

	char endLsn[PG_LSN_MAXLENGTH] = { 0 };

	if (!query_wal_position(sourceConnInfo, source->isPrimary,
							endLsn, sizeof(endLsn)))
	{
		/* not fatal: the backup itself succeeded, only this one piece of
		 * informational metadata is missing -- fall back to the start
		 * position rather than failing an otherwise-successful backup */
		strlcpy(endLsn, startLsn, sizeof(endLsn));
	}

	uint64_t sizeBytes = directory_size(backupDir);

	return monitor_report_basebackup_completed(&(keeper->monitor),
											   basebackupId, endLsn,
											   (int64_t) sizeBytes,
											   backupDir);
}


/*
 * service_archiver_maybe_generate_basebackup checks, once per
 * service_archiver_loop() tick, whether this group has zero existing base
 * backups yet and -- if so, and no generation is already in flight -- forks
 * a child to produce one. See this file's own header comment for the full
 * scope of this first pass (bootstrap trigger, live source only).
 */
bool
service_archiver_maybe_generate_basebackup(Keeper *keeper)
{
	if (basebackup_child_is_running())
	{
		return true;
	}

	KeeperConfig *config = &(keeper->config);
	bool found = false;
	char storageLocation[MAXPGPATH] = { 0 };

	if (!monitor_get_latest_basebackup_location(&(keeper->monitor),
												config->formation,
												config->groupId,
												storageLocation,
												sizeof(storageLocation),
												&found))
	{
		/* errors already logged */
		return false;
	}

	if (found)
	{
		/* bootstrap already satisfied; scheduled/retention-driven triggers
		 * are a follow-up milestone, see this file's own header comment */
		return true;
	}

	NodeAddress source = { 0 };

	if (!select_basebackup_source(keeper, &source))
	{
		log_warn("No eligible node to source a live base backup from yet, "
				 "will retry");
		return true;
	}

	char backupsDir[MAXPGPATH] = { 0 };

	sformat(backupsDir, sizeof(backupsDir), "%s/basebackups",
			config->pgSetup.pgdata);

	if (!directory_exists(backupsDir) && mkdir(backupsDir, 0700) != 0)
	{
		log_error("Failed to create \"%s\": %m", backupsDir);
		return false;
	}

	time_t now = time(NULL);
	struct tm nowUTC = { 0 };

	gmtime_r(&now, &nowUTC);

	char label[NAMEDATALEN] = { 0 };

	strftime(label, sizeof(label), "basebackup-%Y%m%dT%H%M%SZ", &nowUTC);

	char backupDir[MAXPGPATH] = { 0 };

	sformat(backupDir, sizeof(backupDir), "%s/%s", backupsDir, label);

	fflush(stdout);
	fflush(stderr);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork the base backup generation process: %m");
		return false;
	}

	if (pid == 0)
	{
		(void) set_signal_handlers(false);
		(void) set_ps_title("pg_autoctl: archiver basebackup");

		bool ok = generate_basebackup(keeper, &source, backupDir, label);

		exit(ok ? EXIT_CODE_QUIT : EXIT_CODE_INTERNAL_ERROR);
	}

	log_debug("pg_autoctl archiver basebackup process started in "
			 "subprocess %d", pid);
	basebackupPid = pid;

	return true;
}
