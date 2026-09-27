/*
 * src/bin/pg_walsender/cmd_replication_slot.c
 *   See cmd_replication_slot.h.
 *
 * Licensed under the PostgreSQL License.
 *
 */

#include <errno.h>
#include <dirent.h>
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

	char *contents = NULL;
	long fileSize = 0;

	WsColumn columns[] = {
		{ "slot_type", WS_TEXTOID, -1 },
		{ "restart_lsn", WS_TEXTOID, -1 },
		{ "restart_tli", WS_INT8OID, 8 },
	};

	if (!read_file_if_exists(path, &contents, &fileSize) || contents == NULL)
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

	char restartLsn[32] = "0/0";
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

		strlcpy(restartLsn, line, sizeof(restartLsn));
	}

	free(contents);

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

	if (unlink(path) != 0)
	{
		if (errno == ENOENT)
		{
			ws_send_error_response(sock, "42704",
								   "replication slot does not exist");
		}
		else
		{
			log_error("Failed to remove replication slot file \"%s\": %m", path);
			ws_send_error_response(sock, "58030",
								   "failed to drop the replication slot");
		}

		return;
	}

	ws_send_command_complete(sock, "DROP_REPLICATION_SLOT");
}
