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
 * Every well-known file pg_walserver itself ever writes/reads directly
 * under --pgdata -- see cli_root.c's own cli_serve_run() (routes/HBA/
 * passwd/TLS/pidfile paths), tls.c (--ssl-ca-file's own default path,
 * "ca.crt"), and ps_state.h (the ps state file).
 */
static const char *wellKnownFiles[] = {
	"pg_walserver.ini",
	"pg_walserver_hba.conf",
	"pg_walserver_passwd",
	"server.crt",
	"server.key",
	"ca.crt",
	"pg_walserver.pid",
	WS_PS_STATE_FILENAME,
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


bool
cli_ls_run(const char *pgdata)
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

	for (int i = 0; wellKnownFiles[i] != NULL; i++)
	{
		char path[MAXPGPATH] = { 0 };

		sformat(path, sizeof(path), "%s/%s", pgdata, wellKnownFiles[i]);

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
			   wellKnownFiles[i], exists ? "yes" : "no", sizeStr, mtimeStr);
	}

	return true;
}
