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
 * CurrentNodeState gathers information we retrieve through the monitor
 * pgautofailover.current_state API, and that we can also form from other
 * pieces such as local configuration + local state, or monitor notifications.
 */
typedef struct CurrentNodeState
{
	NodeAddress node;

	char formation[NAMEDATALEN];
	char citusClusterName[NAMEDATALEN];
	int groupId;
	PgInstanceKind pgKind;

	NodeState reportedState;
	NodeState goalState;
	int candidatePriority;
	bool replicationQuorum;

	int health;
	double healthLag;
	double reportLag;
} CurrentNodeState;


/*
 * CurrentNodeStateHeaders caches the information we need to print a nice user
 * formatted table from an array of NodeAddress.
 */
typedef struct NodeAddressHeaders
{
	PgInstanceKind nodeKind;

	int maxNameSize;
	int maxHostSize;
	int maxNodeSize;
	int maxLSNSize;
	int maxStateSize;
	int maxHealthSize;

	char nameSeparatorHeader[BUFSIZE];
	char hostSeparatorHeader[BUFSIZE];
	char nodeSeparatorHeader[BUFSIZE];
	char lsnSeparatorHeader[BUFSIZE];
	char stateSeparatorHeader[BUFSIZE];
	char healthSeparatorHeader[BUFSIZE];
} NodeAddressHeaders;


typedef struct CurrentNodeStateArray
{
	int count;
	CurrentNodeState nodes[NODE_ARRAY_MAX_COUNT];
	NodeAddressHeaders headers;
} CurrentNodeStateArray;


void nodestatePrepareHeaders(CurrentNodeStateArray *nodesArray,
							 PgInstanceKind nodeKind);
void nodeAddressArrayPrepareHeaders(NodeAddressHeaders *headers,
									NodeAddressArray *nodesArray,
									int groupId,
									PgInstanceKind nodeKind);
void nodestateAdjustHeaders(NodeAddressHeaders *headers,
							NodeAddress *node, int groupId);
void prepareHeaderSeparators(NodeAddressHeaders *headers);

void nodestatePrintHeader(NodeAddressHeaders *headers);
void nodestatePrintNodeState(NodeAddressHeaders *headers,
							 CurrentNodeState *nodeState);

void nodestatePrepareNode(NodeAddressHeaders *headers, NodeAddress *node,
						  int groupId, char *hostport,
						  char *composedId, char *tliLSN);

void prepareHostNameSeparator(char nameSeparatorHeader[], int size);

bool nodestateAsJSON(CurrentNodeState *nodeState, JSON_Value *js);

char * nodestateHealthToString(int health);
char nodestateHealthToChar(int health);
char * nodestateConnectionType(CurrentNodeState *nodeState);
void nodestate_log(CurrentNodeState *nodeState, int logLevel, int64_t nodeId);

void printNodeArray(NodeAddressArray *nodesArray);
void printNodeHeader(NodeAddressHeaders *headers);
void printNodeEntry(NodeAddressHeaders *headers, NodeAddress *node);

bool nodestateFilterArrayGroup(CurrentNodeStateArray *nodesArray,
							   const char *name);

#endif /* NODESTATE_H */
