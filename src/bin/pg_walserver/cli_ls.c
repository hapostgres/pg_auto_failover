/*
 * src/bin/pg_walserver/cli_ls.c
 *   See cli_ls.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_ls.h"
#include "cli_archive_cleanup.h"
#include "file_utils.h"
#include "log.h"
#include "ps_state.h"
#include "routes.h"
#include "string_utils.h"
#include "wal_dir_scan.h"

/*
 * The config/credential/certificate tier: written once, by the operator
 * or by "setup"/"create-cert", and rarely changing thereafter -- see
 * cli_root.c's own cli_serve_run() (routes/HBA/passwd/TLS paths) and
 * tls.c (--ssl-ca-file's own default path, "ca.crt"). Only shown with
 * --config: confirming these still exist tells an operator little day
 * to day, so they don't clutter the default output, which is the real
 * per-cluster storage summary below instead.
 */
static const char *configFiles[] = {
	"pg_walserver.ini",
	"pg_walserver_hba.conf",
	"pg_walserver_passwd",
	"server.crt",
	"server.key",
	"ca.crt",
	NULL
};


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
 * WsRouteFootprint is one route's own on-disk footprint: how many base
 * backups it holds and their combined real size, how many WAL segments
 * (complete ones; a ".partial" in progress is counted separately, never
 * as a complete segment) and their combined size, and when its most
 * recent base backup was taken -- see this file's own header comment for
 * why exactly these fields, out of everything scan_route_footprint()
 * could report.
 */
typedef struct WsRouteFootprint
{
	int backupCount;
	uint64_t backupBytes;
	time_t lastBackupAt;
	int walSegments;
	int walPartials;
	uint64_t walBytes;
} WsRouteFootprint;


/*
 * scan_route_footprint computes one route's own WsRouteFootprint: a real
 * base backup enumeration (ws_backup_list_load(), cli_archive_cleanup.h --
 * the same one "list backups"/"archive-cleanup" use, so a size or count
 * shown here always agrees with those) plus a real WAL directory scan
 * (ws_wal_dir_classify_filename(), wal_dir_scan.h -- the same
 * classification "list wal" uses). Never fails outright: a route with no
 * backups yet, or whose directory cannot be opened at all (not yet
 * created), simply reports all-zero, exactly like a freshly "setup" route
 * that "serve" has not started for yet.
 */
static void
scan_route_footprint(const WsRoute *route, WsRouteFootprint *out)
{
	memset(out, 0, sizeof(WsRouteFootprint));

	uint64_t segSize = ws_route_wal_segment_size(route);
	WsBackupInfo *backups = NULL;
	int backupCount = 0;

	if (ws_backup_list_load(route->path, segSize, &backups, &backupCount))
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

	DIR *dir = opendir(route->path);

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

		sformat(entryPath, sizeof(entryPath), "%s/%s", route->path, entry->d_name);

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


static void
print_config_files(const char *pgdata)
{
	printf("%-24s %-7s %-10s %s\n", "FILE", "EXISTS", "SIZE", "MODIFIED"); /* IGNORE-BANNED */
	printf("------------------------------------------------------" /* IGNORE-BANNED */
		   "----------\n");

	for (int i = 0; configFiles[i] != NULL; i++)
	{
		char path[MAXPGPATH] = { 0 };

		sformat(path, sizeof(path), "%s/%s", pgdata, configFiles[i]);

		struct stat st;
		bool exists = stat(path, &st) == 0;

		char sizeStr[32] = "-";
		char mtimeStr[32] = "-";

		if (exists)
		{
			format_bytes((uint64_t) st.st_size, sizeStr, sizeof(sizeStr));
			format_utc(st.st_mtime, mtimeStr, sizeof(mtimeStr));
		}

		printf("%-24s %-7s %-10s %s\n", /* IGNORE-BANNED */
			   configFiles[i], exists ? "yes" : "no", sizeStr, mtimeStr);
	}
}


bool
cli_ls_run(const char *pgdata, bool includeConfigFiles)
{
	if (pgdata == NULL || pgdata[0] == '\0')
	{
		log_error("--pgdata is required (or set the PGDATA environment "
				  "variable)");
		return false;
	}

	if (includeConfigFiles)
	{
		print_config_files(pgdata);
		return true;
	}

	char routesPath[MAXPGPATH] = { 0 };

	sformat(routesPath, sizeof(routesPath), "%s/pg_walserver.ini", pgdata);

	WsRoute *routes = NULL;
	int routeCount = 0;

	if (!routes_load(routesPath, &routes, &routeCount) || routeCount == 0)
	{
		printf("No routes configured yet under \"%s\" -- see " /* IGNORE-BANNED */
			   "\"pg_walserver setup\".\n", pgdata);
		routes_free(routes);
		return true;
	}

	printf("%-20s %-8s %-12s %-10s %-10s %-11s %s\n", /* IGNORE-BANNED */
		   "CLUSTER", "BACKUPS", "BACKUP SIZE", "WAL FILES", "WAL SIZE",
		   "TOTAL SIZE", "LAST BACKUP");
	printf("--------------------------------------------------------------" /* IGNORE-BANNED */
		   "----------------------------------------\n");

	for (int i = 0; i < routeCount; i++)
	{
		WsRouteFootprint fp = { 0 };

		scan_route_footprint(&routes[i], &fp);

		char backupSize[32] = { 0 };
		char walSize[32] = { 0 };
		char totalSize[32] = { 0 };
		char lastBackup[32] = { 0 };
		char walFiles[32] = { 0 };

		format_bytes(fp.backupBytes, backupSize, sizeof(backupSize));
		format_bytes(fp.walBytes, walSize, sizeof(walSize));
		format_bytes(fp.backupBytes + fp.walBytes, totalSize, sizeof(totalSize));
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

		printf("%-20s %-8d %-12s %-10s %-10s %-11s %s\n", /* IGNORE-BANNED */
			   routes[i].key, fp.backupCount, backupSize, walFiles, walSize,
			   totalSize, lastBackup);
	}

	routes_free(routes);

	printf("\n(config/credential/certificate files omitted; " /* IGNORE-BANNED */
		   "pass --config to list those instead)\n");

	return true;
}
