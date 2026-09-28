/*
 * src/bin/pg_walserver/cli_ls.c
 *   See cli_ls.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "postgres_fe.h"

#include "cli_ls.h"
#include "file_utils.h"
#include "log.h"
#include "ps_state.h"
#include "string_utils.h"

/*
 * The runtime tier: files "serve" itself keeps current while running,
 * never hand-provisioned -- see cli_root.c's own cli_serve_run() (the
 * pidfile path) and ps_state.h (the ps state file). Always shown: this
 * is the tier actually worth a glance.
 */
static const char *runtimeFiles[] = {
	"pg_walserver.pid",
	WS_PS_STATE_FILENAME,
	NULL
};

/*
 * The config/credential/certificate tier: written once, by the operator
 * or by "setup"/"create-cert", and rarely changing thereafter -- see
 * cli_root.c's own cli_serve_run() (routes/HBA/passwd/TLS paths) and
 * tls.c (--ssl-ca-file's own default path, "ca.crt"). Only shown with
 * --config: confirming these still exist tells an operator little day
 * to day, so they don't clutter the default output.
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
print_file_rows(const char *pgdata, const char *const *files)
{
	for (int i = 0; files[i] != NULL; i++)
	{
		char path[MAXPGPATH] = { 0 };

		sformat(path, sizeof(path), "%s/%s", pgdata, files[i]);

		struct stat st;
		bool exists = stat(path, &st) == 0;

		char sizeStr[32] = "-";
		char mtimeStr[32] = "-";

		if (exists)
		{
			format_bytes((uint64_t) st.st_size, sizeStr, sizeof(sizeStr));

			struct tm tmVal = { 0 };

			gmtime_r(&st.st_mtime, &tmVal);
			strftime(mtimeStr, sizeof(mtimeStr), "%Y-%m-%dT%H:%M:%SZ", &tmVal);
		}

		printf("%-24s %-7s %-10s %s\n", /* IGNORE-BANNED */
			   files[i], exists ? "yes" : "no", sizeStr, mtimeStr);
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

	printf("%-24s %-7s %-10s %s\n", "FILE", "EXISTS", "SIZE", "MODIFIED"); /* IGNORE-BANNED */
	printf("------------------------------------------------------" /* IGNORE-BANNED */
		   "----------\n");

	print_file_rows(pgdata, runtimeFiles);

	if (includeConfigFiles)
	{
		print_file_rows(pgdata, configFiles);
	}
	else
	{
		printf("\n(config/credential/certificate files omitted; " /* IGNORE-BANNED */
			   "pass --config to include them)\n");
	}

	return true;
}
