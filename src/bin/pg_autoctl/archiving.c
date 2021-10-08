/*
 * src/bin/pg_autoctl/archiving.c
 *     Implement archiving support for Postgres.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdbool.h>
#include <sys/stat.h>

#include "archiving.h"
#include "defaults.h"
#include "file_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgsetup.h"
#include "system_utils.h"
#include "wal-g.h"

/* that's part of Postgres --includedir-server installation */
#include "common/md5.h"

static bool ensure_absolute_wal_filename(const char *pgdata,
										 const char *filename,
										 char *wal);

static bool prepareWalFile(const char *formation,
						   int groupId,
						   int64_t nodeId,
						   const char *wal_pathname,
						   MonitorWALFile *walFile);

static char * get_walfile_name(const char *filename);
static bool get_walfile_size(const char *filename, uint64_t *size);
static bool get_walfile_md5(const char *filename, char *md5);


/*
 * archive_wal prepares the archiving of a given WAL file, and archives it
 * using WAL-G.
 */
bool
archive_wal(Keeper *keeper, const char *config_filename, const char *wal)
{
	Monitor *monitor = &(keeper->monitor);
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

	MonitorWALFile walFile = { 0 };

	if (!prepareWalFile(config->formation,
						config->groupId,
						keeper->state.current_node_id,
						wal_pathname,
						&walFile))
	{
		/* errors have already been logged */
		return false;
	}

	char sizeStr[BUFSIZE] = { 0 };

	(void) pretty_print_bytes(sizeStr, sizeof(sizeStr), walFile.filesize);

	log_info("Archiving WAL file \"%s\" for node %lld \"%s\" "
			 "in formation \"%s\" and group %d",
			 walFile.filename,
			 (long long) walFile.nodeId,
			 config->name,
			 walFile.formation,
			 walFile.groupId);

	log_debug("WAL file \"%s\" has size %s and md5 \"%s\"",
			  walFile.filename,
			  sizeStr,
			  walFile.md5);

	/*
	 * Now proceed to archiving the WAL file, unless another node is already
	 * active doing it, or unless the WAL has already been archived previously.
	 */
	MonitorWALFile registeredWalFile = { 0 };

	if (!monitor_register_wal(monitor,
							  config->formation,
							  config->groupId,
							  keeper->state.current_node_id,
							  walFile.filename,
							  walFile.filesize,
							  walFile.md5,
							  &registeredWalFile))
	{
		/* errors have already been logged */
		return false;
	}

	/* if the monitor returns a different entry for the walFile, we skip */
	if (walFile.nodeId != registeredWalFile.nodeId)
	{
		if (IS_EMPTY_STRING_BUFFER(registeredWalFile.finishTime))
		{
			log_warn("WAL file \"%s\" is being archived by node %lld",
					 registeredWalFile.filename,
					 (long long) registeredWalFile.nodeId);
		}
		else
		{
			log_info("WAL file \"%s\" has already been archived by node %lld",
					 registeredWalFile.filename,
					 (long long) registeredWalFile.nodeId);
		}
	}

	if (strcmp(walFile.md5, registeredWalFile.md5) != 0)
	{
		log_error("Computed MD5 for local WAL file is \"%s\", and the monitor "
				  "already has a registration for this WAL file by node %lld "
				  "with MD5 \"%s\", started archiving at %s",
				  walFile.md5,
				  (long long) registeredWalFile.nodeId,
				  registeredWalFile.md5,
				  registeredWalFile.startTime);
		return false;
	}

	/* if we got the registration at our nodeId, now archive the WAL */
	if (walFile.nodeId == registeredWalFile.nodeId &&
		strcmp(walFile.md5, registeredWalFile.md5) == 0 &&
		IS_EMPTY_STRING_BUFFER(registeredWalFile.finishTime))
	{
		bool success = walg_wal_push(config_filename, wal_pathname);

		if (success)
		{
			if (!monitor_finish_wal(monitor,
									registeredWalFile.formation,
									registeredWalFile.groupId,
									registeredWalFile.filename,
									&registeredWalFile))
			{
				/* errors have already been logged */
				return false;
			}

			log_info("Archived WAL file \"%s\" successfully at %s",
					 registeredWalFile.filename,
					 registeredWalFile.finishTime);
		}

		return success;
	}
	else
	{
		log_info("WAL file \"%s\" with MD5 \"%s\" was finished achiving at %s",
				 registeredWalFile.filename,
				 registeredWalFile.md5,
				 registeredWalFile.finishTime);
	}

	return true;
}


/*
 * prepareWalFile prepares a walFile register by computing a WAL file MD5
 * checksum and size.
 */
static bool
prepareWalFile(const char *formation,
			   int groupId,
			   int64_t nodeId,
			   const char *wal_pathname,
			   MonitorWALFile *walFile)
{
	strlcpy(walFile->formation, formation, sizeof(walFile->formation));

	walFile->groupId = groupId;
	walFile->nodeId = nodeId;

	char *walFileName = get_walfile_name(wal_pathname);

	strlcpy(walFile->filename, walFileName, sizeof(walFile->filename));

	if (!get_walfile_md5(wal_pathname, walFile->md5))
	{
		/* errors have already been logged */
		return false;
	}

	if (!get_walfile_size(wal_pathname, &(walFile->filesize)))
	{
		/* errors have already been logged */
		return false;
	}

	return true;
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


/*
 * get_walfile_size returns the size of the given WAL file in bytes. We expect
 * a file of 16MB of course, though recent Postgres versions might be used with
 * custom WAL file sizes.
 */
static bool
get_walfile_size(const char *filename, uint64_t *size)
{
	struct stat buf;

	if (stat(filename, &buf) != 0)
	{
		log_error("Failed to get size of file \"%s\": %m", filename);
		return false;
	}

	*size = buf.st_size;

	return true;
}


/*
 * get_walfile_md5 computes the md5 of the contents of the given filename and
 * fills the md5 buffer (which must be at least 33 bytes).
 */
static bool
get_walfile_md5(const char *filename, char *md5)
{
	char *contents = NULL;
	long size = 0L;

	if (!read_file(filename, &contents, &size))
	{
		/* errors have already been logged */
		return false;
	}

	if (!pg_md5_hash(contents, size, md5))
	{
		log_error("Failed to compute MD5 of file \"%s\"", filename);
		return false;
	}

	return true;
}
