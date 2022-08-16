/*
 * src/bin/pg_autoctl/coordinator.h
 *     Functions for interacting with a Citus coordinator
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef COORDINATOR_H
#define COORDINATOR_H

#include <stdbool.h>

#include "keeper.h"
#include "nodestate_utils.h"
#include "pgsql.h"
#include "state.h"


/* interface to the monitor */
typedef struct Coordinator
{
	NodeAddress node;
	PGSQL pgsql;
} Coordinator;

typedef struct CoordinatorNode
{
	int nodeid;
	int groupid;
	char nodename[_POSIX_HOST_NAME_MAX];
	int nodeport;
	char noderack[NAMEDATALEN];
	bool hasmetadata;
	bool isactive;
	char state[NAMEDATALEN];    /* primary, secondary, unavailable */
	char nodecluster[NAMEDATALEN];
} CoordinatorNode;


bool coordinator_init(Coordinator *coordinator,
					  NodeAddress *node, Keeper *keeper);
bool coordinator_init_from_monitor(Coordinator *coordinator, Keeper *keeper);
bool coordinator_init_from_keeper(Coordinator *coordinator, Keeper *keeper);
bool coordinator_add_node(Coordinator *coordinator, Keeper *keeper,
						  int *nodeid);
bool coordinator_add_inactive_node(Coordinator *coordinator, Keeper *keeper,
								   int *nodeid);
bool coordinator_activate_node(Coordinator *coordinator, Keeper *keeper,
							   int *nodeid);
bool coordinator_remove_node(Coordinator *coordinator, Keeper *keeper);

bool coordinator_udpate_node_transaction_is_prepared(Coordinator *coordinator,
													 Keeper *keeper,
													 bool *transactionHasBeenPrepared);

bool coordinator_update_node_prepare(Coordinator *coordinator, Keeper *keeper);
bool coordinator_update_node_commit(Coordinator *coordinator, Keeper *keeper);
bool coordinator_update_node_rollback(Coordinator *coordinator, Keeper *keeper);
void GetPreparedTransactionName(int nodeid, char *name);
bool coordinator_upsert_poolinfo_port(Coordinator *coordinator, Keeper *keeper);
bool coordinator_node_is_registered(Coordinator *coordinator, bool *isRegistered);

bool coordinator_remove_dropped_nodes(Coordinator *coordinator,
									  CurrentNodeStateArray *nodesArray);

#endif /* COORDINATOR_H */
