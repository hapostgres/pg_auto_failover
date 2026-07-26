/*
 * src/bin/pg_autoctl/fsm_mermaid.h
 *   Mermaid stateDiagram-v2 rendering of the keeper FSM (KeeperFSM[] in
 *   fsm.c), split into narrative phases so each diagram stays small enough
 *   to read at a glance.
 *
 * FsmPhase itself (the enum) lives in fsm.h, as a field directly on
 * KeeperFSMTransition -- there is exactly one FSM table, and this generator
 * walks it the same way print_fsm_for_graphviz() does, just grouping by
 * that field instead of dumping every row.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef FSM_MERMAID_H
#define FSM_MERMAID_H

#include "fsm.h"
#include "state.h"

extern const char *FsmPhaseName[FSM_PHASE_COUNT];
extern const char *FsmPhaseTitle[FSM_PHASE_COUNT];

void print_fsm_mermaid_for_phase(FsmPhase phase);
bool fsm_mermaid_phase_from_string(const char *name, FsmPhase *phase);

#endif /* FSM_MERMAID_H */
