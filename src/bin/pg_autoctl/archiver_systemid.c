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
