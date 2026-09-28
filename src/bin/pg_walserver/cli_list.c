/*
 * src/bin/pg_walserver/cli_list.c
 *   See cli_list.h.
 *
 *   LSN-range computation ("list clusters"): the start LSN of a route's
 *   currently covered WAL range comes straight from its latest base
 *   backup's own "backup_label" ("START WAL LOCATION"), read with
 *   cmd_base_backup.c's own read_backup_label() -- already parsed,
 *   already tested, no reason to duplicate it. The end LSN comes from
 *   wal_dir_scan.c's own wal_dir_find_latest(): a single opendir()/
 *   readdir() pass over the route's own directory picking out the
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
 *   write into a route's own directory (the embedded pull capturer on each
 *   completed segment, archive-wal/ARCHIVE_FILE on each push, the bootstrap-
 *   backup code on completion, archive-cleanup on removal). That was not
 *   implemented in this pass: wiring an incremental-cache update into four
 *   independent, already-shipped write paths, correctly and without risking
 *   a stale/inconsistent cache outliving a crash mid-update, is real,
 *   nontrivial plumbing that did not fit safely in the time available for
 *   this change. What IS implemented instead is the documented fallback:
 *   "list backups"/"list wal" compute their answer fresh, by scanning the
 *   route's own directory, on every invocation -- correct always, cached
 *   only for the lifetime of that one invocation (a single directory scan
 *   feeds every row printed for that route, never re-scanned per row). A
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
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_list.h"
#include "cli_archive_cleanup.h"
#include "cmd_base_backup.h"
#include "file_utils.h"
#include "log.h"
#include "pidfile.h"
#include "ps_state.h"
#include "routes.h"
#include "string_utils.h"
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


/* ---------------------------------------------------------------------
 * small formatting helpers, shared by all three "list" sub-commands
 * --------------------------------------------------------------------- */
static void
format_bytes(uint64_t bytes, char *dest, size_t destSize)
{
	double b = (double) bytes;
	const char *unit = "B";

	if (b >= 1024.0 * 1024 * 1024)
	{
		b /= 1024.0 * 1024 * 1024;
		unit = "GB";
	}
	else if (b >= 1024.0 * 1024)
	{
		b /= 1024.0 * 1024;
		unit = "MB";
	}
	else if (b >= 1024.0)
	{
		b /= 1024.0;
		unit = "KB";
	}

	sformat(dest, destSize, "%.1f%s", b, unit);
}


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
 * shared route loading/filtering
 * --------------------------------------------------------------------- */
static bool
load_pgdata_routes(const char *pgdata, const char *clusterFilter,
				   WsRoute **routesOut, int *countOut)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("--pgdata is required (or set the PGDATA environment "
				  "variable)");
		return false;
	}

	char routesPath[MAXPGPATH] = { 0 };

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini", pgdata);

	if (!routes_load(routesPath, routesOut, countOut))
	{
		log_error("Failed to parse \"%s\"", routesPath);
		return false;
	}

	if (clusterFilter != NULL && clusterFilter[0] != '\0')
	{
		if (routes_find_exact(*routesOut, *countOut, clusterFilter) == NULL)
		{
			log_error("No route \"%s\" in \"%s\"", clusterFilter, routesPath);
			routes_free(*routesOut);
			*routesOut = NULL;
			*countOut = 0;
			return false;
		}
	}

	return true;
}


static bool
route_matches_filter(const WsRoute *route, const char *clusterFilter)
{
	return clusterFilter == NULL || clusterFilter[0] == '\0' ||
		   streq(route->key, clusterFilter);
}


/*
 * read_latest_label reads "<routePath>/basebackups/.latest" -- the label of
 * the route's currently active backup, trimmed of its trailing newline.
 * Returns false (labelOut untouched) when the route has no backup yet.
 */
static bool
read_latest_label(const char *routePath, char *labelOut, size_t labelOutSize)
{
	char latestPath[MAXPGPATH] = { 0 };

	sformat(latestPath, sizeof(latestPath), "%s/basebackups/.latest", routePath);

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
 * capturer_running_for_route cross-references the ps state file (ps_state.h,
 * written by a running "serve") against routeKey. *knownOut is set to false
 * when "serve" is not running at all (the caller should print "n/a", not
 * "no": there is no capturer status to report either way), true otherwise
 * with the return value being the actual running/stopped answer.
 */
static bool
capturer_running_for_route(const char *pgdata, const char *routeKey,
						   bool *knownOut)
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

	for (int i = 0; i < state.capturerCount; i++)
	{
		if (streq(state.capturers[i].routeKey, routeKey))
		{
			*knownOut = true;
			return state.capturers[i].pid > 0 && kill(state.capturers[i].pid, 0) == 0;
		}
	}

	/* the route has no capturer entry at all: known, and definitely not
	 * running (either "capture = pull" isn't set, or it failed to start) */
	*knownOut = true;
	return false;
}


/* ---------------------------------------------------------------------
 * pg_walserver list clusters
 * --------------------------------------------------------------------- */
bool
cli_list_clusters_run(const char *pgdata, const char *clusterFilter)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!load_pgdata_routes(pgdata, clusterFilter, &routes, &routeCount))
	{
		return false;
	}

	if (routeCount == 0)
	{
		log_info("No routes configured in \"%s/pg_walserver.ini\"", pgdata);
		routes_free(routes);
		return true;
	}

	printf("%-20s %-8s %-9s %-9s %-22s %-22s\n", /* IGNORE-BANNED */
		   "CLUSTER", "BACKUP", "CAPTURE", "CAPTURER", "WAL START", "WAL END");
	printf("--------------------------------------------------------------" /* IGNORE-BANNED */
		   "------------------------------\n");

	for (int i = 0; i < routeCount; i++)
	{
		const WsRoute *route = &routes[i];

		if (!route_matches_filter(route, clusterFilter))
		{
			continue;
		}

		char backupLabel[NAMEDATALEN] = { 0 };
		bool haveBackup = read_latest_label(route->path, backupLabel,
											sizeof(backupLabel));

		char startLsn[32] = "-";

		if (haveBackup)
		{
			char backupDir[MAXPGPATH] = { 0 };
			char lsn[32] = { 0 };
			int timeline = 0;

			sformat(backupDir, sizeof(backupDir), "%s/basebackups/%s",
					route->path, backupLabel);

			if (read_backup_label(backupDir, lsn, sizeof(lsn), &timeline))
			{
				strlcpy(startLsn, lsn, sizeof(startLsn));
			}
		}

		char endLsn[32] = "-";
		uint32_t tli = 0;

		(void) wal_dir_find_latest(route, &tli, endLsn, sizeof(endLsn));

		bool known = false;
		bool running = capturer_running_for_route(pgdata, route->key, &known);
		const char *capturerStr = !route->capturePull ? "n/a" :
								  !known ? "n/a" : running ? "yes" : "no";

		printf("%-20s %-8s %-9s %-9s %-22s %-22s\n", /* IGNORE-BANNED */
			   route->key, haveBackup ? "yes" : "no",
			   route->capturePull ? "pull" : "none", capturerStr,
			   startLsn, endLsn);
	}

	routes_free(routes);

	return true;
}


/* ---------------------------------------------------------------------
 * pg_walserver list backups
 * --------------------------------------------------------------------- */
bool
cli_list_backups_run(const char *pgdata, const char *clusterFilter)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!load_pgdata_routes(pgdata, clusterFilter, &routes, &routeCount))
	{
		return false;
	}

	printf("%-20s %-28s %-22s %-10s %s\n", /* IGNORE-BANNED */
		   "CLUSTER", "LABEL", "TAKEN AT", "SIZE", "LATEST");
	printf("--------------------------------------------------------------" /* IGNORE-BANNED */
		   "------------------------------\n");

	for (int i = 0; i < routeCount; i++)
	{
		const WsRoute *route = &routes[i];

		if (!route_matches_filter(route, clusterFilter))
		{
			continue;
		}

		uint64_t segSize = ws_route_wal_segment_size(route);
		WsBackupInfo *backups = NULL;
		int backupCount = 0;

		if (!ws_backup_list_load(route->path, segSize, &backups, &backupCount))
		{
			log_warn("Could not read backups for route \"%s\"", route->key);
			continue;
		}

		char latestLabel[NAMEDATALEN] = { 0 };

		(void) read_latest_label(route->path, latestLabel, sizeof(latestLabel));

		for (int b = 0; b < backupCount; b++)
		{
			char takenAt[32] = { 0 };
			char size[32] = { 0 };

			format_utc(backups[b].takenAt, takenAt, sizeof(takenAt));
			format_bytes(directory_size(backups[b].dirPath), size, sizeof(size));

			printf("%-20s %-28s %-22s %-10s %s\n", /* IGNORE-BANNED */
				   route->key, backups[b].label, takenAt, size,
				   streq(backups[b].label, latestLabel) ? "yes" : "");
		}

		free(backups);
	}

	routes_free(routes);

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


static bool
scan_wal_dir(const WsRoute *route, WsWalStats *stats, bool printSegments,
			 const char *routeKey)
{
	memset(stats, 0, sizeof(WsWalStats));

	DIR *dir = opendir(route->path);

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

		sformat(entryPath, sizeof(entryPath), "%s/%s", route->path, entry->d_name);

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

			format_bytes(size, sizeStr, sizeof(sizeStr));
			format_utc(st.st_mtime, mtimeStr, sizeof(mtimeStr));

			printf("%-20s %-28s %-9s %-10s %s\n", /* IGNORE-BANNED */
				   routeKey, entry->d_name, kindStr, sizeStr, mtimeStr);
		}
	}

	closedir(dir);

	return true;
}


bool
cli_list_wal_run(const char *pgdata, const char *clusterFilter, bool segments)
{
	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!load_pgdata_routes(pgdata, clusterFilter, &routes, &routeCount))
	{
		return false;
	}

	if (segments)
	{
		printf("%-20s %-28s %-9s %-10s %s\n", /* IGNORE-BANNED */
			   "CLUSTER", "FILE", "KIND", "SIZE", "MODIFIED");
	}
	else
	{
		printf("%-20s %-9s %-10s %-24s %-24s %s\n", /* IGNORE-BANNED */
			   "CLUSTER", "SEGMENTS", "SIZE", "OLDEST", "NEWEST", "HISTORY");
	}

	printf("--------------------------------------------------------------" /* IGNORE-BANNED */
		   "------------------------------\n");

	for (int i = 0; i < routeCount; i++)
	{
		const WsRoute *route = &routes[i];

		if (!route_matches_filter(route, clusterFilter))
		{
			continue;
		}

		WsWalStats stats = { 0 };

		if (!scan_wal_dir(route, &stats, segments, route->key))
		{
			log_warn("Could not read WAL directory for route \"%s\"", route->key);
			continue;
		}

		if (!segments)
		{
			char sizeStr[32] = { 0 };

			format_bytes(stats.totalBytes, sizeStr, sizeof(sizeStr));

			printf("%-20s %-9d %-10s %-24s %-24s %d\n", /* IGNORE-BANNED */
				   route->key, stats.segments, sizeStr,
				   stats.oldest[0] != '\0' ? stats.oldest : "-",
				   stats.newest[0] != '\0' ? stats.newest : "-",
				   stats.history);
		}
	}

	routes_free(routes);

	return true;
}
