/*
 * src/bin/pg_walserver/cli_ls.c
 *   See cli_ls.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <getopt.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "postgres_fe.h"

#include "commandline.h"

#include "backup_list.h"
#include "cli_common.h"
#include "cli_ls.h"
#include "cli_root.h"
#include "file_utils.h"
#include "log.h"
#include "ps_state.h"
#include "clusters.h"
#include "string_utils.h"
#include "system_utils.h"
#include "wal_dir_scan.h"

/*
 * The config/credential/certificate tier: written once, by the operator
 * or by "setup"/"create-cert", and rarely changing thereafter -- see
 * cli_root.c's own cli_serve_run() (clusters/HBA/passwd/TLS paths) and
 * tls.c (--ssl-ca-file's own default path, "ca.crt"). Only shown with
 * --all: confirming these still exist tells an operator little day
 * to day, so they don't clutter the default output, which is the real
 * per-cluster storage summary below instead.
 */
static const char *configFiles[] = {
	"pg_walserver_hba.conf",
	"pg_walserver_passwd",
	"server.crt",
	"server.key",
	"ca.crt",
	NULL
};

/*
 * WsClusterFootprint is one cluster's own on-disk footprint: how many base
 * backups it holds and their combined real size, how many WAL segments
 * (complete ones; a ".partial" in progress is counted separately, never
 * as a complete segment) and their combined size, and when its most
 * recent base backup was taken -- see this file's own header comment for
 * why exactly these fields, out of everything scan_cluster_footprint()
 * could report.
 */
typedef struct WsClusterFootprint
{
	int backupCount;
	uint64_t backupBytes;
	time_t lastBackupAt;
	int walSegments;
	int walPartials;
	uint64_t walBytes;
} WsClusterFootprint;

/* local helpers */
static void format_utc(time_t t, char *dest, size_t destSize);
static uint64_t directory_size(const char *path);
static void scan_cluster_footprint(const WsCluster *cluster, WsClusterFootprint *out);
static void print_config_files_row(const char *label, const char *path);
static void print_config_files(const char *pgdata, const char *configPath);

static int cli_ls_getopt(int argc, char **argv);
static void cli_ls_command_run(int argc, char **argv);


/* -----------------------------------------------------------------------
 * pg_walserver ls --pgdata <path>
 * ----------------------------------------------------------------------- */

static char lsPgdata[MAXPGPATH] = { 0 };
static char lsConfigFile[MAXPGPATH] = { 0 };
static bool lsIncludeConfigFiles = false;

static struct option lsLongOptions[] = {
	{ "pgdata", required_argument, NULL, 'D' },
	{ "config", required_argument, NULL, 'f' },
	{ "all", no_argument, NULL, 'a' },
	{ NULL, 0, NULL, 0 }
};

CommandLine ls_command =
	make_command("ls",
				 "Per-cluster storage summary: base backups, WAL, disk usage",
				 "--pgdata <path> [--config <path>] [--all]",
				 "  --pgdata    this instance's own top-level storage root "
				 "(defaults to\n"
				 "              PGDATA)\n"
				 "  --config    where the config file itself lives "
				 "(defaults to\n"
				 "              <pgdata>/pg_walserver.ini, or "
				 "PG_WALSERVER_CONFIG_FILE)\n"
				 "  --all, -a   list the config/credential/certificate "
				 "files instead\n"
				 "              (rarely change, rarely interesting day "
				 "to day)\n",
				 cli_ls_getopt, cli_ls_command_run);


/*
 * format_utc renders t as an ISO-8601 UTC timestamp ("YYYY-MM-DDTHH:MM:SSZ"),
 * or "-" when t is unset (<= 0, e.g. a cluster with no base backup yet).
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
 * under path -- one base backup's own real size on disk, the same helper
 * cli_list.c's own "list backups" uses for the identical purpose.
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


/*
 * scan_cluster_footprint computes one cluster's own WsClusterFootprint: a real
 * base backup enumeration (ws_backup_list_load(), backup_list.h -- the
 * same one "list backups"/"archive-cleanup" use, so a size or count
 * shown here always agrees with those) plus a real WAL directory scan
 * (ws_wal_dir_classify_filename(), wal_dir_scan.h -- the same
 * classification "list wal" uses). Never fails outright: a cluster with no
 * backups yet, or whose directory cannot be opened at all (not yet
 * created), simply reports all-zero, exactly like a freshly "setup" cluster
 * that "serve" has not started for yet.
 */
static void
scan_cluster_footprint(const WsCluster *cluster, WsClusterFootprint *out)
{
	memset(out, 0, sizeof(WsClusterFootprint));

	uint64_t segSize = ws_cluster_wal_segment_size(cluster);
	WsBackupInfo *backups = NULL;
	int backupCount = 0;

	if (ws_backup_list_load(cluster->path, segSize, &backups, &backupCount))
	{
		out->backupCount = backupCount;

		for (int b = 0; b < backupCount; b++)
		{
			out->backupBytes += directory_size(backups[b].dirPath);

			if (backups[b].takenAt > out->lastBackupAt)
			{
				out->lastBackupAt = backups[b].takenAt;
			}
		}

		free(backups);
	}

	DIR *dir = opendir(cluster->path);

	if (dir == NULL)
	{
		return;
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		char segment[25] = { 0 };
		WsWalFileKind kind = ws_wal_dir_classify_filename(entry->d_name, segment);

		if (kind != WS_WAL_FILE_SEGMENT && kind != WS_WAL_FILE_PARTIAL)
		{
			continue;
		}

		char entryPath[MAXPGPATH] = { 0 };

		sformat(entryPath, sizeof(entryPath), "%s/%s", cluster->path, entry->d_name);

		struct stat st;

		if (stat(entryPath, &st) == 0)
		{
			out->walBytes += (uint64_t) st.st_size;
		}

		if (kind == WS_WAL_FILE_SEGMENT)
		{
			out->walSegments++;
		}
		else
		{
			out->walPartials++;
		}
	}

	closedir(dir);
}


/*
 * print_config_files_row prints one row of the --all tier's own table
 * for label/path -- shared between the config file itself (whose
 * resolved path is not always "<pgdata>/<name>", see config_file_path())
 * and every other well-known file, which still always is.
 */
static void
print_config_files_row(const char *label, const char *path)
{
	struct stat st;
	bool exists = stat(path, &st) == 0;

	char sizeStr[32] = "-";
	char mtimeStr[32] = "-";

	if (exists)
	{
		pretty_print_bytes(sizeStr, sizeof(sizeStr), (uint64_t) st.st_size);
		format_utc(st.st_mtime, mtimeStr, sizeof(mtimeStr));
	}

	fformat(stdout, "%-24s %-7s %-10s %s\n",
			label, exists ? "yes" : "no", sizeStr, mtimeStr);
}


/*
 * print_config_files prints the --all tier's own table: one row per
 * well-known config/credential/certificate file, whether it exists, its
 * size, and its last-modified time. Every file except the config file
 * itself lives at a fixed "<pgdata>/<name>" path; the config file's own
 * row instead follows configPath, wherever config_file_path() resolved
 * it to (not always under pgdata, see clusters.h's own comment).
 */
static void
print_config_files(const char *pgdata, const char *configPath)
{
	fformat(stdout, "%-24s %-7s %-10s %s\n", "FILE", "EXISTS", "SIZE", "MODIFIED");
	fformat(stdout, "%-24s %-7s %-10s %s\n",
			"------------------------", "-------", "----------", "--------");

	print_config_files_row("pg_walserver.ini", configPath);

	for (int i = 0; configFiles[i] != NULL; i++)
	{
		char path[MAXPGPATH] = { 0 };

		sformat(path, sizeof(path), "%s/%s", pgdata, configFiles[i]);

		print_config_files_row(configFiles[i], path);
	}
}


/*
 * ws_ls_report prints, by default, one row per configured cluster: its own
 * base backup count/combined size, WAL segment count/combined size, and
 * most recent base backup timestamp. With includeConfigFiles (--all),
 * prints the config/credential/certificate file tier instead (whether
 * each well-known file exists, its size, its last-modified time) -- see
 * this file's own header comment for why these are two separate views,
 * not combined into one. configFile (--config), when given, overrides
 * where the config file itself lives, independent of pgdata -- see
 * config_file_path()'s own comment, clusters.h; the --all tier's own row
 * for it follows that same resolved path, not always
 * "<pgdata>/pg_walserver.ini". Always returns true: no clusters configured
 * yet, or a cluster with nothing on disk yet, are ordinary states to
 * report, never a failure.
 */
bool
ws_ls_report(const char *pgdata, const char *configFile, bool includeConfigFiles)
{
	if (includeConfigFiles && (pgdata == NULL || pgdata[0] == '\0'))
	{
		/*
		 * The --all tier lists well-known files at fixed "<pgdata>/<name>"
		 * paths, not just the config file itself -- pgdata is genuinely
		 * needed here, unlike the default table below.
		 */
		log_error("--pgdata is required for --all (or set the PGDATA "
				  "environment variable)");
		return false;
	}

	if ((pgdata == NULL || pgdata[0] == '\0') &&
		(configFile == NULL || configFile[0] == '\0'))
	{
		log_error("--pgdata or --config is required (or set the PGDATA "
				  "environment variable)");
		return false;
	}

	char clustersPath[MAXPGPATH] = { 0 };

	config_file_path(pgdata, configFile, clustersPath, sizeof(clustersPath));

	if (includeConfigFiles)
	{
		print_config_files(pgdata, clustersPath);
		return true;
	}

	WsCluster *clusters = NULL;
	int clusterCount = 0;

	if (!clusters_load(clustersPath, &clusters, &clusterCount) || clusterCount == 0)
	{
		fformat(stdout, "No clusters configured yet under \"%s\" -- see "
						"\"pg_walserver cluster register\".\n", clustersPath);
		clusters_free(clusters);
		return true;
	}

	fformat(stdout, "%-20s %-8s %-12s %-10s %-10s %-11s %s\n",
			"CLUSTER", "BACKUPS", "BACKUP SIZE", "WAL FILES", "WAL SIZE",
			"TOTAL SIZE", "LAST BACKUP");
	fformat(stdout, "%-20s %-8s %-12s %-10s %-10s %-11s %s\n",
			"--------------------", "--------", "------------", "----------",
			"----------", "-----------", "-----------");

	for (int i = 0; i < clusterCount; i++)
	{
		WsClusterFootprint fp = { 0 };

		scan_cluster_footprint(&clusters[i], &fp);

		char backupSize[32] = { 0 };
		char walSize[32] = { 0 };
		char totalSize[32] = { 0 };
		char lastBackup[32] = { 0 };
		char walFiles[32] = { 0 };

		pretty_print_bytes(backupSize, sizeof(backupSize), fp.backupBytes);
		pretty_print_bytes(walSize, sizeof(walSize), fp.walBytes);
		pretty_print_bytes(totalSize, sizeof(totalSize), fp.backupBytes + fp.walBytes);
		format_utc(fp.lastBackupAt, lastBackup, sizeof(lastBackup));

		if (fp.walPartials > 0)
		{
			sformat(walFiles, sizeof(walFiles), "%d+%d", fp.walSegments,
					fp.walPartials);
		}
		else
		{
			sformat(walFiles, sizeof(walFiles), "%d", fp.walSegments);
		}

		fformat(stdout, "%-20s %-8d %-12s %-10s %-10s %-11s %s\n",
				clusters[i].key, fp.backupCount, backupSize, walFiles, walSize,
				totalSize, lastBackup);
	}

	clusters_free(clusters);

	fformat(stdout, "\n(config/credential/certificate files omitted; "
					"pass --all to list those instead)\n");

	return true;
}


/*
 * cli_ls_getopt parses "pg_walserver ls"'s own flags into the
 * file-scope statics above.
 */
static int
cli_ls_getopt(int argc, char **argv)
{
	optind = 0;
	ws_prefill_pgdata_from_env(lsPgdata);
	lsConfigFile[0] = '\0';
	lsIncludeConfigFiles = false;

	int c;

	while ((c = getopt_long(argc, argv, "D:f:a", lsLongOptions, NULL)) != -1)
	{
		switch (c)
		{
			case 'D':
			{
				strlcpy(lsPgdata, optarg, sizeof(lsPgdata));
				break;
			}

			case 'f':
			{
				strlcpy(lsConfigFile, optarg, sizeof(lsConfigFile));
				break;
			}

			case 'a':
			{
				lsIncludeConfigFiles = true;
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
 * cli_ls_command_run runs "pg_walserver ls" against the options cli_ls_getopt
 * parsed above, then exit()s with its own result.
 */
static void
cli_ls_command_run(int argc, char **argv)
{
	(void) argc;
	(void) argv;

	exit(ws_ls_report(lsPgdata, lsConfigFile, lsIncludeConfigFiles) ? 0 : 1);
}
