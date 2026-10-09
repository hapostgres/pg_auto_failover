/*
 * src/bin/pg_walserver/cli_list.c
 *   See cli_list.h.
 *
 *   LSN-range computation ("list clusters"): the start LSN of a cluster's
 *   currently covered WAL range comes straight from its latest base
 *   backup's own "backup_label" ("START WAL LOCATION"), read with
 *   cmd_base_backup.c's own read_backup_label() -- already parsed,
 *   already tested, no reason to duplicate it. The end LSN comes from
 *   wal_dir_scan.c's own wal_dir_find_latest(): a single opendir()/
 *   readdir() pass over the cluster's own directory picking out the
 *   highest-numbered *complete* WAL segment filename, which is already
 *   this project's one existing "what's the latest WAL we have" answer
 *   (used elsewhere for IDENTIFY_SYSTEM/CREATE_REPLICATION_SLOT). It is a
 *   directory scan, not a forward-probing "stat a handful of candidate
 *   filenames from a last-known position" search -- reusing it as-is,
 *   rather than writing a second, probe-based implementation beside it,
 *   was judged the better trade here: a WAL cache directory holds nothing
 *   but WAL/.partial/.backup/.history files (nothing else is ever written
 *   there), so one linear readdir() over it costs one syscall loop no
 *   matter how it is answered, and "list clusters" is an interactive,
 *   occasional operator command, not a per-connection hot path -- unlike,
 *   say, an actual "resume streaming from here" decision, where avoiding a
 *   full scan matters far more. See this file's own header comment on
 *   "list wal"/"list backups" for where a full scan genuinely cannot be
 *   avoided (their own aggregate stats), and the caching trade-off made
 *   there instead.
 *
 *   Caching: the per-cluster metadata a fully accurate "list backups"/
 *   "list wal" needs (segment counts, total bytes, the full backup/.history
 *   inventory) is NOT cheaply derivable from a probe the way a single "what
 *   is the newest complete segment" answer is -- it requires reading every
 *   entry in the directory at least once. The original design sketch for
 *   this feature called for a small, per-cluster, incrementally-maintained
 *   cache file, updated by each of the existing code paths that already
 *   write into a cluster's own directory (the embedded receivewal worker on each
 *   completed segment, archive-wal/ARCHIVE_FILE on each push, the bootstrap-
 *   backup code on completion, archive-cleanup on removal). That was not
 *   implemented in this pass: wiring an incremental-cache update into four
 *   independent, already-shipped write paths, correctly and without risking
 *   a stale/inconsistent cache outliving a crash mid-update, is real,
 *   nontrivial plumbing that did not fit safely in the time available for
 *   this change. What IS implemented instead is the documented fallback:
 *   "list backups"/"list wal" compute their answer fresh, by scanning the
 *   cluster's own directory, on every invocation -- correct always, cached
 *   only for the lifetime of that one invocation (a single directory scan
 *   feeds every row printed for that cluster, never re-scanned per row). A
 *   directory holding a realistic number of WAL segments (thousands, e.g. a
 *   few days of retention at the default 16MB segment size) scans in well
 *   under the time a human operator running this by hand would notice; if
 *   "list wal"/"list backups" against a *very* large, long-retention
 *   archive ever becomes a real bottleneck, the incremental cache file
 *   sketched above is the natural next step, reusing this same WsWalStats/
 *   WsBackupInfo shape as its own on-disk schema.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "backup_list.h"
#include "cli_common.h"
#include "cli_list.h"
#include "cli_root.h"
#include "cmd_base_backup.h"
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "clusters.h"
#include "string_utils.h"
#include "system_utils.h"
#include "wal_dir_scan.h"

/*
 * "pidfile.h" above may resolve to either src/bin/common/pidfile.h or, via
 * this project's own include-path fallback, pg_autoctl's own pidfile.h --
 * which pulls in keeper.h, and, with it, commandline.h's own "streq" macro.
 * Guard against a redefinition error either way, rather than relying on
 * which one the include path happens to pick.
 */
#ifndef streq
#define streq(x, y) ((x != NULL) && (y != NULL) && (strcmp(x, y) == 0))
#endif

static int cli_list_clusters_getopt(int argc, char **argv);
static void cli_list_clusters_command_run(int argc, char **argv);

static void cli_list_backups_command_run(int argc, char **argv);

static int cli_list_wal_getopt(int argc, char **argv);
static void cli_list_wal_command_run(int argc, char **argv);

CommandLine list_clusters_command =
	make_command("clusters",
				 "List every cluster, its backup/receivewal status, and the "
				 "WAL range it covers",
				 "--pgdata <path> [--config <path>] [--cluster <name>]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single cluster\n",
				 cli_list_clusters_getopt, cli_list_clusters_command_run);

CommandLine list_backups_command =
	make_command("backups",
				 "List base backups per cluster (label, size, which is "
				 ".latest)",
				 "--pgdata <path> [--config <path>] [--cluster <name>]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single cluster\n",
				 cli_list_clusters_getopt, cli_list_backups_command_run);

CommandLine list_wal_command =
	make_command("wal",
				 "List WAL cache aggregate stats per cluster, or every "
				 "file with --segments",
				 "--pgdata <path> [--config <path>] [--cluster <name>] "
				 "[--segments]",
				 "  --pgdata    this instance's own data root (defaults to "
				 "PGDATA)\n"
				 "  --config  where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --cluster   limit output to a single cluster\n"
				 "  --segments  list every individual WAL/.history/.backup "
				 "file instead\n"
				 "              of the default aggregate stats\n",
				 cli_list_wal_getopt, cli_list_wal_command_run);

static CommandLine *list_subcommands[] = {
	&list_clusters_command,
	&list_backups_command,
	&list_wal_command,
	NULL
};

CommandLine list_commands =
	make_command_set("list",
					 "List clusters, base backups, or WAL cache contents",
					 NULL, NULL, NULL, list_subcommands);


/* ---------------------------------------------------------------------
 * small formatting helpers, shared by all three "list" sub-commands
 * --------------------------------------------------------------------- */

/*
 * format_utc renders t as an ISO-8601 UTC timestamp ("YYYY-MM-DDTHH:MM:SSZ"),
 * or "-" when t is unset (<= 0).
 */
static void
format_utc(time_t t, char *dest, size_t destSize)
{
	if (t <= 0)
	{
		strlcpy(dest, "-", destSize);
		return;
	}

	struct tm tmVal = { 0 };

	gmtime_r(&t, &tmVal);
	strftime(dest, destSize, "%Y-%m-%dT%H:%M:%SZ", &tmVal);
}


/*
 * directory_size recursively sums the size in bytes of every regular file
 * under path (a base backup directory: PGDATA's own base/, pg_wal/, etc.
 * subdirectories included) -- "list backups" own per-row size column.
 */
static uint64_t
directory_size(const char *path)
{
	DIR *dir = opendir(path);

	if (dir == NULL)
	{
		return 0;
	}

	uint64_t total = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
		{
			continue;
		}

		char entryPath[MAXPGPATH] = { 0 };

		sformat(entryPath, sizeof(entryPath), "%s/%s", path, entry->d_name);

		struct stat st;

		if (lstat(entryPath, &st) != 0)
		{
			continue;
		}

		if (S_ISDIR(st.st_mode))
		{
			total += directory_size(entryPath);
		}
		else if (S_ISREG(st.st_mode))
		{
			total += (uint64_t) st.st_size;
		}
	}

	closedir(dir);

	return total;
}


/* ---------------------------------------------------------------------
 * shared cluster loading/filtering
 * --------------------------------------------------------------------- */

/*
 * load_pgdata_clusters reads the config file config_file_path() resolves
 * for pgdata/configFile into a freshly malloc'ed array (clusters_load()),
 * the shared first step every "list" sub-command needs; when
 * clusterFilter is given, also refuses (false, an error already logged)
 * if no cluster matches it, rather than every caller having to check that
 * on its own.
 */
static bool
load_pgdata_clusters(const char *pgdata, const char *configFile,
					 const char *clusterFilter,
					 WsCluster **clustersOut, int *countOut)
{
	if ((pgdata == NULL || pgdata[0] == '\0') &&
		(configFile == NULL || configFile[0] == '\0'))
	{
		log_error("--pgdata or --config is required (or set the PGDATA "
				  "environment variable)");
		return false;
	}

	char clustersPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, clustersPath, sizeof(clustersPath));

	if (!clusters_load(clustersPath, clustersOut, countOut))
	{
		log_error("Failed to parse \"%s\"", clustersPath);
		return false;
	}

	if (clusterFilter != NULL && clusterFilter[0] != '\0')
	{
		if (clusters_find_exact(*clustersOut, *countOut, clusterFilter) == NULL)
		{
			log_error("No cluster \"%s\" in \"%s\"", clusterFilter, clustersPath);
			clusters_free(*clustersOut);
			*clustersOut = NULL;
			*countOut = 0;
			return false;
		}
	}

	return true;
}


/*
 * cluster_matches_filter is true when clusterFilter is empty (no --cluster
 * given, every cluster matches) or equals cluster's own key exactly.
 */
static bool
cluster_matches_filter(const WsCluster *cluster, const char *clusterFilter)
{
	return clusterFilter == NULL || clusterFilter[0] == '\0' ||
		   streq(cluster->key, clusterFilter);
}


/*
 * read_latest_label reads "<clusterPath>/basebackups/.latest" -- the label of
 * the cluster's currently active backup, trimmed of its trailing newline.
 * Returns false (labelOut untouched) when the cluster has no backup yet.
 */
static bool
read_latest_label(const char *clusterPath, char *labelOut, size_t labelOutSize)
{
	char latestPath[MAXPGPATH] = { 0 };

	sformat(latestPath, sizeof(latestPath), "%s/basebackups/.latest", clusterPath);

	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(latestPath, &contents, &fileSize) ||
		contents == NULL || fileSize == 0)
	{
		return false;
	}

	strlcpy(labelOut, contents, labelOutSize);
	free(contents);

	size_t len = strlen(labelOut);

	if (len > 0 && labelOut[len - 1] == '\n')
	{
		labelOut[len - 1] = '\0';
	}

	return labelOut[0] != '\0';
}


/*
 * receivewal_running_for_cluster cross-references the ps state file (ps_state.h,
 * written by a running "serve") against clusterKey. *knownOut is set to false
 * when "serve" is not running at all (the caller should print "n/a", not
 * "no": there is no receivewal worker status to report either way), true otherwise
 * with the return value being the actual running/stopped answer. When
 * entryOut is not NULL and a matching entry exists, it is copied there too --
 * "list clusters" uses this to reuse that entry's own live LSN reading
 * (ps_state.h's WsPsReceivewalEntry.lsn) rather than re-deriving one.
 */
static bool
receivewal_running_for_cluster(const char *pgdata, const char *clusterKey,
							   bool *knownOut, WsPsReceivewalEntry *entryOut)
{
	*knownOut = false;

	char pidfilePath[MAXPGPATH] = { 0 };

	sformat(pidfilePath, sizeof(pidfilePath), "%s/pg_walserver.pid", pgdata);

	pid_t servePid = 0;

	if (!read_pidfile(pidfilePath, &servePid))
	{
		return false;   /* "serve" is not running: unknown */
	}

	WsPsState state = { 0 };

	if (!ws_ps_state_read(pgdata, &state))
	{
		return false;
	}

	for (int i = 0; i < state.receivewalWorkerCount; i++)
	{
		if (streq(state.receivewalWorkers[i].clusterKey, clusterKey))
		{
			*knownOut = true;

			if (entryOut != NULL)
			{
				*entryOut = state.receivewalWorkers[i];
			}

			return state.receivewalWorkers[i].pid > 0 && kill(
				state.receivewalWorkers[i].pid, 0) == 0;
		}
	}

	/* the cluster has no receivewal worker entry at all: known, and definitely not
	 * running (either "receivewal = pull" isn't set, or it failed to start) */
	*knownOut = true;
	return false;
}


/* ---------------------------------------------------------------------
 * pg_walserver list clusters
 * --------------------------------------------------------------------- */
bool
cli_list_clusters_run(const char *pgdata, const char *configFile,
					  const char *clusterFilter)
{
	WsCluster *clusters = NULL;
	int clusterCount = 0;

	if (!load_pgdata_clusters(pgdata, configFile, clusterFilter, &clusters,
							  &clusterCount))
	{
		return false;
	}

	if (clusterCount == 0)
	{
		char configPath[MAXPGPATH] = { 0 };

		config_file_path(pgdata, configFile, configPath, sizeof(configPath));
		log_info("No clusters configured in \"%s\"", configPath);
		clusters_free(clusters);
		return true;
	}

	fformat(stdout, "%-20s %-8s %-10s %-8s %-22s %-22s\n",
			"CLUSTER", "BACKUP", "RECEIVEWAL", "WORKER", "WAL START", "WAL END");
	fformat(stdout, "%-20s %-8s %-10s %-8s %-22s %-22s\n",
			"--------------------", "--------", "----------", "--------",
			"----------------------", "----------------------");

	for (int i = 0; i < clusterCount; i++)
	{
		const WsCluster *cluster = &clusters[i];

		if (!cluster_matches_filter(cluster, clusterFilter))
		{
			continue;
		}

		char backupLabel[NAMEDATALEN] = { 0 };
		bool haveBackup = read_latest_label(cluster->path, backupLabel,
											sizeof(backupLabel));

		char startLsn[32] = "-";

		if (haveBackup)
		{
			char backupDir[MAXPGPATH] = { 0 };
			char lsn[32] = { 0 };
			int timeline = 0;

			sformat(backupDir, sizeof(backupDir), "%s/basebackups/%s",
					cluster->path, backupLabel);

			if (read_backup_label(backupDir, lsn, sizeof(lsn), &timeline))
			{
				strlcpy(startLsn, lsn, sizeof(startLsn));
			}
		}

		char endLsn[32] = "-";
		uint32_t tli = 0;

		(void) wal_dir_find_latest(cluster, &tli, endLsn, sizeof(endLsn));

		bool known = false;
		WsPsReceivewalEntry entry = { 0 };
		bool running = receivewal_running_for_cluster(pgdata, cluster->key, &known,
													  &entry);
		const char *receivewalStr = !cluster->receivewalPull ? "n/a" :
									!known ? "n/a" : running ? "yes" : "no";

		/*
		 * A running receivewal worker's own live reading (ps_state.h,
		 * relayed from its hook callbacks -- see accept_loop.c's own
		 * refresh_ps_state()) is a strictly more current "what's the latest
		 * WAL we have" answer than wal_dir_find_latest()'s segment-boundary
		 * scan above, for this exact cluster: use it in preference, falling
		 * back to the scan-based value when the cluster has no receivewal
		 * worker running, or it hasn't reported a reading yet.
		 */
		if (running && entry.lsn[0] != '\0')
		{
			strlcpy(endLsn, entry.lsn, sizeof(endLsn));
		}

		fformat(stdout, "%-20s %-8s %-10s %-8s %-22s %-22s\n",
				cluster->key, haveBackup ? "yes" : "no",
				cluster->receivewalPull ? "pull" : "none", receivewalStr,
				startLsn, endLsn);
	}

	clusters_free(clusters);

	return true;
}


/* ---------------------------------------------------------------------
 * pg_walserver list backups
 * --------------------------------------------------------------------- */
bool
cli_list_backups_run(const char *pgdata, const char *configFile,
					 const char *clusterFilter)
{
	WsCluster *clusters = NULL;
	int clusterCount = 0;

	if (!load_pgdata_clusters(pgdata, configFile, clusterFilter, &clusters,
							  &clusterCount))
	{
		return false;
	}

	fformat(stdout, "%-20s %-28s %-22s %-10s %s\n",
			"CLUSTER", "LABEL", "TAKEN AT", "SIZE", "LATEST");
	fformat(stdout, "%-20s %-28s %-22s %-10s %s\n",
			"--------------------", "----------------------------",
			"----------------------", "----------", "------");

	for (int i = 0; i < clusterCount; i++)
	{
		const WsCluster *cluster = &clusters[i];

		if (!cluster_matches_filter(cluster, clusterFilter))
		{
			continue;
		}

		uint64_t segSize = ws_cluster_wal_segment_size(cluster);
		WsBackupInfo *backups = NULL;
		int backupCount = 0;

		if (!ws_backup_list_load(cluster->path, segSize, &backups, &backupCount))
		{
			log_warn("Could not read backups for cluster \"%s\"", cluster->key);
			continue;
		}

		char latestLabel[NAMEDATALEN] = { 0 };

		(void) read_latest_label(cluster->path, latestLabel, sizeof(latestLabel));

		for (int b = 0; b < backupCount; b++)
		{
			char takenAt[32] = { 0 };
			char size[32] = { 0 };

			format_utc(backups[b].takenAt, takenAt, sizeof(takenAt));
			pretty_print_bytes(size, sizeof(size), directory_size(backups[b].dirPath));

			fformat(stdout, "%-20s %-28s %-22s %-10s %s\n",
					cluster->key, backups[b].label, takenAt, size,
					streq(backups[b].label, latestLabel) ? "yes" : "");
		}

		free(backups);
	}

	clusters_free(clusters);

	return true;
}


/* ---------------------------------------------------------------------
 * pg_walserver list wal
 * --------------------------------------------------------------------- */
typedef struct WsWalStats
{
	int segments;
	int partials;
	int backups;
	int history;
	uint64_t totalBytes;
	char oldest[25];
	char newest[25];
} WsWalStats;


/*
 * scan_wal_dir classifies and tallies every WAL/.partial/.backup/.history
 * file under cluster's own directory into *stats (segment/partial/backup/
 * history counts, total bytes, oldest/newest complete segment); when
 * printSegments, also prints one row per file as it goes (the
 * "--segments" detail view), so this only ever scans the directory once
 * regardless of which "list wal" mode was asked for. False (stats
 * left zeroed) if the directory itself cannot be opened.
 */
static bool
scan_wal_dir(const WsCluster *cluster, WsWalStats *stats, bool printSegments,
			 const char *clusterKey)
{
	memset(stats, 0, sizeof(WsWalStats));

	DIR *dir = opendir(cluster->path);

	if (dir == NULL)
	{
		return false;
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		char segment[25] = { 0 };
		WsWalFileKind kind = ws_wal_dir_classify_filename(entry->d_name, segment);

		if (kind == WS_WAL_FILE_OTHER)
		{
			continue;
		}

		char entryPath[MAXPGPATH] = { 0 };

		sformat(entryPath, sizeof(entryPath), "%s/%s", cluster->path, entry->d_name);

		struct stat st;
		uint64_t size = 0;

		if (stat(entryPath, &st) == 0)
		{
			size = (uint64_t) st.st_size;
		}

		stats->totalBytes += size;

		switch (kind)
		{
			case WS_WAL_FILE_SEGMENT:
			{
				stats->segments++;

				if (stats->oldest[0] == '\0' || strcmp(segment, stats->oldest) < 0)
				{
					strlcpy(stats->oldest, segment, sizeof(stats->oldest));
				}

				if (stats->newest[0] == '\0' || strcmp(segment, stats->newest) > 0)
				{
					strlcpy(stats->newest, segment, sizeof(stats->newest));
				}

				break;
			}

			case WS_WAL_FILE_PARTIAL:
			{
				stats->partials++;
				break;
			}

			case WS_WAL_FILE_BACKUP:
			{
				stats->backups++;
				break;
			}

			case WS_WAL_FILE_HISTORY:
			{
				stats->history++;
				break;
			}

			default:
			{
				break;
			}
		}

		if (printSegments)
		{
			const char *kindStr =
				kind == WS_WAL_FILE_SEGMENT ? "segment" :
				kind == WS_WAL_FILE_PARTIAL ? "partial" :
				kind == WS_WAL_FILE_BACKUP ? "backup" : "history";

			char sizeStr[32] = { 0 };
			char mtimeStr[32] = { 0 };

			pretty_print_bytes(sizeStr, sizeof(sizeStr), size);
			format_utc(st.st_mtime, mtimeStr, sizeof(mtimeStr));

			fformat(stdout, "%-20s %-28s %-9s %-10s %s\n",
					clusterKey, entry->d_name, kindStr, sizeStr, mtimeStr);
		}
	}

	closedir(dir);

	return true;
}


/*
 * cli_list_wal_run prints, per cluster, WsWalStats's own aggregate counts
 * (the default: one pg_controldata-style "Label:  value" block per
 * cluster, chosen over a table for the same reason "cluster list
 * --upstream" is pivoted rather than tabular -- one cluster's worth of
 * facts read more naturally stacked than crammed into a row), or every
 * individual file, still a table, via scan_wal_dir()'s own printSegments
 * mode when segments is true.
 */
bool
cli_list_wal_run(const char *pgdata, const char *configFile,
				 const char *clusterFilter, bool segments)
{
	WsCluster *clusters = NULL;
	int clusterCount = 0;

	if (!load_pgdata_clusters(pgdata, configFile, clusterFilter, &clusters,
							  &clusterCount))
	{
		return false;
	}

	if (segments)
	{
		fformat(stdout, "%-20s %-28s %-9s %-10s %s\n",
				"CLUSTER", "FILE", "KIND", "SIZE", "MODIFIED");
		fformat(stdout, "%-20s %-28s %-9s %-10s %s\n",
				"--------------------", "----------------------------",
				"---------", "----------", "--------");
	}

	bool first = true;

	for (int i = 0; i < clusterCount; i++)
	{
		const WsCluster *cluster = &clusters[i];

		if (!cluster_matches_filter(cluster, clusterFilter))
		{
			continue;
		}

		WsWalStats stats = { 0 };

		if (!scan_wal_dir(cluster, &stats, segments, cluster->key))
		{
			log_warn("Could not read WAL directory for cluster \"%s\"", cluster->key);
			continue;
		}

		if (!segments)
		{
			char sizeStr[32] = { 0 };

			pretty_print_bytes(sizeStr, sizeof(sizeStr), stats.totalBytes);

			if (!first)
			{
				fformat(stdout, "\n");
			}
			first = false;

			fformat(stdout, "%-11s%s\n", "Cluster:", cluster->key);
			fformat(stdout, "%-11s%d\n", "Segments:", stats.segments);
			fformat(stdout, "%-11s%s\n", "Size:", sizeStr);
			fformat(stdout, "%-11s%s\n", "Oldest:",
					stats.oldest[0] != '\0' ? stats.oldest : "-");
			fformat(stdout, "%-11s%s\n", "Newest:",
					stats.newest[0] != '\0' ? stats.newest : "-");
			fformat(stdout, "%-11s%d\n", "History:", stats.history);
		}
	}

	clusters_free(clusters);

	return true;
}


/* -----------------------------------------------------------------------
 * pg_walserver list clusters|backups|wal [--cluster <name>] [--segments]
 * ----------------------------------------------------------------------- */

static char listPgdata[MAXPGPATH] = { 0 };
static char listConfigFile[MAXPGPATH] = { 0 };
static char listCluster[NAMEDATALEN + 16] = { 0 };
static bool listWalSegments = false;

static struct option listClustersLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "cluster", required_argument, NULL, 'c' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_list_clusters_getopt parses "pg_walserver list clusters"'s own flags into the
 * file-scope statics above.
 */
static int
cli_list_clusters_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(listPgdata);
	listConfigFile[0] = '\0';
	listCluster[0] = '\0';

	int c;

	while ((c = getopt_long(argc, argv, "D:f:c:", listClustersLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(listPgdata, optarg, sizeof(listPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(listConfigFile, optarg, sizeof(listConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(listCluster, optarg, sizeof(listCluster));
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
 * cli_list_clusters_command_run runs "pg_walserver list clusters" against the
 * options cli_list_clusters_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_list_clusters_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_clusters_run(listPgdata, listConfigFile, listCluster) ? 0 : 1);
}


/*
 * cli_list_backups_command_run runs "pg_walserver list backups" against the
 * options cli_list_backups_getopt parsed above, then exit()s with its own
 * result.
 */
static void
cli_list_backups_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_backups_run(listPgdata, listConfigFile, listCluster) ? 0 : 1);
}


static struct option listWalLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "cluster", required_argument, NULL, 'c' },
	{ "segments", no_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

/*
 * cli_list_wal_getopt parses "pg_walserver list wal"'s own flags into the
 * file-scope statics above.
 */
static int
cli_list_wal_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(listPgdata);
	listConfigFile[0] = '\0';
	listCluster[0] = '\0';
	listWalSegments = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:c:s", listWalLongOptions,
							NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(listPgdata, optarg, sizeof(listPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(listConfigFile, optarg, sizeof(listConfigFile));
				break;
			}

			case 'c':
			{
				strlcpy(listCluster, optarg, sizeof(listCluster));
				break;
			}

			case 's':
			{
				listWalSegments = true;
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
 * cli_list_wal_command_run runs "pg_walserver list wal" against the options
 * cli_list_wal_getopt parsed above, then exit()s with its own result.
 */
static void
cli_list_wal_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(cli_list_wal_run(listPgdata, listConfigFile, listCluster,
						  listWalSegments) ? 0 : 1);
}
