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


typedef struct KeeperVersion
{
	char pg_autoctl_version[BUFSIZE];
	char required_extension_version[BUFSIZE];
} KeeperVersion;


bool keeper_init(Keeper *keeper, KeeperConfig *config);
bool keeper_init_fsm(Keeper *keeper);
bool keeper_register_and_init(Keeper *keeper, NodeState initialState);
bool keeper_register_again(Keeper *keeper);
bool keeper_load_state(Keeper *keeper);
bool keeper_store_state(Keeper *keeper);
bool keeper_update_state(Keeper *keeper, int64_t node_id, int group_id, NodeState state,
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
bool keeper_update_pg_state(Keeper *keeper, int logLevel);
bool keeper_node_active(Keeper *keeper, bool doInit,
						MonitorAssignedState *assignedState);
bool keeper_ensure_node_has_been_dropped(Keeper *keeper, bool *dropped);
bool ReportPgIsRunning(Keeper *keeper);
bool keeper_remove(Keeper *keeper, KeeperConfig *config);
bool keeper_check_monitor_extension_version(Keeper *keeper,
											MonitorExtensionVersion *version);
bool keeper_state_as_json(Keeper *keeper, char *json, int size);
bool keeper_update_group_hba(Keeper *keeper, NodeAddressArray *diffNodesArray);
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
typedef bool (*KeeperReloadFunction)(Keeper *keeper, bool firstLoop, bool doInit);

/*
 * When updating the list of other nodes (a NodesArray) after calling
 * node_active, the keeper needs to implement specific actions such as editing
 * the HBA rules to allow new nodes to connect.
 */
typedef bool (*KeeperNodesArrayRefreshFunction)(Keeper *keeper,
												NodeAddressArray *newNodesArray,
												bool forceCacheInvalidation);

/* src/bin/pg_autoctl/service_keeper.c */
extern KeeperReloadFunction *KeeperReloadHooks;
extern KeeperNodesArrayRefreshFunction *KeeperRefreshHooks;

void keeper_call_reload_hooks(Keeper *keeper, bool firstLoop, bool doInit);
bool keeper_reload_configuration(Keeper *keeper, bool firstLoop, bool doInit);

bool keeper_call_refresh_hooks(Keeper *keeper,
							   NodeAddressArray *newNodesArray,
							   bool forceCacheInvalidation);
bool keeper_refresh_hba(Keeper *keeper,
						NodeAddressArray *newNodesArray,
						bool forceCacheInvalidation);

bool keeper_read_nodes_from_file(Keeper *keeper, NodeAddressArray *nodesArray);
bool keeper_get_primary(Keeper *keeper, NodeAddress *primaryNode);
bool keeper_get_most_advanced_standby(Keeper *keeper, NodeAddress *primaryNode);


bool keeper_pg_autoctl_get_version_from_disk(Keeper *keeper,
											 KeeperVersion *version);


#endif /* KEEPER_H */
