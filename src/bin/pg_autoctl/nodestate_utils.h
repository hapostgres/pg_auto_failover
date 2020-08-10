/*
 * src/bin/pg_autoctl/nodestate_utils.h
 *     Functions for printing node states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#ifndef NODESTATE_H
#define NODESTATE_H

#include <stdbool.h>

#include "pgsql.h"

/*
 * CurrentNodeState gather information we retrive through the monitor
 * pgautofailover.current_state API, and that we can also form from other
 * pieces such as local configuration + local state, or monitor notifications.
 */
typedef struct CurrentNodeState
{
	NodeAddress node;
	char formation[NAMEDATALEN];
	int groupId;

	NodeState reportedState;
	NodeState goalState;
	int candidatePriority;
	bool replicationQuorum;

	int health;
} CurrentNodeState;


/*
 * CurrentNodeStateHeaders caches the information we need to print a nice user
 * formatted table from an array of NodeAddress.
 */
typedef struct NodeAddressHeaders
{
	int maxNameSize;
	int maxHostSize;
	int maxNodeSize;

	char nameSeparatorHeader[BUFSIZE];
	char hostSeparatorHeader[BUFSIZE];
	char nodeSeparatorHeader[BUFSIZE];
} NodeAddressHeaders;


typedef struct CurrentNodeStateArray
{
	int count;
	CurrentNodeState nodes[NODE_ARRAY_MAX_COUNT];
	NodeAddressHeaders headers;
} CurrentNodeStateArray;


void nodestatePrepareHeaders(CurrentNodeStateArray *nodesArray);
void nodeAddressArrayPrepareHeaders(NodeAddressHeaders *headers,
									NodeAddressArray *nodesArray, int groupId);
void nodestateAdjustHeaders(NodeAddressHeaders *headers,
							NodeAddress *node, int groupId);
void prepareHeaderSeparators(NodeAddressHeaders *headers);

void nodestatePrintHeader(CurrentNodeStateArray *nodesArray);
void nodestatePrintNodeState(CurrentNodeStateArray *nodesArray,
							 CurrentNodeState *nodeState);

void nodestatePrepareNode(NodeAddress *node,
						  int groupId, char *hostport, char *composedId);
void prepareHostNameSeparator(char nameSeparatorHeader[], int size);

#endif /* NODESTATE_H */
