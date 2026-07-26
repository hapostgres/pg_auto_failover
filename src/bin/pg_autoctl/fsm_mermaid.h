/*
 * src/bin/pg_autoctl/fsm_mermaid.h
 *   Mermaid stateDiagram-v2 rendering of the keeper FSM (KeeperFSM[] in
 *   fsm.c), split into narrative phases so each diagram stays small enough
 *   to read at a glance.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef FSM_MERMAID_H
#define FSM_MERMAID_H

#include "state.h"

/*
 * Each edge of the deduplicated (current, assigned) FSM graph is tagged with
 * one of these phases. Some states legitimately appear in more than one
 * phase's diagram (e.g. CATCHINGUP/SECONDARY show up in both "init" and
 * "steady-state") -- that's expected, not a partition bug, and the generator
 * annotates every such state with a note pointing at the other diagram(s) it
 * also appears in.
 */
typedef enum
{
	FSM_PHASE_INIT = 0,
	FSM_PHASE_STEADY_STATE,
	FSM_PHASE_FAILOVER,
	FSM_PHASE_MAINTENANCE,
	FSM_PHASE_REMOVAL,

	FSM_PHASE_COUNT
} FsmMermaidPhase;

extern const char *FsmMermaidPhaseName[FSM_PHASE_COUNT];
extern const char *FsmMermaidPhaseTitle[FSM_PHASE_COUNT];

void print_fsm_mermaid_for_phase(FsmMermaidPhase phase);
bool fsm_mermaid_phase_from_string(const char *name, FsmMermaidPhase *phase);

#endif /* FSM_MERMAID_H */
