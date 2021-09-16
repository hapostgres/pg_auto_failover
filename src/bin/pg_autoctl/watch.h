/*
 * src/bin/pg_autoctl/watch.h
 *     Implementation of a CLI to show events, states, and URI from the
 *     pg_auto_failover monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef WATCH_H
#define WATCH_H

#include <ncurses.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "defaults.h"
#include "monitor.h"
#include "nodestate_utils.h"

/* a ring-buffer of the last monitor events */
#define EVENTS_BUFFER_COUNT 24

/* share a context between the update and render functions */
typedef struct WatchContext
{
	int rows;
	int cols;

	Monitor monitor;
	char formation[NAMEDATALEN];
	int groupId;
	int number_sync_standbys;

	CurrentNodeStateArray nodesArray;
	MonitorEventsArray eventsArray;
} WatchContext;

void watch_main_loop(WatchContext *context);
void watch_init_window(WatchContext *context);
void watch_end_window(WatchContext *context);

bool watch_update(WatchContext *context);
bool watch_render(WatchContext *context);

#endif  /* WATCH_H */
