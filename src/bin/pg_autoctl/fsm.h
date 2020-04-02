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

/*
 * Each transition specifies if it wants Postgres to be running as a
 * pre-condition to the transition. The Postgres service is managed by a
 * dedicated sub-process that reads the on-disk FSM state and manages the
 * service accordingly.
 */
typedef enum
{
	PGSTATUS_UNKNOWN = 0,       /* please do nothing */
	PGSTATUS_INIT,              /* see init stage in init state file */
	PGSTATUS_STOPPED,           /* ensure Postgres is NOT running */
	PGSTATUS_RUNNING            /* Postgres should be running now */
} ExpectedPostgresStatus;

/* defines a possible transition in the FSM */
typedef struct KeeperFSMTransition
{
	NodeState current;
	NodeState assigned;
	const char *comment;
	ExpectedPostgresStatus pgStatus;
	ReachAssignedStateFunction transitionFunction;
} KeeperFSMTransition;

/* src/bin/pg_autoctl/fsm.c */
extern KeeperFSMTransition KeeperFSM[];

bool fsm_init_primary(Keeper *keeper);
bool fsm_prepare_replication(Keeper *keeper);
bool fsm_disable_replication(Keeper *keeper);
bool fsm_resume_as_primary(Keeper *keeper);
bool fsm_rewind_or_init(Keeper *keeper);

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

bool fsm_start_maintenance_on_standby(Keeper *keeper);
bool fsm_restart_standby(Keeper *keeper);

/*
 * Generic API to use the previous definitions.
 */
void print_reachable_states(KeeperStateData *keeperState);
void print_fsm_for_graphviz(void);
bool keeper_fsm_step(Keeper *keeper);
bool keeper_fsm_reach_assigned_state(Keeper *keeper);

ExpectedPostgresStatus keeper_fsm_get_pgstatus(Keeper *keeper);


#endif /* KEEPER_FSM_H */
