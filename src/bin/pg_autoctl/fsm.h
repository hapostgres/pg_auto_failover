/*
 * src/bin/pg_autoctl/fsm.h
 *   Finite State Machine implementation for pg_autoctl.
 *
 * Handling of the Finite State Machine driving the pg_autoctl Keeper.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_FSM_H
#define KEEPER_FSM_H

#include "keeper.h"
#include "keeper_config.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"


/*
 * Each FSM entry is a transition from a current state to the next
 */
typedef bool (*ReachAssignedStateFunction)(Keeper *keeper);

/* defines a possible transition in the FSM */
typedef struct KeeperFSMTransition
{
	NodeState current;
	NodeState assigned;
	const char *comment;
	ReachAssignedStateFunction transitionFunction;
} KeeperFSMTransition;

/* src/bin/pg_autoctl/fsm.c */
extern KeeperFSMTransition KeeperFSM[];

bool fsm_init_primary(Keeper *keeper);
bool fsm_prepare_replication(Keeper *keeper);
bool fsm_disable_replication(Keeper *keeper);
bool fsm_resume_as_primary(Keeper *keeper);
bool fsm_rewind_or_init(Keeper *keeper);
bool fsm_prepare_for_secondary(Keeper *keeper);

bool fsm_init_standby(Keeper *keeper);
bool fsm_promote_standby(Keeper *keeper);
bool fsm_prepare_standby_for_promotion(Keeper *keeper);
bool fsm_promote_standby_to_primary(Keeper *keeper);
bool fsm_promote_standby_to_single(Keeper *keeper);
bool fsm_stop_replication(Keeper *keeper);

bool fsm_enable_sync_rep(Keeper *keeper);
bool fsm_disable_sync_rep(Keeper *keeper);
bool fsm_apply_settings(Keeper *keeper);

bool fsm_start_postgres(Keeper *keeper);
bool fsm_stop_postgres(Keeper *keeper);
bool fsm_stop_postgres_for_primary_maintenance(Keeper *keeper);
bool fsm_stop_postgres_and_setup_standby(Keeper *keeper);
bool fsm_checkpoint_and_stop_postgres(Keeper *keeper);

bool fsm_start_maintenance_on_standby(Keeper *keeper);
bool fsm_restart_standby(Keeper *keeper);

bool fsm_report_lsn(Keeper *keeper);
bool fsm_report_lsn_and_drop_replication_slots(Keeper *keeper);
bool fsm_fast_forward(Keeper *keeper);
bool fsm_prepare_cascade(Keeper *keeper);
bool fsm_follow_new_primary(Keeper *keeper);
bool fsm_cleanup_as_primary(Keeper *keeper);

bool fsm_init_from_standby(Keeper *keeper);

bool fsm_drop_node(Keeper *keeper);

/*
 * Extra helpers.
 */
bool prepare_replication(Keeper *keeper, NodeState otherNodeState);

/*
 * Generic API to use the previous definitions.
 */
void print_reachable_states(KeeperStateData *keeperState);
void print_fsm_for_graphviz(void);
bool keeper_fsm_step(Keeper *keeper);
bool keeper_fsm_reach_assigned_state(Keeper *keeper);


#endif /* KEEPER_FSM_H */
