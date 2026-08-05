/*
 * src/bin/pg_autoctl/service_archiver_basebackup.c
 *   Archiving & Disaster Recovery: base backup generation, both `live` and
 *   `replay`/`volatile` sources (Milestone 5, per the Build order in
 *   ~/dev/temp/archiving-disaster-recovery.md: "live first, then
 *   replay/volatile"), plus policy-driven scheduling and retention --
 *   appended to M5 rather than left as a follow-up, so the archiver's own
 *   base-backup production is a real, bounded resource (frequency-gated,
 *   count/age-pruned) before Milestones 6/7/8 (warm standby, PITR, cloud
 *   push) start building on top of it. `replay`/`persistent` is still a
 *   later milestone -- that mode keeps its staging instance resident as a
 *   `warm-standby` `archiver_node` row, which doesn't exist until
 *   Milestone 6.
 *
 * Trigger scope: bootstrap is always `live` (nothing to replay from yet on
 * the first run, matching the design doc's own bootstrap rule), every
 * backup after that follows basebackup_policy's own `source`/`replaymode`
 * (resolved via monitor_get_basebackup_policy_for_group(), which chains
 * get_archiver_policy()'s group-override / formation-default / schema-
 * default fallback the same way wal_archived()'s own archiver_quorum
 * lookup does), gated on `frequency` seconds having elapsed since the
 * newest existing backup -- or fired immediately regardless of frequency
 * when `onpromotion` is set and the group's primary has changed since the
 * last tick that checked (see get_current_primary_node_id()'s own
 * comment). After each successful completion, retention prunes anything
 * beyond `maxcount` or older than `maxage` (apply_basebackup_retention()):
 * removes the directory, then report_basebackup_deleted() on the monitor,
 * which cascades to prune_archiver_wal() on its own. `concurrency` is
 * read but not enforced: this milestone's own single-membership scope
 * (one archiver, one group) already limits this file to one base backup
 * production job in flight at a time (basebackup_child_is_running()) --
 * running several concurrently only has meaning once an archiver can serve
 * more than one (formation, group) at once, a later milestone's concern.
 *
 * Target selection ('live') follows the design doc's own precedence,
 * minus its warm-standby tier (a later milestone, nothing to select from
 * yet): the first healthy secondary in the group, falling back to the
 * primary when none exists. "Healthy" here just means "reachable via
 * pgautofailover.get_nodes()", not "least-loaded" -- picking between
 * several healthy secondaries by load is a refinement, not required for
 * base-backup generation to work at all.
 *
 * Target selection ('replay') is entirely local: a throwaway staging
 * Postgres instance, extracted from the last retained base backup and
 * replayed forward using this archiver's own already-captured WAL (no
 * network round trip to any live node at all) until it promotes on its
 * own (see write_replay_recovery_config()'s own comment on why this
 * targets "everything locally available" rather than a specific LSN),
 * then sourced via pg_basebackup over loopback and discarded ('volatile':
 * no persistent archiver_node row, nothing left running or on disk between
 * cycles).
 *
 * Base backup generation itself is a one-shot forked child (tracked via
 * basebackupPid, the same pattern service_archiver.c uses for
 * pgReceivewalPid), not a persistent supervised service: it runs to
 * completion and exits, so it must not block service_archiver_loop()'s own
 * per-tick node_active()/WAL-report cycle for however long it takes.
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

#include "postgres_fe.h"

#include "service_archiver_basebackup.h"

#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "runprogram.h"
#include "signals.h"

/*
 * One base backup generation child at a time, mirroring
 * service_archiver.c's own pgReceivewalPid tracking pattern.
 */
static pid_t basebackupPid = -1;

/* accumulator for directory_size()'s nftw() callback -- nftw() has no
 * user-data parameter, so this has to be file-scope */
static uint64_t directorySizeAccumulator = 0;

/* how long to wait for the replay staging instance to finish replaying
 * available WAL and promote before giving up on this cycle */
#define ARCHIVER_REPLAY_PROMOTE_TIMEOUT_SECONDS 60


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
 * query_wal_position runs a single ad hoc query against connInfo, used
 * right after a base backup finishes to capture the source's current WAL
 * write position (primary) or replay position (standby/staging instance)
 * -- recorded as the backup's endlsn. Not the exact internal stop-backup
 * LSN real pg_basebackup computes server-side (not observable from a plain
 * CLI wrapper around it), but a reasonable upper bound: "WAL up to at
 * least this point must be replayed to reach consistency."
 */
static bool
query_wal_position(const char *connInfo, bool isPrimary,
				   char *lsn, size_t lsnSize)
{
	PGSQL client = { 0 };

	if (!pgsql_init(&client, (char *) connInfo, PGSQL_CONN_UPSTREAM))
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
 * base backup over. Exposed (service_archiver_basebackup.h) for service_
 * archiver.c's own periodic storage-usage report, over the archiver's
 * whole pgdata rather than just one backup directory.
 */
uint64_t
directory_size(const char *dirPath)
{
	directorySizeAccumulator = 0;

	(void) nftw(dirPath, accumulate_file_size, 16, FTW_PHYS);

	return directorySizeAccumulator;
}


/*
 * run_pg_basebackup execs the real, unmodified pg_basebackup client
 * against source, writing into backupDir. --wal-method=none: this backup
 * is deliberately not self-consistent on its own -- for a `live` backup,
 * the archiver's already-running WAL capture (service_archiver.c) is what
 * supplies the WAL needed to reach consistency on replay; for a `replay`
 * backup, the source is itself already paused at a known-consistent LSN,
 * so there is nothing further to bundle either way.
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

	log_info("Generating base backup \"%s\" from %s:%d into \"%s\"",
			 label, source->host, source->port, backupDir);

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
 * report_basebackup reads backupDir's own backup_label for the
 * authoritative start position, then reports both the start and
 * completion of this base backup to the monitor. Shared by the live and
 * replay paths; source/replaymode is the one thing that differs.
 */
static bool
report_basebackup(Keeper *keeper, NodeAddress *endLsnSource,
				  const char *backupDir, const char *label,
				  const char *source, const char *replaymode)
{
	KeeperConfig *config = &(keeper->config);

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
										   source, replaymode,
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
	 * plain read-only SQL query -- true of the replay staging instance too,
	 * copied verbatim from a `live` backup of an ordinary node.
	 */
	char connInfo[MAXCONNINFO] = { 0 };

	sformat(connInfo, sizeof(connInfo),
			"host=%s port=%d user=%s dbname=%s application_name=%s",
			endLsnSource->host, endLsnSource->port,
			PG_AUTOCTL_REPLICA_USERNAME, DEFAULT_DATABASE_NAME, config->name);

	char endLsn[PG_LSN_MAXLENGTH] = { 0 };

	if (!query_wal_position(connInfo, endLsnSource->isPrimary,
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
 * apply_basebackup_retention lists every complete base backup for this
 * group (newest first, list_basebackups()'s own ordering) and prunes
 * whatever policy says shouldn't survive: anything beyond the newest
 * maxcount, or older than maxage, whichever fires first for a given
 * backup -- a backup can be pruned for either reason independently, not
 * only once maxcount is already exceeded. maxcount <= 0 or maxage_seconds
 * <= 0 disables that particular rule (there is no real-world policy where
 * "keep zero backups" or "expire instantly" is the intended behavior; the
 * schema's own CHECK constraints don't allow either as a stored value,
 * but this stays defensive against a hand-edited row or a future relaxed
 * constraint).
 *
 * Best effort past the first failure: one backup's directory failing to
 * remove (e.g. a permissions issue) does not stop the rest of the list
 * from being evaluated -- each one is independent, and the failed one
 * simply gets retried on the next cycle since it's still 'complete' and
 * still over its own retention rule.
 */
static bool
apply_basebackup_retention(Keeper *keeper, BasebackupPolicy *policy)
{
	KeeperConfig *config = &(keeper->config);
	BasebackupInfoArray backups = { 0 };

	if (!monitor_list_basebackups(&(keeper->monitor),
								  config->formation, config->groupId,
								  &backups))
	{
		log_warn("Failed to list base backups for retention, will retry "
				 "on the next cycle");
		return false;
	}

	time_t now = time(NULL);
	bool success = true;

	for (int i = 0; i < backups.count; i++)
	{
		BasebackupInfo *backup = &(backups.backups[i]);

		bool beyondMaxCount = policy->maxCount > 0 && i >= policy->maxCount;
		bool beyondMaxAge = policy->maxAgeSeconds > 0 &&
							(now - (time_t) backup->startedAtEpoch) >
							policy->maxAgeSeconds;

		if (!beyondMaxCount && !beyondMaxAge)
		{
			continue;
		}

		log_info("Pruning base backup \"%s\" (%s)",
				 backup->label,
				 beyondMaxCount ? "beyond maxcount" : "past maxage");

		if (directory_exists(backup->storageLocation) &&
			!rmtree(backup->storageLocation, true))
		{
			log_warn("Failed to remove base backup directory \"%s\", will "
					 "retry on the next cycle", backup->storageLocation);
			success = false;
			continue;
		}

		if (!monitor_report_basebackup_deleted(&(keeper->monitor),
											   backup->basebackupId))
		{
			log_warn("Failed to report base backup %" PRId64 " as deleted "
															 "to the monitor, will retry on the next cycle",
					 backup->basebackupId);
			success = false;
		}
	}

	return success;
}


/*
 * generate_live_basebackup is the forked child's own body for a `live`
 * backup: run pg_basebackup against source to completion, report it, then
 * apply retention. Runs in its own process, with its own monitor
 * connection (the parent's keeper->monitor is not fork-safe to share,
 * exactly as service_archiver_run.c's own supervised children already
 * document).
 */
static bool
generate_live_basebackup(Keeper *keeper, NodeAddress *source,
						 const char *backupDir, const char *label,
						 BasebackupPolicy *policy)
{
	if (!run_pg_basebackup(&(keeper->config), source, backupDir, label))
	{
		return false;
	}

	if (!report_basebackup(keeper, source, backupDir, label, "live", NULL))
	{
		return false;
	}

	(void) apply_basebackup_retention(keeper, policy);

	return true;
}


/*
 * copy_directory_tree shells out to `cp -R -p` (POSIX-portable across this
 * project's actual dev/CI targets, unlike GNU cp's `-a`) to seed the
 * replay staging directory from the last retained base backup. No
 * existing recursive-copy helper exists in this codebase to reuse, and
 * reimplementing one (special files, symlinks, permissions) is a much
 * larger and riskier undertaking than reusing a battle-tested system
 * utility -- the same reasoning this project already applies to
 * pg_basebackup/pg_receivewal/pg_ctl themselves. Uses run_program()
 * (runprogram.h), this project's own subprocess helper, rather than a
 * hand-rolled fork()/exec(): matches every other external-program call in
 * this codebase, and captures stderr for the error message below.
 */
static bool
copy_directory_tree(const char *sourceDir, const char *destDir)
{
	char cpPath[MAXPGPATH] = { 0 };

	if (!search_path_first("cp", cpPath, LOG_ERROR))
	{
		log_error("Failed to find \"cp\" in PATH");
		return false;
	}

	Program program = run_program(cpPath, "-R", "-p", sourceDir, destDir, NULL);
	bool success = program.returnCode == 0;

	if (!success)
	{
		log_error("cp -R -p \"%s\" \"%s\" failed: %s",
				  sourceDir, destDir,
				  program.stdErr != NULL ? program.stdErr : "");
	}

	free_program(&program);

	return success;
}


/*
 * write_replay_recovery_config points the staging instance's recovery at
 * this archiver's own local WAL cache (the colocated fast path -- no
 * network round trip needed, matching service_archiver.c's own philosophy).
 * No recovery_target_lsn: an idle-ish source produces mostly-zero-padded
 * segments (a "complete", renamed segment file is always its full fixed
 * size regardless of how much of it is real WAL), so "the end of the
 * latest complete segment" is not actually a reachable record boundary --
 * recovery correctly refuses to pause at a target that doesn't correspond
 * to any real record, and errors out instead ("recovery ended before
 * configured recovery target was reached"). Instead, this replays every
 * available locally-captured record and lets Postgres promote once
 * restore_command runs out of segments to fetch -- for a snapshot that
 * gets pg_basebackup'd and discarded immediately after (this is
 * `volatile`: nothing persists between cycles), a promoted instance is
 * exactly as usable a source as a paused one; only a `persistent` replica
 * kept resident between cycles (a later milestone) would need the more
 * precise pause-at-target-LSN behavior the design doc describes for
 * `pg_autoctl warm-standby advance`.
 *
 * recovery.signal, not standby.signal: this is a one-shot archive recovery
 * of already-captured WAL, not open-ended standby streaming.
 */
static bool
write_replay_recovery_config(const char *stagingDir, const char *walcacheDir)
{
	/*
	 * recovery.signal is what actually puts Postgres into archive recovery
	 * at startup (PG12+): without it, a data directory that still has
	 * backup_label is treated as an ordinary crash-recovery restart, which
	 * fails outright since the copied backup's pg_wal has no local WAL to
	 * replay from ("could not locate required checkpoint record") --
	 * restore_command is only ever consulted once recovery.signal (or
	 * standby.signal) says this is a recovery in the first place.
	 */
	char signalPath[MAXPGPATH] = { 0 };

	sformat(signalPath, sizeof(signalPath), "%s/recovery.signal", stagingDir);

	if (!write_file("", 0, signalPath))
	{
		return false;
	}

	char confPath[MAXPGPATH] = { 0 };

	sformat(confPath, sizeof(confPath), "%s/postgresql.auto.conf", stagingDir);

	char conf[BUFSIZE] = { 0 };

	/*
	 * ssl = off: the copied postgresql.conf/postgresql.auto.conf still
	 * carries the source node's own ssl_cert_file/ssl_key_file settings
	 * (typically absolute paths into *that node's* PGDATA, e.g. from
	 * --ssl-self-signed) -- meaningless here, since this archiver has no
	 * Postgres SSL certs of its own to begin with. Left enabled, the
	 * staging instance fails outright at startup ("could not load server
	 * certificate file ...: No such file or directory"). Safe to disable
	 * unconditionally: this instance only ever accepts the loopback
	 * pg_basebackup connection below, for the lifetime of one throwaway
	 * cycle.
	 */
	sformat(conf, sizeof(conf),
			"\n"
			"# added by pg_autoctl's archiver replay/volatile base backup generation\n"
			"restore_command = 'cp \"%s/%%f\" \"%%p\"'\n"
			"ssl = off\n",
			walcacheDir);

	return append_to_file(conf, strlen(conf), confPath);
}


/*
 * pid of the currently-running replay staging instance, if any -- tracked
 * the same way service_archiver.c tracks pgReceivewalPid, so
 * stop_staging_postgres() knows what to signal.
 */
static pid_t stagingPostgresPid = -1;


/*
 * start_staging_postgres execs the real "postgres" binary directly against
 * stagingDir, loopback-only, on PG_AUTOCTL_ARCHIVER_REPLAY_PORT -- the same
 * fork()/execv() pattern already used for pg_receivewal
 * (service_archiver.c) and pg_basebackup (run_pg_basebackup(), this file),
 * rather than going through pg_ctl: readiness is confirmed by
 * wait_for_replay_pause()'s own connection-retry loop below, so pg_ctl's
 * own "-w" startup wait buys nothing here, and this sidesteps it -- and the
 * SQL-connection-based readiness check this needs anyway.
 */
static bool
start_staging_postgres(KeeperConfig *config, const char *stagingDir)
{
	char postgresPath[MAXPGPATH] = { 0 };

	path_in_same_directory(config->pgSetup.pg_ctl, "postgres", postgresPath);

	if (!file_exists(postgresPath))
	{
		log_error("Failed to find postgres at \"%s\"", postgresPath);
		return false;
	}

	char portStr[NAMEDATALEN] = { 0 };

	sformat(portStr, sizeof(portStr), "%d", PG_AUTOCTL_ARCHIVER_REPLAY_PORT);

	pid_t pid = fork();

	if (pid == -1)
	{
		log_error("Failed to fork postgres: %m");
		return false;
	}

	if (pid == 0)
	{
		char *args[8];
		int argsIndex = 0;

		args[argsIndex++] = postgresPath;
		args[argsIndex++] = "-D";
		args[argsIndex++] = (char *) stagingDir;
		args[argsIndex++] = "-p";
		args[argsIndex++] = portStr;
		args[argsIndex++] = "-h";
		args[argsIndex++] = "127.0.0.1";
		args[argsIndex] = NULL;

		execv(postgresPath, args);

		/* execv only returns on failure */
		log_fatal("execv(\"%s\"): %m", postgresPath);
		_exit(127);
	}

	stagingPostgresPid = pid;

	return true;
}


/*
 * stop_staging_postgres stops the replay staging instance. Best effort:
 * called during cleanup, including on failure paths where the instance may
 * or may not have actually started.
 */
static void
stop_staging_postgres(void)
{
	if (stagingPostgresPid <= 0)
	{
		return;
	}

	if (kill(stagingPostgresPid, SIGTERM) != 0 && errno != ESRCH)
	{
		log_warn("Failed to send SIGTERM to the replay staging instance "
				 "(pid %d): %m", stagingPostgresPid);
	}

	int status = 0;

	if (waitpid(stagingPostgresPid, &status, 0) == -1 && errno != ECHILD)
	{
		log_warn("Failed to wait for the replay staging instance "
				 "(pid %d) to stop: %m", stagingPostgresPid);
	}

	stagingPostgresPid = -1;
}


/*
 * wait_for_replay_promotion connects to the staging instance (retrying: it
 * takes a moment after fork()/execv() to start accepting connections) and
 * polls pg_is_in_recovery() until it reports false -- Postgres promotes on
 * its own once restore_command runs out of segments to fetch (see
 * write_replay_recovery_config()'s own comment on why this replays to "no
 * more locally-captured WAL" rather than a specific target LSN) -- or
 * timeoutSeconds elapses.
 */
static bool
wait_for_replay_promotion(const char *connInfo, int timeoutSeconds)
{
	time_t deadline = time(NULL) + timeoutSeconds;
	bool promoted = false;

	while (!promoted && time(NULL) < deadline)
	{
		PGSQL client = { 0 };

		if (pgsql_init(&client, (char *) connInfo, PGSQL_CONN_UPSTREAM))
		{
			SingleValueResultContext context = { { 0 }, PGSQL_RESULT_BOOL, false };
			const char *sql = "SELECT pg_is_in_recovery()";

			if (pgsql_execute_with_params(&client, sql, 0, NULL, NULL,
										  &context, &parseSingleValueResult) &&
				context.parsedOk)
			{
				promoted = !context.boolVal;
			}

			PQfinish(client.connection);
		}

		if (!promoted)
		{
			sleep(1);
		}
	}

	return promoted;
}


/*
 * generate_replay_basebackup is the forked child's own body for a
 * `replay`/`volatile` backup: extract the last retained base backup into a
 * fresh staging directory, replay this archiver's own locally-captured WAL
 * forward until it promotes (see write_replay_recovery_config()'s own
 * comment for why this targets "everything locally available" rather than
 * a specific LSN), snapshot the promoted instance via pg_basebackup over
 * loopback, report it, then stop and discard the staging instance --
 * 'volatile' means nothing survives between cycles, each one replays the
 * whole gap since the last retained backup again.
 */
static bool
generate_replay_basebackup(Keeper *keeper, const char *sourceBackupDir,
						   const char *backupDir, const char *label,
						   BasebackupPolicy *policy)
{
	KeeperConfig *config = &(keeper->config);

	char stagingDir[MAXPGPATH] = { 0 };

	sformat(stagingDir, sizeof(stagingDir), "%s/replay-staging",
			config->pgSetup.pgdata);

	if (directory_exists(stagingDir) && !rmtree(stagingDir, true))
	{
		log_error("Failed to remove leftover replay staging directory "
				  "\"%s\" from a previous cycle", stagingDir);
		return false;
	}

	log_info("Generating a replay base backup, extracting \"%s\" into \"%s\"",
			 sourceBackupDir, stagingDir);

	if (!copy_directory_tree(sourceBackupDir, stagingDir))
	{
		return false;
	}

	if (!write_replay_recovery_config(stagingDir, config->pgSetup.pgdata))
	{
		log_error("Failed to write replay recovery configuration in \"%s\"",
				  stagingDir);
		return false;
	}

	if (!start_staging_postgres(config, stagingDir))
	{
		return false;
	}

	char stagingConnInfo[MAXCONNINFO] = { 0 };

	sformat(stagingConnInfo, sizeof(stagingConnInfo),
			"host=127.0.0.1 port=%d user=%s dbname=%s application_name=%s",
			PG_AUTOCTL_ARCHIVER_REPLAY_PORT,
			PG_AUTOCTL_REPLICA_USERNAME, DEFAULT_DATABASE_NAME, config->name);

	bool ok = wait_for_replay_promotion(stagingConnInfo,
										ARCHIVER_REPLAY_PROMOTE_TIMEOUT_SECONDS);

	if (!ok)
	{
		log_error("Replay staging instance at \"%s\" failed to replay "
				  "available WAL and promote within %d seconds",
				  stagingDir, ARCHIVER_REPLAY_PROMOTE_TIMEOUT_SECONDS);
	}
	else
	{
		NodeAddress stagingNode = { 0 };

		strlcpy(stagingNode.host, "127.0.0.1", sizeof(stagingNode.host));
		stagingNode.port = PG_AUTOCTL_ARCHIVER_REPLAY_PORT;

		/* promoted by the time wait_for_replay_promotion() returns true --
		 * report_basebackup()'s own endlsn query needs to know to use
		 * pg_current_wal_lsn(), not pg_last_wal_replay_lsn() (NULL outside
		 * recovery) */
		stagingNode.isPrimary = true;

		ok = run_pg_basebackup(config, &stagingNode, backupDir, label) &&
			 report_basebackup(keeper, &stagingNode, backupDir, label,
							   "replay", policy->replayMode);

		if (ok)
		{
			(void) apply_basebackup_retention(keeper, policy);
		}
	}

	stop_staging_postgres();

	/* volatile: discard the staging instance unconditionally, success or not */
	if (!rmtree(stagingDir, true))
	{
		log_warn("Failed to remove replay staging directory \"%s\" after "
				 "use, will be overwritten on the next cycle", stagingDir);
	}

	return ok;
}


/*
 * lastKnownPrimaryNodeId tracks the group's primary across ticks, purely
 * in-memory (reset on archiver restart, same lifetime as basebackupPid/
 * stagingPostgresPid above) -- -1 means "not observed yet", which the
 * onpromotion check below treats as "nothing to compare against", not "a
 * promotion just happened" (that would misfire a forced backup on this
 * process's very first tick).
 */
static int64_t lastKnownPrimaryNodeId = -1;


/*
 * get_current_primary_node_id finds the group's current primary via the
 * same monitor_get_nodes() call select_basebackup_source() already makes
 * for its own, different purpose (picking a live source) -- kept as a
 * separate round trip rather than sharing state across the two call
 * sites, since either can run without the other on a given tick
 * (onpromotion is checked unconditionally; select_basebackup_source() only
 * runs once a backup already turns out to be due). Returns false (not an
 * error) when the group currently has no primary at all (mid-election) --
 * callers should skip the comparison for this tick rather than treat that
 * as "no promotion".
 */
static bool
get_current_primary_node_id(Keeper *keeper, int64_t *primaryNodeId)
{
	NodeAddressArray nodeArray = { 0 };

	if (!monitor_get_nodes(&(keeper->monitor),
						   keeper->config.formation,
						   keeper->config.groupId,
						   &nodeArray))
	{
		return false;
	}

	for (int i = 0; i < nodeArray.count; i++)
	{
		if (nodeArray.nodes[i].isPrimary)
		{
			*primaryNodeId = nodeArray.nodes[i].nodeId;
			return true;
		}
	}

	return false;
}


/*
 * service_archiver_maybe_generate_basebackup checks, once per
 * service_archiver_loop() tick, whether a base backup generation is due
 * for this group and -- if so, and no generation is already in flight --
 * forks a child to produce one. See this file's own header comment for
 * the full policy-driven trigger scope this implements.
 */
bool
service_archiver_maybe_generate_basebackup(Keeper *keeper)
{
	if (basebackup_child_is_running())
	{
		return true;
	}

	KeeperConfig *config = &(keeper->config);

	BasebackupPolicy policy = { 0 };
	bool foundPolicy = false;

	if (!monitor_get_basebackup_policy_for_group(&(keeper->monitor),
												 config->formation,
												 config->groupId,
												 &policy, &foundPolicy))
	{
		/* errors already logged */
		return false;
	}

	if (!foundPolicy)
	{
		/* shouldn't happen: the schema's own 'default' policy always
		 * exists, and get_archiver_policy()'s own three-way fallback
		 * always resolves to at least that row */
		log_warn("Failed to resolve a base-backup policy for \"%s\"/%d, "
				 "skipping this cycle", config->formation, config->groupId);
		return true;
	}

	BasebackupInfoArray backups = { 0 };

	if (!monitor_list_basebackups(&(keeper->monitor),
								  config->formation, config->groupId,
								  &backups))
	{
		/* errors already logged */
		return false;
	}

	bool bootstrap = (backups.count == 0);

	/*
	 * Runs every tick regardless of whether a backup is otherwise due, so
	 * lastKnownPrimaryNodeId always reflects the most recently observed
	 * primary -- skipping this update on a due-anyway tick would compare
	 * a future promotion against a stale value from several ticks back
	 * and misfire.
	 */
	bool forcedByPromotion = false;

	if (policy.onPromotion)
	{
		int64_t currentPrimaryNodeId = 0;

		if (get_current_primary_node_id(keeper, &currentPrimaryNodeId))
		{
			if (lastKnownPrimaryNodeId >= 0 &&
				lastKnownPrimaryNodeId != currentPrimaryNodeId)
			{
				forcedByPromotion = true;

				log_info("Forcing a new base backup: the group's primary "
						 "changed (node %" PRId64 " -> node %" PRId64 ")",
						 lastKnownPrimaryNodeId, currentPrimaryNodeId);
			}

			lastKnownPrimaryNodeId = currentPrimaryNodeId;
		}
	}

	bool due = bootstrap || forcedByPromotion;

	if (!due)
	{
		time_t now = time(NULL);
		time_t elapsed = now - (time_t) backups.backups[0].startedAtEpoch;

		due = elapsed >= (time_t) policy.frequencySeconds;
	}

	if (!due)
	{
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

	/* bootstrap is always 'live' -- nothing to replay from yet, matching
	 * the design doc's own bootstrap rule -- every backup after that
	 * follows the resolved policy's own source */
	bool useReplay = !bootstrap && strcmp(policy.source, "replay") == 0;

	time_t now = time(NULL);
	struct tm nowUTC = { 0 };

	gmtime_r(&now, &nowUTC);

	char label[NAMEDATALEN] = { 0 };

	strftime(label, sizeof(label),
			 useReplay ? "basebackup-replay-%Y%m%dT%H%M%SZ"
			 : "basebackup-%Y%m%dT%H%M%SZ",
			 &nowUTC);

	char backupDir[MAXPGPATH] = { 0 };

	sformat(backupDir, sizeof(backupDir), "%s/%s", backupsDir, label);

	/*
	 * sourceBackupDir/policy must be captured now, in the parent, into
	 * buffers the forked child can safely read after fork(): both are
	 * local, stack-allocated, still valid across fork() (the child gets
	 * its own copy of the whole address space).
	 */
	char sourceBackupDir[MAXPGPATH] = { 0 };

	if (useReplay)
	{
		strlcpy(sourceBackupDir, backups.backups[0].storageLocation,
				sizeof(sourceBackupDir));
	}

	NodeAddress liveSource = { 0 };
	bool haveLiveSource = false;

	if (!useReplay)
	{
		if (!select_basebackup_source(keeper, &liveSource))
		{
			log_warn("No eligible node to source a live base backup from "
					 "yet, will retry");
			return true;
		}

		haveLiveSource = true;
	}

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

		bool ok = haveLiveSource
				  ? generate_live_basebackup(keeper, &liveSource, backupDir,
											 label, &policy)
				  : generate_replay_basebackup(keeper, sourceBackupDir,
											   backupDir, label, &policy);

		exit(ok ? EXIT_CODE_QUIT : EXIT_CODE_INTERNAL_ERROR);
	}

	log_debug("pg_autoctl archiver basebackup process started in "
			  "subprocess %d", pid);
	basebackupPid = pid;

	return true;
}
