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

typedef enum
{
	WATCH_MOVE_FOCUS_NONE = 0,
	WATCH_MOVE_FOCUS_LEFT,
	WATCH_MOVE_FOCUS_RIGHT,
	WATCH_MOVE_FOCUS_HOME,
	WATCH_MOVE_FOCUS_END,
	WATCH_MOVE_FOCUS_UP,
	WATCH_MOVE_FOCUS_DOWN
} WatchMoveFocus;

/* compute max size of items to display for events */
typedef struct MonitorEventsHeaders
{
	int maxEventIdSize;
	int maxEventTimeSize;
	int maxEventNodeNameSize;
	int maxEventDescSize;
} MonitorEventsHeaders;

#define EVENTS_BUFFER_COUNT 80

/* share a context between the update and render functions */
typedef struct WatchContext
{
	/* state of the display */
	int rows;
	int cols;
	int selectedRow;
	int selectedArea;           /* area 1: node states, area 2: node events */
	int startCol;
	WatchMoveFocus move;

	/* internal state */
	bool initialized;
	bool cookedMode;
	bool shouldExit;            /* true when q or F1 have been pressed */
	bool couldContactMonitor;

	/* parameters used to fetch the data we display */
	Monitor monitor;
	char formation[NAMEDATALEN];
	int groupId;
	int number_sync_standbys;

	/* data to display */
	CurrentNodeStateArray nodesArray;
	MonitorEventsArray eventsArray;
	MonitorEventsHeaders eventsHeaders;
} WatchContext;

void cli_watch_main_loop(WatchContext *context);
void cli_watch_init_window(WatchContext *context);
void cli_watch_end_window(WatchContext *context);

bool cli_watch_update(WatchContext *context, int step);
bool cli_watch_render(WatchContext *context, WatchContext *previous);

#endif  /* WATCH_H */
