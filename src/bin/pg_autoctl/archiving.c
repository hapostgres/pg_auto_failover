/*
 * src/bin/pg_autoctl/archiving.c
 *     Implement archiving support for Postgres.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <stdbool.h>

#include "archiving.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "pgsetup.h"
#include "wal-g.h"


static bool ensure_absolute_wal_filename(const char *pgdata,
										 const char *filename,
										 char *wal);

static char * get_walfile_name(const char *filename);


/*
 * archive_wal prepares the archiving of a given WAL file, and archives it
 * using WAL-G.
 */
bool
archive_wal(Keeper *keeper, const char *config_filename, const char *wal)
{
	KeeperConfig *config = &(keeper->config);
	PostgresSetup *pgSetup = &(config->pgSetup);

	char wal_pathname[MAXPGPATH] = { 0 };

	if (!normalize_filename(pgSetup->pgdata, pgSetup->pgdata, MAXPGPATH))
	{
		/* errors have already been logged */
		return false;
	}

	if (!ensure_absolute_wal_filename(pgSetup->pgdata, wal, wal_pathname))
	{
		/* errors have already been logged */
		return false;
	}

	log_info("Archiving WAL file \"%s\" for "
			 "node %lld \"%s\" in formation \"%s\" and group %d",
			 get_walfile_name(wal_pathname),
			 (long long) keeper->state.current_node_id,
			 config->name,
			 config->formation,
			 config->groupId);

	return walg_wal_push(config_filename, wal_pathname);
}


/*
 * ensure_absolute_wal_filename gets the absolute pathname for the given
 * filename. When it's already an absolute, its value is copied to the wal
 * buffer. Otherwise, filename is appended to the pgdata value, and the result
 * is copied to the wal buffer.
 *
 * The destination char buffer wal should be at least MAXPGPATH long.
 */
static bool
ensure_absolute_wal_filename(const char *pgdata, const char *filename,
							 char *wal)
{
	if (filename[0] == '/')
	{
		if (!file_exists(filename))
		{
			log_error("WAL file \"%s\" does not exists", filename);
			return false;
		}

		strlcpy(wal, filename, MAXPGPATH);
	}
	else
	{
		/* if the provided filename is relative, find it in pg_wal */
		sformat(wal, MAXPGPATH, "%s/pg_wal/%s", pgdata, filename);

		if (!file_exists(wal))
		{
			log_error("WAL file \"%s\" does not exists", wal);
			return false;
		}
	}

	return true;
}


/*
 * get_walfile_name returns a pointer to the WAL filename when given the
 * absolute name of the file on-disk.
 */
static char *
get_walfile_name(const char *filename)
{
	if (filename == NULL)
	{
		return NULL;
	}

	char *ptr = strrchr(filename, '/');

	return ptr + 1;
}
