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
	char state[NAMEDATALEN];	/* primary, secondary, unavailable */
	char nodecluster[NAMEDATALEN];
} CoordinatorNode;


bool coordinator_init(Coordinator *coordinator,
					  NodeAddress *node, Keeper *keeper);
bool coordinator_init_from_monitor(Coordinator *coordinator, Keeper *keeper);
bool coordinator_add_inactive_node(Coordinator *coordinator, Keeper *keeper,
								   CoordinatorNode *node);
bool coordinator_activate_node(Coordinator *coordinator, Keeper *keeper,
							   CoordinatorNode *node);
bool coordinator_remove_node(Coordinator *coordinator, Keeper *keeper);

bool coordinator_update_node_prepare(Coordinator *coordinator, Keeper *keeper,
									 NodeAddress *primary);
bool coordinator_update_node_commit(Coordinator *coordinator, Keeper *keeper);
bool coordinator_update_node_rollback(Coordinator *coordinator, Keeper *keeper);
bool coordinator_update_cleanup(Keeper *keeper);
void GetPreparedTransactionName(int nodeid, char *name);
bool coordinator_cleanup_lost_transaction(Keeper *keeper,
										  Coordinator *coordinator);
bool coordinator_upsert_poolinfo_port(Coordinator *coordinator, Keeper *keeper);


#endif /* COORDINATOR_H */
