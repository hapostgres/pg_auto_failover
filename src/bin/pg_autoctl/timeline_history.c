/*
 * src/bin/pg_autoctl/timeline_history.c
 *   Reads the local node's own timeline history (from pg_wal/<tli>.history, not
 *   over a replication connection) and encodes it for publishing to the
 *   monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdlib.h>

#include "postgres_fe.h"

#include "file_utils.h"
#include "log.h"
#include "parson.h"
#include "timeline_history.h"


/*
 * keeper_fetch_local_timeline_history reads this node's own
 * pg_wal/%08X.history file for the given current timeline, without any
 * replication connection: this is what makes it possible to call on every
 * keeper tick, unconditionally of FSM state, cheaply.
 *
 * A node still on the very first timeline it has ever known has no history
 * file at all (nothing has forked yet): that's expected, not an error, and
 * results in a single-entry history (the current tip only).
 *
 * On return, system->timeline and system->timelines are populated exactly
 * as they would be by parsing a TIMELINE_HISTORY replication command result
 * (see parseTimeLineHistory), oldest known ancestor first, current tip last.
 */
bool
keeper_fetch_local_timeline_history(PostgresSetup *pgSetup,
									 uint32_t currentTLI,
									 IdentifySystem *system)
{
	char historyFileName[MAXPGPATH] = { 0 };
	char historyFilePath[MAXPGPATH] = { 0 };
	char *content = NULL;
	long fileSize = 0L;
	bool ok = false;

	system->timeline = currentTLI;

	sformat(historyFileName, sizeof(historyFileName),
			"%08X.history", currentTLI);

	join_path_components(historyFilePath, pgSetup->pgdata, "pg_wal");
	join_path_components(historyFilePath, historyFilePath, historyFileName);

	if (currentTLI <= 1 || !read_file_if_exists(historyFilePath,
												&content,
												&fileSize))
	{
		/*
		 * No history file: either we're still on timeline 1 (nothing has
		 * ever forked), or the file isn't there for some other reason. In
		 * both cases we still know our own current tip, we just don't know
		 * of any ancestor beyond it.
		 */
		content = strdup("");

		if (content == NULL)
		{
			log_error(ALLOCATION_FAILED_ERROR);
			return false;
		}
	}

	ok = parseTimeLineHistory(historyFilePath, content, system);

	free(content);

	return ok;
}


/*
 * timeline_history_to_json encodes a node's timeline history (as fetched by
 * keeper_fetch_local_timeline_history) as a JSON array of
 * {tli, parenttli, switchpoint} objects, oldest first, suitable for
 * pgautofailover.report_timeline_history()'s history jsonb argument.
 *
 * parenttli is 0 for the oldest entry (its parent, if any, predates
 * anything we know about locally). switchpoint is the LSN at which that
 * entry's timeline began (entry->begin), which is exactly the LSN at which
 * the parent timeline was superseded.
 *
 * Returns a malloc'd string the caller must free with
 * json_free_serialized_string(), or NULL on error.
 */
char *
timeline_history_to_json(IdentifySystem *system)
{
	JSON_Value *jsArray = json_value_init_array();
	JSON_Array *array = json_value_get_array(jsArray);

	for (int i = 0; i < system->timelines.count; i++)
	{
		TimeLineHistoryEntry *entry = &(system->timelines.history[i]);

		uint32_t parentTLI =
			(i == 0) ? 0 : system->timelines.history[i - 1].tli;

		char switchpoint[PG_LSN_MAXLENGTH] = { 0 };

		sformat(switchpoint, sizeof(switchpoint), "%X/%X",
				(uint32_t) (entry->begin >> 32),
				(uint32_t) entry->begin);

		JSON_Value *jsEntry = json_value_init_object();
		JSON_Object *jsObj = json_value_get_object(jsEntry);

		json_object_set_number(jsObj, "tli", entry->tli);
		json_object_set_number(jsObj, "parenttli", parentTLI);
		json_object_set_string(jsObj, "switchpoint", switchpoint);

		json_array_append_value(array, jsEntry);
	}

	char *serialized = json_serialize_to_string(jsArray);

	json_value_free(jsArray);

	return serialized;
}
