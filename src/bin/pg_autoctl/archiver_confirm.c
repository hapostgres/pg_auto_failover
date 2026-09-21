/*
 * src/bin/pg_autoctl/archiver_confirm.c
 *   See archiver_confirm.h.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "archiver_confirm.h"
#include "cli_root.h"
#include "defaults.h"
#include "file_utils.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "string_utils.h"

#define ARCHIVE_CONFIRM_CACHE_FILENAME "pg_autoctl.archive-confirm"
#define ARCHIVE_CONFIRM_TIMEOUT 3

/* a segment name that no archiver ever reports: probes for archiver membership */
#define ARCHIVE_CONFIRM_PROBE_NAME "000000000000000000000000"


/*
 * archiver_confirm_is_wal_segment returns true for exactly 24 hex digits.
 */
bool
archiver_confirm_is_wal_segment(const char *name)
{
	if (name == NULL || strlen(name) != 24)
	{
		return false;
	}

	for (int i = 0; i < 24; i++)
	{
		if (!isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


/*
 * Cache: one line "archivers=<n> checkedat=<epoch>", written after every
 * successful monitor answer, read when the monitor can't be reached.
 */
static void
archive_confirm_write_cache(const char *pgdata, int archivers)
{
	char path[MAXPGPATH] = { 0 };
	char line[BUFSIZE] = { 0 };

	join_path_components(path, pgdata, ARCHIVE_CONFIRM_CACHE_FILENAME);
	int len = sformat(line, sizeof(line), "archivers=%d checkedat=%lld\n",
					  archivers, (long long) time(NULL));

	if (!write_file_atomic(line, len, path))
	{
		log_debug("Failed to write \"%s\"", path);
	}
}


/* returns the cached archivers count, or -1 when nothing usable is cached */
static int
archive_confirm_read_cache(const char *pgdata)
{
	char path[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long size = 0L;
	int archivers = -1;

	join_path_components(path, pgdata, ARCHIVE_CONFIRM_CACHE_FILENAME);

	if (!read_file_if_exists(path, &contents, &size) || size == 0)
	{
		return -1;
	}

	if (strncmp(contents, "archivers=", 10) == 0 &&
		(contents[10] == '0' || contents[10] == '1' ||
		 (contents[10] >= '2' && contents[10] <= '9')))
	{
		archivers = atoi(contents + 10);     /* IGNORE-BANNED */
	}

	free(contents);

	return archivers;
}


/*
 * archiver_confirm_run implements the archive_command policy.
 */
int
archiver_confirm_run(const char *pgdata, const char *walFileName)
{
	KeeperConfig config = { 0 };
	Monitor monitor = { 0 };
	bool confirmed = false;

	if (!archiver_confirm_is_wal_segment(walFileName))
	{
		/*
		 * Timeline history, .backup and .partial files are informational:
		 * the archiver's pg_receivewal captures timeline history itself.
		 */
		log_debug("archive confirm: \"%s\" is not a WAL segment, skipping",
				  walFileName);
		return 0;
	}

	strlcpy(config.pgSetup.pgdata, pgdata, MAXPGPATH);

	if (!SetConfigFilePath(&(config.pathnames), pgdata) ||
		!keeper_config_read_file_skip_pgsetup(&config, true))
	{
		log_warn("archive confirm %s: cannot read the pg_autoctl "
				 "configuration for \"%s\", will retry", walFileName, pgdata);
		return 1;
	}

	if (config.monitorDisabled)
	{
		return 0;
	}

	pgconnect_timeout = ARCHIVE_CONFIRM_TIMEOUT;

	if (monitor_init(&monitor, config.monitor_pguri))
	{
		/* single attempt, the caller (Postgres) is our retry loop */
		pgsql_set_retry_policy(&(monitor.pgsql.retryPolicy),
							   ARCHIVE_CONFIRM_TIMEOUT, 0, 1000, 500);

		if (monitor_archive_confirmed(&monitor, config.formation,
									  config.groupId, walFileName,
									  &confirmed))
		{
			int archivers = 0;

			if (confirmed)
			{
				/* true means either no archiver, or a confirmed segment */
				bool probe = false;

				archivers =
					(monitor_archive_confirmed(&monitor, config.formation,
											   config.groupId,
											   ARCHIVE_CONFIRM_PROBE_NAME,
											   &probe) && probe) ? 0 : 1;
			}
			else
			{
				archivers = 1;
			}

			pgsql_finish(&monitor.pgsql);
			archive_confirm_write_cache(pgdata, archivers);

			if (!confirmed)
			{
				log_info("archive confirm %s: not yet captured by the archiver",
						 walFileName);
			}

			return confirmed ? 0 : 1;
		}

		pgsql_finish(&monitor.pgsql);
	}

	/* monitor failure: fall back on the last known answer */
	int archivers = archive_confirm_read_cache(pgdata);

	if (archivers == 0)
	{
		log_warn("archive confirm %s: monitor unreachable, last known state "
				 "is no archiver, accepting", walFileName);
		return 0;
	}

	log_warn("archive confirm %s: monitor unreachable and %s, will retry",
			 walFileName,
			 archivers > 0 ? "archivers exist" : "no cached answer");

	return 1;
}
