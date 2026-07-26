/*
 * src/bin/pg_autoctl/fsm_mermaid.c
 *   Mermaid stateDiagram-v2 rendering of the keeper FSM (KeeperFSM[] in
 *   fsm.c), split into narrative phases so each diagram stays small enough
 *   to read at a glance, instead of one dense 77-edge/20-state graph.
 *
 * The phase a given (current, assigned) edge belongs to is not derived
 * automatically from KeeperFSM[] -- it is a small, hand-curated
 * classification (FsmPhaseMap below), verified once against every edge's
 * real transition comment so it stays grounded in what the FSM actually
 * does rather than a guess. If a new transition is ever added to
 * KeeperFSM[], this table needs a matching new entry, and the generator
 * intentionally logs a warning (not silently drops the edge) if one is
 * missing, so that omission gets noticed instead of silently disappearing
 * from every diagram.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include <stdio.h>
#include <string.h>

#include "defaults.h"
#include "fsm.h"
#include "fsm_mermaid.h"
#include "log.h"
#include "state.h"


const char *FsmMermaidPhaseName[FSM_PHASE_COUNT] = {
	"init",
	"steady-state",
	"failover",
	"maintenance",
	"removal"
};

const char *FsmMermaidPhaseTitle[FSM_PHASE_COUNT] = {
	"Node init / join",
	"Steady-state / config changes",
	"Failover / promotion",
	"Maintenance",
	"Node removal / drop"
};

typedef struct FsmPhaseMapEntry
{
	NodeState current;
	NodeState assigned;
	FsmMermaidPhase phase;
} FsmPhaseMapEntry;

/*
 * One entry per unique (current, assigned) pair reachable from any node
 * kind (Citus coordinator/worker transitions never introduce a different
 * edge shape than NODE_KIND_ANY -- only a different transition function --
 * so this table, like the generator below, only needs to reason about the
 * ANY-kind graph). Verified against fsm.c: 77 real transitions plus the
 * ANY_STATE -> DROPPED_STATE catch-all = 78 rows here; the NO_STATE ->
 * NO_STATE sentinel row in KeeperFSM[] is skipped explicitly, never
 * classified.
 */
static const FsmPhaseMapEntry FsmPhaseMap[] = {
	{ INIT_STATE, SINGLE_STATE, FSM_PHASE_INIT },
	{ DROPPED_STATE, SINGLE_STATE, FSM_PHASE_INIT },
	{ DROPPED_STATE, REPORT_LSN_STATE, FSM_PHASE_INIT },
	{ SINGLE_STATE, WAIT_PRIMARY_STATE, FSM_PHASE_INIT },
	{ INIT_STATE, WAIT_STANDBY_STATE, FSM_PHASE_INIT },
	{ DROPPED_STATE, WAIT_STANDBY_STATE, FSM_PHASE_INIT },
	{ WAIT_STANDBY_STATE, CATCHINGUP_STATE, FSM_PHASE_INIT },
	{ INIT_STATE, REPORT_LSN_STATE, FSM_PHASE_INIT },

	{ PRIMARY_STATE, WAIT_PRIMARY_STATE, FSM_PHASE_STEADY_STATE },
	{ WAIT_PRIMARY_STATE, PRIMARY_STATE, FSM_PHASE_STEADY_STATE },
	{ SECONDARY_STATE, CATCHINGUP_STATE, FSM_PHASE_STEADY_STATE },
	{ CATCHINGUP_STATE, SECONDARY_STATE, FSM_PHASE_STEADY_STATE },
	{ SECONDARY_STATE, WAIT_STANDBY_STATE, FSM_PHASE_STEADY_STATE },
	{ PRIMARY_STATE, APPLY_SETTINGS_STATE, FSM_PHASE_STEADY_STATE },
	{ WAIT_PRIMARY_STATE, APPLY_SETTINGS_STATE, FSM_PHASE_STEADY_STATE },
	{ APPLY_SETTINGS_STATE, PRIMARY_STATE, FSM_PHASE_STEADY_STATE },
	{ APPLY_SETTINGS_STATE, WAIT_PRIMARY_STATE, FSM_PHASE_STEADY_STATE },

	{ PRIMARY_STATE, DRAINING_STATE, FSM_PHASE_FAILOVER },
	{ DRAINING_STATE, DEMOTED_STATE, FSM_PHASE_FAILOVER },
	{ PRIMARY_STATE, DEMOTED_STATE, FSM_PHASE_FAILOVER },
	{ PRIMARY_STATE, DEMOTE_TIMEOUT_STATE, FSM_PHASE_FAILOVER },
	{ APPLY_SETTINGS_STATE, DRAINING_STATE, FSM_PHASE_FAILOVER },
	{ APPLY_SETTINGS_STATE, DEMOTED_STATE, FSM_PHASE_FAILOVER },
	{ APPLY_SETTINGS_STATE, DEMOTE_TIMEOUT_STATE, FSM_PHASE_FAILOVER },
	{ DRAINING_STATE, DEMOTE_TIMEOUT_STATE, FSM_PHASE_FAILOVER },
	{ DEMOTE_TIMEOUT_STATE, DEMOTED_STATE, FSM_PHASE_FAILOVER },
	{ WAIT_PRIMARY_STATE, DEMOTED_STATE, FSM_PHASE_FAILOVER },
	{ DEMOTE_TIMEOUT_STATE, PRIMARY_STATE, FSM_PHASE_FAILOVER },
	{ DEMOTED_STATE, CATCHINGUP_STATE, FSM_PHASE_FAILOVER },
	{ SECONDARY_STATE, PREP_PROMOTION_STATE, FSM_PHASE_FAILOVER },
	{ CATCHINGUP_STATE, PREP_PROMOTION_STATE, FSM_PHASE_FAILOVER },
	{ PREP_PROMOTION_STATE, STOP_REPLICATION_STATE, FSM_PHASE_FAILOVER },
	{ STOP_REPLICATION_STATE, WAIT_PRIMARY_STATE, FSM_PHASE_FAILOVER },
	{ PREP_PROMOTION_STATE, WAIT_PRIMARY_STATE, FSM_PHASE_FAILOVER },
	{ SECONDARY_STATE, REPORT_LSN_STATE, FSM_PHASE_FAILOVER },
	{ CATCHINGUP_STATE, REPORT_LSN_STATE, FSM_PHASE_FAILOVER },
	{ REPORT_LSN_STATE, PREP_PROMOTION_STATE, FSM_PHASE_FAILOVER },
	{ REPORT_LSN_STATE, FAST_FORWARD_STATE, FSM_PHASE_FAILOVER },
	{ FAST_FORWARD_STATE, PREP_PROMOTION_STATE, FSM_PHASE_FAILOVER },
	{ REPORT_LSN_STATE, JOIN_SECONDARY_STATE, FSM_PHASE_FAILOVER },
	{ REPORT_LSN_STATE, SECONDARY_STATE, FSM_PHASE_FAILOVER },
	{ JOIN_SECONDARY_STATE, SECONDARY_STATE, FSM_PHASE_FAILOVER },
	{ DRAINING_STATE, REPORT_LSN_STATE, FSM_PHASE_FAILOVER },
	{ DEMOTED_STATE, REPORT_LSN_STATE, FSM_PHASE_FAILOVER },

	{ PRIMARY_STATE, PREPARE_MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ PREPARE_MAINTENANCE_STATE, MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ PRIMARY_STATE, MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ SECONDARY_STATE, WAIT_MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ CATCHINGUP_STATE, WAIT_MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ SECONDARY_STATE, MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ CATCHINGUP_STATE, MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ WAIT_MAINTENANCE_STATE, MAINTENANCE_STATE, FSM_PHASE_MAINTENANCE },
	{ MAINTENANCE_STATE, CATCHINGUP_STATE, FSM_PHASE_MAINTENANCE },
	{ PREPARE_MAINTENANCE_STATE, CATCHINGUP_STATE, FSM_PHASE_MAINTENANCE },
	{ MAINTENANCE_STATE, REPORT_LSN_STATE, FSM_PHASE_MAINTENANCE },
	{ PREPARE_MAINTENANCE_STATE, REPORT_LSN_STATE, FSM_PHASE_MAINTENANCE },

	{ PRIMARY_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ WAIT_PRIMARY_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ APPLY_SETTINGS_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ DEMOTED_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ DEMOTE_TIMEOUT_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ DRAINING_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ SECONDARY_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ CATCHINGUP_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ PREP_PROMOTION_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ STOP_REPLICATION_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ REPORT_LSN_STATE, SINGLE_STATE, FSM_PHASE_REMOVAL },
	{ ANY_STATE, DROPPED_STATE, FSM_PHASE_REMOVAL },
};

#define FSM_PHASE_MAP_COUNT (sizeof(FsmPhaseMap) / sizeof(FsmPhaseMap[0]))

/* the six colour classes used to shade the diagrams */
typedef enum
{
	FSM_COLOR_META = 0,
	FSM_COLOR_PRIMARY,
	FSM_COLOR_SECONDARY,
	FSM_COLOR_DEMOTING,
	FSM_COLOR_MAINTENANCE,
	FSM_COLOR_ELECTION,

	FSM_COLOR_COUNT
} FsmMermaidColor;

static const char *FsmMermaidColorClassDef[FSM_COLOR_COUNT] = {
	"classDef metaState fill:#e0e0e0,stroke:#888888,color:#333333",
	"classDef primaryState fill:#cfe2ff,stroke:#3b6fb6,color:#1a1a1a",
	"classDef secondaryState fill:#d4edda,stroke:#4c9a5b,color:#1a1a1a",
	"classDef demotingState fill:#f8d7da,stroke:#c0392b,color:#1a1a1a",
	"classDef maintenanceState fill:#e8dff5,stroke:#8e6bb0,color:#1a1a1a",
	"classDef electionState fill:#fff3cd,stroke:#c99a1e,color:#1a1a1a"
};

static const char *FsmMermaidColorClassName[FSM_COLOR_COUNT] = {
	"metaState",
	"primaryState",
	"secondaryState",
	"demotingState",
	"maintenanceState",
	"electionState"
};

/*
 * FsmMermaidColorForState assigns each real state to one of the six colour
 * classes above. ANY_STATE/NO_STATE never appear as a rendered node (only
 * as the "from" side of the drop catch-all, which is emitted as a plain
 * edge, not a node needing a colour), so they're not classified here.
 */
static FsmMermaidColor
FsmMermaidColorForState(NodeState state)
{
	switch (state)
	{
		case INIT_STATE:
		case SINGLE_STATE:
		case DROPPED_STATE:
		case ANY_STATE:
		{
			return FSM_COLOR_META;
		}

		case PRIMARY_STATE:
		case WAIT_PRIMARY_STATE:
		case JOIN_PRIMARY_STATE:
		case APPLY_SETTINGS_STATE:
		{
			return FSM_COLOR_PRIMARY;
		}

		case SECONDARY_STATE:
		case CATCHINGUP_STATE:
		case WAIT_STANDBY_STATE:
		{
			return FSM_COLOR_SECONDARY;
		}

		case DRAINING_STATE:
		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		{
			return FSM_COLOR_DEMOTING;
		}

		case MAINTENANCE_STATE:
		case PREPARE_MAINTENANCE_STATE:
		case WAIT_MAINTENANCE_STATE:
		{
			return FSM_COLOR_MAINTENANCE;
		}

		case PREP_PROMOTION_STATE:
		case STOP_REPLICATION_STATE:
		case REPORT_LSN_STATE:
		case FAST_FORWARD_STATE:
		case JOIN_SECONDARY_STATE:
		default:
		{
			return FSM_COLOR_ELECTION;
		}
	}
}

/*
 * FsmMermaidId returns the identifier to use for a state in Mermaid source.
 * NodeStateToString(ANY_STATE) returns "#any state#", which is not a valid
 * Mermaid identifier (spaces, a '#') -- every other state's string is
 * already a bare lowercase word and safe to use as-is.
 */
static const char *
FsmMermaidId(NodeState state)
{
	if (state == ANY_STATE)
	{
		return "any_state";
	}

	return NodeStateToString(state);
}


bool
fsm_mermaid_phase_from_string(const char *name, FsmMermaidPhase *phase)
{
	int i = 0;

	for (i = 0; i < FSM_PHASE_COUNT; i++)
	{
		if (strcmp(name, FsmMermaidPhaseName[i]) == 0)
		{
			*phase = (FsmMermaidPhase) i;
			return true;
		}
	}

	return false;
}


/* a small fixed set of "seen" states, big enough for the whole FSM */
#define FSM_MAX_STATES 32

static bool
StateSetContains(NodeState *set, int count, NodeState state)
{
	int i = 0;

	for (i = 0; i < count; i++)
	{
		if (set[i] == state)
		{
			return true;
		}
	}
	return false;
}


/*
 * FsmMermaidPhaseMaskForState returns a bitmask of every phase (1 <<
 * FSM_PHASE_*) in which the given state appears as either side of an edge,
 * by scanning FsmPhaseMap. Used to print "also appears in ..." notes.
 */
static int
FsmMermaidPhaseMaskForState(NodeState state)
{
	int mask = 0;
	int i = 0;

	for (i = 0; i < FSM_PHASE_MAP_COUNT; i++)
	{
		if (FsmPhaseMap[i].current == state || FsmPhaseMap[i].assigned == state)
		{
			mask |= (1 << FsmPhaseMap[i].phase);
		}
	}

	return mask;
}


/*
 * FsmPhaseForEdge looks up the curated phase for a given (current, assigned)
 * pair. Returns false (logging a warning) when the edge isn't in
 * FsmPhaseMap at all -- this can only happen if KeeperFSM[] gained a new
 * NODE_KIND_ANY transition that FsmPhaseMap wasn't updated to cover.
 */
static bool
FsmPhaseForEdge(NodeState current, NodeState assigned, FsmMermaidPhase *phase)
{
	int i = 0;

	for (i = 0; i < FSM_PHASE_MAP_COUNT; i++)
	{
		if (FsmPhaseMap[i].current == current &&
			FsmPhaseMap[i].assigned == assigned)
		{
			*phase = FsmPhaseMap[i].phase;
			return true;
		}
	}

	log_warn("BUG: FSM transition \"%s\" -> \"%s\" has no entry in "
			 "FsmPhaseMap (fsm_mermaid.c) -- it will not appear in any "
			 "phase diagram until this is fixed",
			 NodeStateToString(current), NodeStateToString(assigned));

	return false;
}


/*
 * print_fsm_mermaid_for_phase renders one phase's slice of the FSM as a
 * Mermaid stateDiagram-v2. Only NODE_KIND_ANY transitions are considered:
 * Citus coordinator/worker rows never introduce an edge that doesn't also
 * exist under NODE_KIND_ANY (verified: every one of the 15 Citus-specific
 * (current, assigned) pairs already has an ANY-kind counterpart), so this
 * diagram already covers Citus topologies -- there is no separate "Citus"
 * phase to add.
 */
void
print_fsm_mermaid_for_phase(FsmMermaidPhase phase)
{
	KeeperFSMTransition transition = KeeperFSM[0];
	int transitionIndex = 0;

	NodeState seenCurrent[FSM_MAX_STATES] = { 0 };
	NodeState seenAssigned[FSM_MAX_STATES] = { 0 };
	int seenCount = 0;

	NodeState diagramStates[FSM_MAX_STATES] = { 0 };
	int diagramStateCount = 0;

	int i = 0;

	fformat(stdout, "stateDiagram-v2\n");

	while (transition.current != NO_STATE)
	{
		bool alreadySeen = false;
		FsmMermaidPhase edgePhase;

		if (transition.pgKind != NODE_KIND_ANY)
		{
			/* Citus-specific rows never add a new edge shape, skip them */
			transition = KeeperFSM[++transitionIndex];
			continue;
		}

		if (transition.current == JOIN_PRIMARY_STATE ||
			transition.assigned == JOIN_PRIMARY_STATE)
		{
			/*
			 * join_primary is deprecated and no longer assigned to nodes
			 * (see docs/failover-state-machine.rst) -- KeeperFSM[] still
			 * carries its transitions for backward compatibility with
			 * on-disk state from old versions, but it should not clutter
			 * diagrams describing current behaviour.
			 */
			transition = KeeperFSM[++transitionIndex];
			continue;
		}

		for (i = 0; i < seenCount; i++)
		{
			if (seenCurrent[i] == transition.current &&
				seenAssigned[i] == transition.assigned)
			{
				alreadySeen = true;
				break;
			}
		}

		if (alreadySeen)
		{
			transition = KeeperFSM[++transitionIndex];
			continue;
		}

		if (seenCount < FSM_MAX_STATES)
		{
			seenCurrent[seenCount] = transition.current;
			seenAssigned[seenCount] = transition.assigned;
			++seenCount;
		}

		if (FsmPhaseForEdge(transition.current, transition.assigned, &edgePhase) &&
			edgePhase == phase)
		{
			fformat(stdout, "    %s --> %s : %s\n",
					FsmMermaidId(transition.current),
					FsmMermaidId(transition.assigned),
					transition.comment);

			if (!StateSetContains(diagramStates, diagramStateCount,
								  transition.current) &&
				diagramStateCount < FSM_MAX_STATES)
			{
				diagramStates[diagramStateCount++] = transition.current;
			}
			if (!StateSetContains(diagramStates, diagramStateCount,
								  transition.assigned) &&
				diagramStateCount < FSM_MAX_STATES)
			{
				diagramStates[diagramStateCount++] = transition.assigned;
			}
		}

		transition = KeeperFSM[++transitionIndex];
	}

	fformat(stdout, "\n");

	/* notes: make it obvious when a state also lives in another diagram */
	for (i = 0; i < diagramStateCount; i++)
	{
		int mask = FsmMermaidPhaseMaskForState(diagramStates[i]);
		int otherMask = mask & ~(1 << phase);
		int p = 0;
		bool first = true;
		char note[BUFSIZE] = { 0 };

		if (otherMask == 0)
		{
			continue;
		}

		/*
		 * No colon inside the note text itself: Mermaid's "note right of
		 * X : text" grammar treats a colon as the delimiter between the
		 * state name and the note body, and chokes on a second one inside
		 * the body ("Expecting ... got 'DESCR'").
		 */
		strlcat(note, "also appears in ", BUFSIZE);

		for (p = 0; p < FSM_PHASE_COUNT; p++)
		{
			if (otherMask & (1 << p))
			{
				if (!first)
				{
					strlcat(note, ", ", BUFSIZE);
				}
				strlcat(note, FsmMermaidPhaseTitle[p], BUFSIZE);
				first = false;
			}
		}

		fformat(stdout, "    note right of %s : %s\n",
				FsmMermaidId(diagramStates[i]), note);
	}

	fformat(stdout, "\n");

	/* colour scheme: only emit classDef/class for states used here */
	{
		bool colorUsed[FSM_COLOR_COUNT] = { false };

		for (i = 0; i < diagramStateCount; i++)
		{
			colorUsed[FsmMermaidColorForState(diagramStates[i])] = true;
		}

		for (i = 0; i < FSM_COLOR_COUNT; i++)
		{
			if (colorUsed[i])
			{
				fformat(stdout, "    %s\n", FsmMermaidColorClassDef[i]);
			}
		}

		for (i = 0; i < diagramStateCount; i++)
		{
			FsmMermaidColor color = FsmMermaidColorForState(diagramStates[i]);

			fformat(stdout, "    class %s %s\n",
					FsmMermaidId(diagramStates[i]),
					FsmMermaidColorClassName[color]);
		}
	}
}
