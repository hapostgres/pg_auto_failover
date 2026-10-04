/*
 * src/bin/pg_walserver/cmd_replication_slot.c
 *   See cmd_replication_slot.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>

#include "postgres_fe.h"

#include "cmd_replication_slot.h"
#include "file_utils.h"
#include "framing.h"
#include "log.h"
#include "ws_util.h"
#include "wal_dir_scan.h"

/* a valid slot name is at most WS_SLOT_NAME_LEN_MAX (NAMEDATALEN-1) */
#define WS_SLOT_NAME_LEN_MAX 63

/* slots per route: each is a file in the route's directory */
#define WS_MAX_SLOTS_PER_ROUTE 64

#define WS_SLOT_PREFIX ".slot_"


/*
 * Slot names are restricted like PostgreSQL's ReplicationSlotValidateName():
 * [a-z0-9_]{1,63}. The name ends up in a file name, so this is also what
 * keeps a client from writing anywhere but its route's directory.
 */
static bool
slot_name_is_safe(const char *name)
{
	size_t len = strlen(name);

	if (len == 0 || len > WS_SLOT_NAME_LEN_MAX)
	{
		return false;
	}

	for (const char *p = name; *p; p++)
	{
		if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
			  *p == '_'))
		{
			return false;
		}
	}

	return true;
}


/* is this directory entry a slot file (".slot_" + a valid name)? */
static bool
entry_is_slot(const char *entryName)
{
	return strncmp(entryName, WS_SLOT_PREFIX, strlen(WS_SLOT_PREFIX)) == 0 &&
		   slot_name_is_safe(entryName + strlen(WS_SLOT_PREFIX));
}


static int
count_slots(const char *routePath)
{
	DIR *dir = opendir(routePath);
	int count = 0;

	if (dir == NULL)
	{
		return 0;
	}

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (entry_is_slot(entry->d_name))
		{
			count++;
		}
	}

	closedir(dir);

	return count;
}


static void
slot_marker_path(const WsRoute *route, const char *slotName, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/.slot_%s", route->path, slotName);
}


/*
 * slot_lock_path is deliberately a *different* file from slot_marker_
 * path()'s own ".slot_<name>" -- see ws_replication_slot_try_lock()'s
 * own comment for why: a file that write_file_atomic() ever replaces
 * via rename() cannot double as a stable flock() target.
 */
static void
slot_lock_path(const WsRoute *route, const char *slotName, char *dest, size_t destSize)
{
	sformat(dest, destSize, "%s/.slot_%s.lock", route->path, slotName);
}


/*
 * ws_replication_slot_try_lock serializes concurrent streaming attempts
 * against the same slot, the same "one active connection per slot" rule
 * a real PostgreSQL walsender enforces -- without it, two concurrent
 * START_REPLICATION sessions naming the same slot could each advance and
 * persist its own "restart_lsn" independently, and whichever persists
 * last wins regardless of which one is actually further ahead, silently
 * moving the slot's own floor backward (still only ever a retention
 * cost -- cli_archive_cleanup.c only ever keeps *more* WAL for a smaller
 * restart_lsn, never removes something a real reader still needs -- but
 * a real violation of "restart_lsn only ever moves forward" all the
 * same).
 *
 * This locks a dedicated, never-renamed ".slot_<name>.lock" file, never
 * slot_marker_path()'s own ".slot_<name>" -- that file's own periodic
 * rewrite (ws_replication_slot_update_restart_lsn(), a temp file plus
 * rename()) swaps in a fresh inode each time, so a lock taken against it
 * would only ever protect whichever incarnation happened to be open at
 * lock time, not the slot as a whole. The lock file itself is never
 * rewritten, only opened, so this problem doesn't apply to it.
 *
 * flock()'s own release-on-close() semantics (including on an unclean
 * process exit -- a crashed or killed session releases it for free, no
 * separate staleness detection needed) is exactly the property wanted
 * here: each connection is its own forked child (accept_loop.c), so
 * "the lock is held for as long as this process has the fd open" is
 * already "for as long as this one streaming session is alive".
 *
 * Returns an open fd to keep for as long as the lock should be held
 * (release with ws_replication_slot_unlock()), or -1 -- already locked
 * by another session, or some other error, either way already logged --
 * when the lock could not be acquired.
 */
int
ws_replication_slot_try_lock(const WsRoute *route, const char *slotName)
{
	if (route == NULL || route->path[0] == '\0' || !slot_name_is_safe(slotName))
	{
		return -1;
	}

	char path[MAXPGPATH];

	slot_lock_path(route, slotName, path, sizeof(path));

	int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600); /* IGNORE-BANNED */

	if (fd < 0)
	{
		log_warn("Failed to open replication slot lock file \"%s\": %m", path);
		return -1;
	}

	if (flock(fd, LOCK_EX | LOCK_NB) != 0)
	{
		if (errno == EWOULDBLOCK)
		{
			log_warn("Replication slot \"%s\" is already active for "
					 "another session", slotName);
		}
		else
		{
			log_warn("Failed to lock \"%s\": %m", path);
		}

		close(fd);
		return -1;
	}

	return fd;
}


/*
 * ws_replication_slot_unlock releases a lock ws_replication_slot_try_
 * lock() returned -- close() alone already releases the flock(), this
 * just gives the release its own named call site rather than a bare
 * close() wherever a caller happens to return. Safe to call with fd < 0
 * (nothing was ever locked, e.g. slotName was empty -- no SLOT clause
 * given at all).
 */
void
ws_replication_slot_unlock(int fd)
{
	if (fd >= 0)
	{
		close(fd);
	}
}


/*
 * read_restart_lsn_file reads path's own "restart_lsn=<lsn>\n" line into
 * lsnOut -- the parsing cmd_read_replication_slot() and every exported
 * accessor below share, factored out to one place.
 */
static bool
read_restart_lsn_file(const char *path, char *lsnOut, size_t lsnOutSize)
{
	char *contents = NULL;
	long fileSize = 0;

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
	{
		return false;
	}

	bool found = false;
	const char *prefix = "restart_lsn=";
	char *line = strstr(contents, prefix);

	if (line != NULL)
	{
		line += strlen(prefix);

		char *nl = strchr(line, '\n');

		if (nl != NULL)
		{
			*nl = '\0';
		}

		strlcpy(lsnOut, line, lsnOutSize);
		found = true;
	}

	free(contents);

	return found;
}


bool
ws_replication_slot_exists(const WsRoute *route, const char *slotName)
{
	if (route == NULL || route->path[0] == '\0' || !slot_name_is_safe(slotName))
	{
		return false;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	return file_exists(path);
}


bool
ws_replication_slot_read_restart_lsn(const WsRoute *route, const char *slotName,
									 char *lsnOut, size_t lsnOutSize)
{
	if (route == NULL || route->path[0] == '\0' || !slot_name_is_safe(slotName))
	{
		return false;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	return read_restart_lsn_file(path, lsnOut, lsnOutSize);
}


bool
ws_replication_slot_update_restart_lsn(const WsRoute *route, const char *slotName,
									   const char *lsn)
{
	if (route == NULL || route->path[0] == '\0' || !slot_name_is_safe(slotName))
	{
		return false;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	/* never create a slot as a side effect of streaming -- only an
	 * already-existing slot's own restart_lsn can be advanced */
	if (!file_exists(path))
	{
		return false;
	}

	char contents[128];

	sformat(contents, sizeof(contents), "restart_lsn=%s\n", lsn);

	return write_file_atomic(contents, strlen(contents), path);
}


bool
ws_replication_slot_oldest_restart_lsn(const WsRoute *route, uint64_t segSize,
									   char *slotNameOut, size_t slotNameOutSize,
									   char *lsnOut, size_t lsnOutSize)
{
	if (route == NULL || route->path[0] == '\0')
	{
		return false;
	}

	DIR *dir = opendir(route->path);

	if (dir == NULL)
	{
		return false;
	}

	bool found = false;
	uint64_t oldestSegno = 0;
	char oldestLsn[32] = { 0 };
	char oldestName[NAMEDATALEN] = { 0 };

	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL)
	{
		if (!entry_is_slot(entry->d_name))
		{
			continue;
		}

		char path[MAXPGPATH];

		sformat(path, sizeof(path), "%s/%s", route->path, entry->d_name);

		char lsn[32] = { 0 };

		if (!read_restart_lsn_file(path, lsn, sizeof(lsn)))
		{
			continue;
		}

		uint64_t segno;

		if (!wal_lsn_to_segno(lsn, segSize, &segno))
		{
			continue;
		}

		if (!found || segno < oldestSegno)
		{
			found = true;
			oldestSegno = segno;
			strlcpy(oldestLsn, lsn, sizeof(oldestLsn));
			strlcpy(oldestName, entry->d_name + strlen(WS_SLOT_PREFIX),
					sizeof(oldestName));
		}
	}

	closedir(dir);

	if (found)
	{
		strlcpy(slotNameOut, oldestName, slotNameOutSize);
		strlcpy(lsnOut, oldestLsn, lsnOutSize);
	}

	return found;
}


void
cmd_create_replication_slot(int sock, const WsRoute *route,
							const char *slotName, bool temporary, bool isLogical)
{
	if (route == NULL || route->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	/* TEMPORARY and RESERVE_WAL/legacy options are accepted by the grammar
	 * but not enforced yet -- see this file's own header comment on
	 * retention. Silence the unused-parameter warning until they are. */
	(void) temporary;

	if (!slot_name_is_safe(slotName))
	{
		ws_send_error_response(sock, "42602",
							   "invalid replication slot name: use only "
							   "lower case letters, numbers, and the "
							   "underscore character (63 at most)");
		return;
	}

	if (isLogical)
	{
		ws_send_error_response(sock, "0A000",
							   "only physical replication slots are supported");
		return;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	/* an existing slot is left as it is, never reset by a second CREATE */
	if (file_exists(path))
	{
		ws_send_error_response(sock, "42710",
							   "replication slot already exists");
		return;
	}

	if (count_slots(route->path) >= WS_MAX_SLOTS_PER_ROUTE)
	{
		ws_send_error_response(sock, "53400",
							   "all replication slots of this route are in "
							   "use");
		return;
	}

	char consistentPoint[32] = "0/0";
	uint32_t timeline;

	if (!wal_position_cache_read(route->path, &timeline, consistentPoint,
								 sizeof(consistentPoint)))
	{
		(void) wal_dir_find_latest(route, &timeline, consistentPoint,
								   sizeof(consistentPoint));
	}

	char contents[128];

	sformat(contents, sizeof(contents), "restart_lsn=%s\n", consistentPoint);

	/* temp file + rename: a reader never sees a half written slot */
	if (!write_file_atomic(contents, strlen(contents), path))
	{
		log_error("Failed to write replication slot marker \"%s\"", path);
		ws_send_error_response(sock, "58030", "failed to persist the replication slot");
		return;
	}

	WsColumn columns[] = {
		{ "slot_name", WS_TEXTOID, -1 },
		{ "consistent_point", WS_TEXTOID, -1 },
		{ "snapshot_name", WS_TEXTOID, -1 },
		{ "output_plugin", WS_TEXTOID, -1 },
	};

	const char *values[] = { slotName, consistentPoint, NULL, NULL };

	if (ws_send_row_description(sock, columns, 4) &&
		ws_send_data_row(sock, values, 4))
	{
		ws_send_command_complete(sock, "CREATE_REPLICATION_SLOT");
	}
}


void
cmd_read_replication_slot(int sock, const WsRoute *route, const char *slotName)
{
	if (route == NULL || route->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	if (!slot_name_is_safe(slotName))
	{
		ws_send_error_response(sock, "42602", "invalid replication slot name");
		return;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	WsColumn columns[] = {
		{ "slot_type", WS_TEXTOID, -1 },
		{ "restart_lsn", WS_TEXTOID, -1 },
		{ "restart_tli", WS_INT8OID, 8 },
	};

	if (!file_exists(path))
	{
		/* matches real Postgres: slot doesn't exist -> one all-NULL row,
		 * not an ErrorResponse -- the client checks PQgetisnull() itself */
		const char *nullValues[] = { NULL, NULL, NULL };

		if (ws_send_row_description(sock, columns, 3) &&
			ws_send_data_row(sock, nullValues, 3))
		{
			ws_send_command_complete(sock, "READ_REPLICATION_SLOT");
		}

		return;
	}

	/* the file exists: report it even if its own "restart_lsn=" line
	 * could somehow not be parsed (never happens in practice -- nothing
	 * but this file's own code ever writes a slot marker -- but a
	 * genuinely malformed file is still a real slot, not a missing one) */
	char restartLsn[32] = "0/0";

	(void) read_restart_lsn_file(path, restartLsn, sizeof(restartLsn));

	uint32_t timeline = 1;
	char discardLsn[32] = { 0 };

	if (!wal_position_cache_read(route->path, &timeline, discardLsn,
								 sizeof(discardLsn)))
	{
		(void) wal_dir_find_latest(route, &timeline, discardLsn,
								   sizeof(discardLsn));
	}

	char timelineStr[16];

	sformat(timelineStr, sizeof(timelineStr), "%u", timeline);

	const char *values[] = { "physical", restartLsn, timelineStr };

	if (ws_send_row_description(sock, columns, 3) &&
		ws_send_data_row(sock, values, 3))
	{
		ws_send_command_complete(sock, "READ_REPLICATION_SLOT");
	}
}


/*
 * DROP_REPLICATION_SLOT slot_name [ WAIT ]: pg_receivewal --drop-slot and
 * friends send it. A slot that does not exist is an error, as in
 * PostgreSQL.
 */
void
cmd_drop_replication_slot(int sock, const WsRoute *route,
						  const char *slotName, bool wait)
{
	if (route == NULL || route->path[0] == '\0')
	{
		ws_send_error_response(sock, "58P01",
							   "no WAL cache directory configured for this route");
		return;
	}

	/* WAIT is accepted by the grammar; a slot marker file drop is always
	 * synchronous here, so there's nothing to actually wait for */
	(void) wait;

	if (!slot_name_is_safe(slotName))
	{
		ws_send_error_response(sock, "42602", "invalid replication slot name");
		return;
	}

	char path[MAXPGPATH];

	slot_marker_path(route, slotName, path, sizeof(path));

	if (!file_exists(path))
	{
		ws_send_error_response(sock, "42704",
							   "replication slot does not exist");
		return;
	}

	/*
	 * Refuse to drop a slot a START_REPLICATION session is actively
	 * streaming against, the same as real PostgreSQL -- ws_replication_
	 * slot_try_lock() (cmd_replication_slot.h) is the same lock that
	 * session itself holds for as long as it's alive.
	 */
	int lockFd = ws_replication_slot_try_lock(route, slotName);

	if (lockFd < 0)
	{
		ws_send_error_response(sock, "55006",
							   "replication slot is active for another session");
		return;
	}

	bool ok = unlink(path) == 0;

	if (!ok)
	{
		log_error("Failed to remove replication slot file \"%s\": %m", path);
	}

	char lockPath[MAXPGPATH];

	slot_lock_path(route, slotName, lockPath, sizeof(lockPath));
	ws_replication_slot_unlock(lockFd);
	(void) unlink(lockPath);

	if (!ok)
	{
		ws_send_error_response(sock, "58030",
							   "failed to drop the replication slot");
		return;
	}

	ws_send_command_complete(sock, "DROP_REPLICATION_SLOT");
}
