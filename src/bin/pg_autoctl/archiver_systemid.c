/*
 * src/bin/pg_autoctl/archiver_systemid.c
 *   See archiver_systemid.h.
 *
 * service_archiver_maybe_persist_systemid() (service_archiver.c) is the
 * only writer: once per membership, ever (a Postgres cluster's system
 * identifier never changes across its own lifetime). Readers -- pg_
 * receivewal's own WalSegmentClosedHook (service_archiver_pgreceivewal_
 * ctl.c, running inside the forked pg_receivewal child) and the periodic
 * scanner (service_archiver_wal_scanner.c) -- both need this value to tag
 * every WAL-notify message they send (archiver_wal_notify_send()) with the
 * cluster incarnation it came from, and neither has any other way to learn
 * it: an archiver has no real pg_control of its own to read it from
 * directly (see service_archiver_maybe_persist_systemid()'s own comment).
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>

#include "postgres_fe.h"

#include "archiver_systemid.h"

#include "file_utils.h"
#include "string_utils.h"

void
archiver_systemid_path(KeeperConfig *config, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/archiver-systemid", config->pgSetup.pgdata);
}


/*
 * archiver_systemid_read reads back what service_archiver_maybe_persist_
 * systemid() wrote -- returns false (leaving *systemIdentifier untouched)
 * if the file doesn't exist yet (this membership hasn't learned its
 * group's system identifier from the monitor yet) or can't be parsed,
 * both expected, transient states early in a membership's life, not
 * errors: callers are expected to simply try again later.
 */
bool
archiver_systemid_read(KeeperConfig *config, uint64_t *systemIdentifier)
{
	char path[MAXPGPATH] = { 0 };

	archiver_systemid_path(config, path, sizeof(path));

	return archiver_systemid_read_from_path(path, systemIdentifier);
}


/*
 * archiver_systemid_read_from_path is archiver_systemid_read()'s own
 * implementation, taking an already-computed path directly -- for callers
 * that only ever have a plain KeeperConfig* before fork()ing into a
 * context where computing it fresh isn't convenient (pg_receivewal's own
 * WalSegmentClosedHook, service_archiver_pgreceivewal_ctl.c, which like
 * that file's own archiverWalNotifySocketPath caches this path once in
 * the parent, before the fork whose child actually calls this).
 */
bool
archiver_systemid_read_from_path(const char *path, uint64_t *systemIdentifier)
{
	char *contents = NULL;
	long fileSize = 0;

	if (!file_exists(path) || !read_file(path, &contents, &fileSize) ||
		contents == NULL)
	{
		return false;
	}

	uint64_t value = strtoull(contents, NULL, 10); /* IGNORE-BANNED */

	free(contents);

	if (value == 0)
	{
		return false;
	}

	*systemIdentifier = value;
	return true;
}


/*
 * archiver_walsegsize_read returns the WAL segment size pg_receivewal
 * recorded (RetrieveWalSegSize() against the primary) or the 16MiB default
 * when the file is absent or not a power of two between 1MiB and 1GiB.
 */
uint64_t
archiver_walsegsize_read(const char *membershipDir)
{
	char path[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long fileSize = 0;
	uint64_t value = 16 * 1024 * 1024;

	sformat(path, sizeof(path), "%s/archiver-walsegsize", membershipDir);

	if (!file_exists(path) || !read_file(path, &contents, &fileSize) ||
		contents == NULL)
	{
		return value;
	}

	uint64_t parsed = strtoull(contents, NULL, 10); /* IGNORE-BANNED */

	free(contents);

	if (parsed >= (1024 * 1024) && parsed <= (1024 * 1024 * 1024) &&
		(parsed & (parsed - 1)) == 0)
	{
		value = parsed;
	}

	return value;
}


/* true when the first 24 characters of name are all hex digits */
bool
archiver_wal_name_is_hex24(const char *name)
{
	for (int i = 0; i < 24; i++)
	{
		if (name[i] == '\0' || !isxdigit((unsigned char) name[i]))
		{
			return false;
		}
	}

	return true;
}


uint64_t
archiver_wal_name_segno(const char *walFileName, uint64_t segsize)
{
	char logIdHex[9] = { 0 };
	char segHex[9] = { 0 };

	memcpy(logIdHex, walFileName + 8, 8); /* IGNORE-BANNED */
	memcpy(segHex, walFileName + 16, 8); /* IGNORE-BANNED */

	uint64_t logId = strtoull(logIdHex, NULL, 16); /* IGNORE-BANNED */
	uint64_t seg = strtoull(segHex, NULL, 16); /* IGNORE-BANNED */

	return logId * (((uint64_t) 0x100000000) / segsize) + seg;
}


bool
archiver_wal_floor_read(const char *membershipDir, uint64_t *segno)
{
	char path[MAXPGPATH] = { 0 };
	char *contents = NULL;
	long fileSize = 0;

	sformat(path, sizeof(path), "%s/archiver-wal-floor", membershipDir);

	if (!file_exists(path) || !read_file(path, &contents, &fileSize) ||
		contents == NULL)
	{
		return false;
	}

	*segno = strtoull(contents, NULL, 10); /* IGNORE-BANNED */
	free(contents);

	return true;
}


bool
archiver_wal_floor_write(const char *membershipDir, uint64_t segno)
{
	char path[MAXPGPATH] = { 0 };
	char contents[32] = { 0 };

	sformat(path, sizeof(path), "%s/archiver-wal-floor", membershipDir);

	int size = sformat(contents, sizeof(contents), "%" PRIu64 "\n", segno);

	return write_file_atomic(contents, size, path);
}
