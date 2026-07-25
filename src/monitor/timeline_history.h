/*
 * src/monitor/timeline_history.h
 *   Ancestry-aware comparisons over pgautofailover.node_timeline_history,
 *   used by the election to tell "genuinely comparable, just behind" apart
 *   from "on a forked, incomparable timeline" -- see group_state_machine.c.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#pragma once

#include "access/xlogdefs.h"
#include "nodes/pg_list.h"

#include "node_metadata.h"

typedef struct TimelineHistoryEdge
{
	int tli;
	int parentTli;
	XLogRecPtr switchpointLSN;
} TimelineHistoryEdge;

extern List * FetchGroupTimelineHistory(char *formationId, int groupId);

extern bool TimelineIsAncestor(List *history, int candidateTli,
							   int referenceTli, XLogRecPtr *outSwitchpoint);

extern int GetAcceptedTimeline(char *formationId, int groupId);

extern void ResolveAcceptedTimeline(char *formationId, int groupId);

extern List * FilterNodesByTimelineAncestry(List *nodeList,
											char *formationId, int groupId,
											int *outReferenceTli);
