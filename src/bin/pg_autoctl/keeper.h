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

	/* Local cache of the other nodes list, fetched from the monitor */
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
bool keeper_create_and_drop_replication_slots(Keeper *keeper);
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
bool keeper_update_group_hba(Keeper *keeper, NodeAddressArray *diffNodesArray);
bool keeper_refresh_standby_names(Keeper *keeper, bool forceCacheInvalidation);
bool keeper_refresh_other_nodes(Keeper *keeper, bool forceCacheInvalidation);

bool keeper_set_node_metadata(Keeper *keeper, KeeperConfig *oldConfig);
bool keeper_update_nodename_from_monitor(Keeper *keeper);
bool keeper_config_accept_new(Keeper *keeper, KeeperConfig *newConfig);


/*
 * When receiving a SIGHUP signal, the keeper knows how to reload its current
 * in-memory configuration from the on-disk configuration file, and then apply
 * changes. For this we use an array of functions that we call in order each
 * time we are asked to reload.
 *
 * Because it's possible to edit the configuration file while pg_autoctl is not
 * running, we also call the ReloadHook functions when entering our main loop
 * the first time.
 */
typedef bool (*KeeperReloadFunction)(Keeper *keeper, bool firstLoop);

/* src/bin/pg_autoctl/service_keeper.c */
extern KeeperReloadFunction KeeperReloadHooks[];

void keeper_call_reload_hooks(Keeper *keeper, bool firstLoop);
bool keeper_reload_configuration(Keeper *keeper, bool firstLoop);

#endif /* KEEPER_H */
