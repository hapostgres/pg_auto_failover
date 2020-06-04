/*
 * src/bin/pg_autoctl/keeper.h
 *    Main data structures for the pg_autoctl service state.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef KEEPER_H
#define KEEPER_H

#include "commandline.h"
#include "keeper_config.h"
#include "log.h"
#include "monitor.h"
#include "primary_standby.h"
#include "state.h"

/* the keeper manages a postgres server according to the given configuration */
typedef struct Keeper
{
	KeeperConfig config;
	LocalPostgresServer postgres;
	KeeperStateData state;
	Monitor monitor;

	/*
	 * When running without monitor, we need a place to stash the otherNodes
	 * information. This is necessary in some transitions.
	 */
	NodeAddressArray otherNodes;

	/* Only useful during the initialization of the Keeper */
	KeeperStateInit initState;
} Keeper;


bool keeper_init(Keeper *keeper, KeeperConfig *config);
bool keeper_init_fsm(Keeper *keeper);
bool keeper_register_and_init(Keeper *keeper, NodeState initialState);
bool keeper_load_state(Keeper *keeper);
bool keeper_store_state(Keeper *keeper);
bool keeper_update_state(Keeper *keeper, int node_id, int group_id, NodeState state,
						 bool update_last_monitor_contact);
bool keeper_start_postgres(Keeper *keeper);
bool keeper_restart_postgres(Keeper *keeper);
bool keeper_should_ensure_current_state_before_transition(Keeper *keeper);
bool keeper_ensure_postgres_is_running(Keeper *keeper, bool updateRetries);
bool keeper_drop_replication_slots_for_removed_nodes(Keeper *keeper);
bool keeper_maintain_replication_slots(Keeper *keeper);
bool keeper_ensure_current_state(Keeper *keeper);
bool keeper_create_self_signed_cert(Keeper *keeper);
bool keeper_ensure_configuration(Keeper *keeper, bool postgresNotRunningIsOk);
bool keeper_update_pg_state(Keeper *keeper);
bool ReportPgIsRunning(Keeper *keeper);
bool keeper_remove(Keeper *keeper, KeeperConfig *config,
				   bool ignore_monitor_errors);
bool keeper_check_monitor_extension_version(Keeper *keeper);
bool keeper_state_as_json(Keeper *keeper, char *json, int size);

#endif /* KEEPER_H */
