/*-------------------------------------------------------------------------
 *
 * src/monitor/timeline_history.c
 *
 * Ancestry-aware comparisons over pgautofailover.node_timeline_history.
 *
 * Every Postgres timeline has exactly one parent and one fork LSN, so the
 * union of every node's known history forms a single tree (in the normal,
 * non-split-brain case, a single chain). This lets the election tell
 * "genuinely comparable, just behind" apart from "on a forked, incomparable
 * timeline" without any node needing to contact any other node -- there is
 * no elected primary at report_lsn time to check against, which is exactly
 * why this has to live here, centrally, on the monitor.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"

#include "access/xact.h"
#include "access/xlogdefs.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/pg_lsn.h"
#include "utils/timestamp.h"

#include "node_metadata.h"
#include "notifications.h"
#include "timeline_history.h"


/*
 * FetchGroupTimelineHistory returns the union of every node's reported
 * timeline history for the given (formation, group), as a list of
 * TimelineHistoryEdge, deduplicated: honest nodes always agree on the facts
 * (a timeline's parent and switchpoint are physical properties of that
 * timeline, not opinions), so a plain DISTINCT collapses every node's
 * repeated report of the same edge into one.
 */
List *
FetchGroupTimelineHistory(char *formationId, int groupId)
{
	List *history = NIL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = { TEXTOID, INT4OID };
	Datum argValues[] = {
		CStringGetTextDatum(formationId),
		Int32GetDatum(groupId)
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		"SELECT DISTINCT h.tli, h.parenttli, h.switchpoint_lsn "
		"  FROM pgautofailover.node_timeline_history h "
		"  JOIN pgautofailover.node n ON n.nodeid = h.nodeid "
		" WHERE n.formationid = $1 AND n.groupid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes,
										  argValues, NULL, false, 0);

	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from pgautofailover.node_timeline_history");
	}

	MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

	for (uint64 rowNumber = 0; rowNumber < SPI_processed; rowNumber++)
	{
		HeapTuple heapTuple = SPI_tuptable->vals[rowNumber];
		TupleDesc tupdesc = SPI_tuptable->tupdesc;
		bool isNull = false;

		TimelineHistoryEdge *edge =
			(TimelineHistoryEdge *) palloc0(sizeof(TimelineHistoryEdge));

		edge->tli = DatumGetInt32(
			SPI_getbinval(heapTuple, tupdesc, 1, &isNull));
		edge->parentTli = DatumGetInt32(
			SPI_getbinval(heapTuple, tupdesc, 2, &isNull));
		edge->switchpointLSN = DatumGetLSN(
			SPI_getbinval(heapTuple, tupdesc, 3, &isNull));

		history = lappend(history, edge);
	}

	MemoryContextSwitchTo(spiContext);

	SPI_finish();

	return history;
}


/*
 * FindTimelineHistoryEdge returns the edge describing the given tli, or
 * NULL when it doesn't appear in history at all (unknown ancestry).
 */
static TimelineHistoryEdge *
FindTimelineHistoryEdge(List *history, int tli)
{
	ListCell *cell = NULL;

	foreach(cell, history)
	{
		TimelineHistoryEdge *edge = (TimelineHistoryEdge *) lfirst(cell);

		if (edge->tli == tli)
		{
			return edge;
		}
	}

	return NULL;
}


/*
 * TimelineIsAncestor returns true when candidateTli is referenceTli itself,
 * or a genuine ancestor of it: walking referenceTli's parent chain (via
 * history) reaches candidateTli. When it returns true and outSwitchpoint is
 * non-NULL, *outSwitchpoint is set to the LSN at which candidateTli was
 * superseded (InvalidXLogRecPtr when candidateTli == referenceTli, i.e.
 * there's no switchpoint to speak of).
 *
 * Postgres timelines are created by promotion, always numbered higher than
 * their parent, so walking strictly decreasing tli values can never cycle;
 * the list_length(history) bound below is a defensive backstop against
 * malformed data, not something normal operation can reach.
 */
bool
TimelineIsAncestor(List *history, int candidateTli, int referenceTli,
				   XLogRecPtr *outSwitchpoint)
{
	if (candidateTli == referenceTli)
	{
		if (outSwitchpoint != NULL)
		{
			*outSwitchpoint = InvalidXLogRecPtr;
		}

		return true;
	}

	int currentTli = referenceTli;
	int maxSteps = list_length(history) + 1;

	for (int steps = 0; steps < maxSteps; steps++)
	{
		TimelineHistoryEdge *edge = FindTimelineHistoryEdge(history, currentTli);

		if (edge == NULL)
		{
			/* we don't know currentTli's ancestry beyond this point */
			return false;
		}

		if (edge->parentTli == candidateTli)
		{
			if (outSwitchpoint != NULL)
			{
				*outSwitchpoint = edge->switchpointLSN;
			}

			return true;
		}

		if (edge->parentTli <= 0 || edge->parentTli >= currentTli)
		{
			/* reached the root, or malformed/cyclic data: stop */
			return false;
		}

		currentTli = edge->parentTli;
	}

	return false;
}


/*
 * GetAcceptedTimeline returns the currently pinned tli for the given
 * (formation, group), from the most recent unresolved row in
 * pgautofailover.accepted_timeline, or 0 when there is none (the normal
 * case, always, absent an active fork).
 */
int
GetAcceptedTimeline(char *formationId, int groupId)
{
	int acceptedTli = 0;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = { TEXTOID, INT4OID };
	Datum argValues[] = {
		CStringGetTextDatum(formationId),
		Int32GetDatum(groupId)
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		"SELECT accepted_tli "
		"  FROM pgautofailover.accepted_timeline "
		" WHERE formationid = $1 AND groupid = $2 AND resolved_at IS NULL "
		" ORDER BY decided_at DESC "
		" LIMIT 1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes,
										  argValues, NULL, false, 0);

	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from pgautofailover.accepted_timeline");
	}

	MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

	if (SPI_processed > 0)
	{
		bool isNull = false;
		HeapTuple heapTuple = SPI_tuptable->vals[0];

		acceptedTli = DatumGetInt32(
			SPI_getbinval(heapTuple, SPI_tuptable->tupdesc, 1, &isNull));

		if (isNull)
		{
			acceptedTli = 0;
		}
	}

	MemoryContextSwitchTo(spiContext);

	SPI_finish();

	return acceptedTli;
}


/*
 * FilterNodesByTimelineAncestry returns the subset of nodeList whose
 * reportedTLI is genuinely comparable to the group's reference lineage --
 * either pinned explicitly via pgautofailover.accepted_timeline, or, in the
 * normal case (no pin), auto-detected as the branch containing the highest
 * reportedTLI among nodeList itself (the most recently promoted-to
 * timeline is the most likely real continuation).
 *
 * Nodes outside that lineage are excluded, not deprioritized: they simply
 * don't appear in the returned list, exactly as if they hadn't reported at
 * all. This is deliberate -- a node that has genuinely diverged can never
 * become comparable no matter how long the election waits for it, so
 * counting it as "missing" would block the group forever; excluding it
 * lets the remaining, genuinely comparable candidates proceed normally
 * (see #683). If excluding a diverged node happens to leave zero
 * candidates, the caller's existing "not enough candidates yet" handling
 * already does the right, graceful thing.
 *
 * *outReferenceTli is set to the reference tli used (0 when nodeList had no
 * reported tli to work from at all, in which case the input is returned
 * unfiltered -- there's nothing to filter against yet).
 */
List *
FilterNodesByTimelineAncestry(List *nodeList, char *formationId, int groupId,
							  int *outReferenceTli)
{
	ListCell *cell = NULL;
	int referenceTli = GetAcceptedTimeline(formationId, groupId);

	if (referenceTli == 0)
	{
		foreach(cell, nodeList)
		{
			AutoFailoverNode *node = (AutoFailoverNode *) lfirst(cell);

			if (node->reportedTLI > referenceTli)
			{
				referenceTli = node->reportedTLI;
			}
		}
	}

	if (outReferenceTli != NULL)
	{
		*outReferenceTli = referenceTli;
	}

	if (referenceTli == 0)
	{
		/* nobody has reported a timeline yet: nothing to filter against */
		return nodeList;
	}

	List *history = FetchGroupTimelineHistory(formationId, groupId);
	List *filteredList = NIL;

	foreach(cell, nodeList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(cell);

		if (node->reportedTLI <= 0)
		{
			/* hasn't reported a timeline yet, don't exclude on that basis */
			filteredList = lappend(filteredList, node);
			continue;
		}

		if (TimelineIsAncestor(history, node->reportedTLI, referenceTli, NULL))
		{
			filteredList = lappend(filteredList, node);
			continue;
		}

		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Excluding " NODE_FORMAT
			" from the election: its reported timeline %d does not appear "
			"to be an ancestor of the group's reference timeline %d -- "
			"it may have diverged and could need pg_rewind; see "
			"`pg_autoctl show timeline` and `pg_autoctl accept timeline`",
			NODE_FORMAT_ARGS(node),
			node->reportedTLI,
			referenceTli);
	}

	return filteredList;
}
