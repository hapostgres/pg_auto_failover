/*
 * src/bin/pg_autoctl/nodestate_utils.c
 *     Functions for printing node states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "file_utils.h"
#include "nodestate_utils.h"
#include "string_utils.h"


/*
 * nodestatePrepareHeaders computes the maximum length needed for variable
 * length columns and prepare the separation strings, filling them with the
 * right amount of dashes.
 */
void
nodestatePrepareHeaders(CurrentNodeStateArray *nodesArray)
{
	int index = 0;

	nodesArray->headers.maxNameSize = 4;  /* "Name" */
	nodesArray->headers.maxHostSize = 10; /* "Host:Port" */
	nodesArray->headers.maxNodeSize = 5;  /* "Node" */

	/*
	 * Dynamically adjust our display output to the length of the longer
	 * hostname in the result set
	 */
	for (index = 0; index < nodesArray->count; index++)
	{
		CurrentNodeState *nodeState = &(nodesArray->nodes[index]);

		(void) nodestateAdjustHeaders(&(nodesArray->headers),
									  &(nodeState->node),
									  nodeState->groupId);
	}

	/* prepare a nice dynamic string of '-' as a header separator */
	(void) prepareHeaderSeparators(&(nodesArray->headers));
}


/*
 * nodestatePrepareHeaders computes the maximum length needed for variable
 * length columns and prepare the separation strings, filling them with the
 * right amount of dashes.
 */
void
nodeAddressArrayPrepareHeaders(NodeAddressHeaders *headers,
							   NodeAddressArray *nodesArray, int groupId)
{
	int index = 0;

	headers->maxNameSize = 4;  /* "Name" */
	headers->maxHostSize = 10; /* "Host:Port" */
	headers->maxNodeSize = 5;  /* "Node" */

	/*
	 * Dynamically adjust our display output to the length of the longer
	 * hostname in the result set
	 */
	for (index = 0; index < nodesArray->count; index++)
	{
		NodeAddress *node = &(nodesArray->nodes[index]);

		(void) nodestateAdjustHeaders(headers, node, groupId);
	}

	/* prepare a nice dynamic string of '-' as a header separator */
	(void) prepareHeaderSeparators(headers);
}


/*
 * prepareHeaderSeparators prepares all the separator strings. headers sizes
 * must have been pre-computed.
 */
void
prepareHeaderSeparators(NodeAddressHeaders *headers)
{
	(void) prepareHostNameSeparator(headers->nameSeparatorHeader,
									headers->maxNameSize);

	(void) prepareHostNameSeparator(headers->hostSeparatorHeader,
									headers->maxHostSize);

	(void) prepareHostNameSeparator(headers->nodeSeparatorHeader,
									headers->maxNodeSize);
}


/*
 * re-compute headers properties from current properties and the new node
 * characteristics.
 */
void
nodestateAdjustHeaders(NodeAddressHeaders *headers,
					   NodeAddress *node, int groupId)
{
	int nameLen = strlen(node->name);

	/* compute strlen of host:port */
	IntString portString = intToString(node->port);
	int hostLen =
		strlen(node->host) + strlen(portString.strValue) + 1;

	/* compute strlen of groupId/nodeId, as in "0/1" */
	IntString nodeIdString = intToString(node->nodeId);
	IntString groupIdString = intToString(groupId);
	int nodeLen =
		strlen(groupIdString.strValue) + strlen(nodeIdString.strValue) + 1;

	if (nameLen > headers->maxNameSize)
	{
		headers->maxNameSize = nameLen;
	}

	if (hostLen > headers->maxHostSize)
	{
		headers->maxHostSize = hostLen;
	}

	if (nodeLen > headers->maxNodeSize)
	{
		headers->maxNodeSize = nodeLen;
	}
}


/*
 * nodestatePrintHeader prints the given CurrentNodeStateArray header.
 */
void
nodestatePrintHeader(CurrentNodeStateArray *nodesArray)
{
	fformat(stdout, "%*s | %*s | %*s | %17s | %17s | %17s | %6s\n",
			nodesArray->headers.maxNameSize, "Name",
			nodesArray->headers.maxNodeSize, "Node",
			nodesArray->headers.maxHostSize, "Host:Port",
			"Current State", "Assigned State",
			"LSN", "Health");

	fformat(stdout, "%*s-+-%*s-+-%*s-+-%17s-+-%17s-+-%17s-+-%6s\n",
			nodesArray->headers.maxNameSize,
			nodesArray->headers.nameSeparatorHeader,
			nodesArray->headers.maxNodeSize,
			nodesArray->headers.nodeSeparatorHeader,
			nodesArray->headers.maxHostSize,
			nodesArray->headers.hostSeparatorHeader,
			"-----------------", "-----------------", "-----------------",
			"------");
}


/*
 * nodestatePrintNodeState prints the node at the given position in the given
 * nodesArray, using the nodesArray pre-computed sizes for the dynamic columns.
 */
void
nodestatePrintNodeState(CurrentNodeStateArray *nodesArray,
						CurrentNodeState *nodeState)
{
	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };

	(void) nodestatePrepareNode(&(nodeState->node),
								nodeState->groupId,
								hostport,
								composedId);

	fformat(stdout, "%*s | %*s | %*s | %17s | %17s | %17s | %6s\n",
			nodesArray->headers.maxNameSize, nodeState->node.name,
			nodesArray->headers.maxNodeSize, composedId,
			nodesArray->headers.maxHostSize, hostport,
			NodeStateToString(nodeState->reportedState),
			NodeStateToString(nodeState->goalState),
			nodeState->node.lsn,
			nodeState->health == 1 ? "✓" : "✗");
}


/*
 * nodestatePrepareNode prepares the "host:port" and the "Node" computed
 * columns used to display a node. The hostport and composedId parameters must
 * be pre-allocated string buffers.
 */
void
nodestatePrepareNode(NodeAddress *node,
					 int groupId, char *hostport, char *composedId)
{
	sformat(hostport, BUFSIZE, "%s:%d", node->host, node->port);
	sformat(composedId, BUFSIZE, "%d/%d", groupId, node->nodeId);
}


/*
 * prepareHostNameSeparator fills in the pre-allocated given string with the
 * expected amount of dashes to use as a separator line in our tabular output.
 */
void
prepareHostNameSeparator(char nameSeparatorHeader[], int size)
{
	for (int i = 0; i <= size; i++)
	{
		if (i < size)
		{
			nameSeparatorHeader[i] = '-';
		}
		else
		{
			nameSeparatorHeader[i] = '\0';
			break;
		}
	}
}
