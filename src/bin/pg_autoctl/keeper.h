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
} Keeper;


bool keeper_init(Keeper *keeper, KeeperConfig *config);
bool keeper_init_fsm(Keeper *keeper, KeeperConfig *config);
bool keeper_register_and_init(Keeper *keeper, KeeperConfig *config,
							  NodeState initialState);
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
bool keeper_update_pg_state(Keeper *keeper);
bool ReportPgIsRunning(Keeper *keeper);
bool keeper_remove(Keeper *keeper, KeeperConfig *config,
				   bool ignore_monitor_errors);
bool keeper_check_monitor_extension_version(Keeper *keeper);

bool keeper_init_state_write(Keeper *keeper);
bool keeper_init_state_read(Keeper *keeper, KeeperStateInit *initState);
bool keeper_state_as_json(Keeper *keeper, char *json, int size);
bool keeper_init_state_discover(Keeper *keeper, KeeperStateInit *initState);

/* loop.c */
bool keeper_service_init(Keeper *keeper, pid_t *pid);
bool keeper_service_stop(Keeper *keeper);
bool keeper_service_run(Keeper *keeper, pid_t *start_pid);
bool read_pidfile(const char *pidfile, pid_t *pid);

#endif /* KEEPER_H */
