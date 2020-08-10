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

	nodesArray->maxNameSize = 4;  /* "Name" */
	nodesArray->maxHostSize = 10; /* "Host:Port" */
	nodesArray->maxNodeSize = 5;  /* "Node" */

	/*
	 * Dynamically adjust our display output to the length of the longer
	 * hostname in the result set
	 */
	for (index = 0; index < nodesArray->count; index++)
	{
		CurrentNodeState *nodeState = &(nodesArray->nodes[index]);

		int nameLen = strlen(nodeState->node.name);

		/* compute strlen of host:port */
		IntString portString = intToString(nodeState->node.port);
		int hostLen =
			strlen(nodeState->node.host) + strlen(portString.strValue) + 1;

		/* compute strlen of groupId/nodeId, as in "0/1" */
		IntString nodeIdString = intToString(nodeState->node.nodeId);
		IntString groupIdString = intToString(nodeState->groupId);
		int nodeLen =
			strlen(groupIdString.strValue) + strlen(nodeIdString.strValue) + 1;

		if (nameLen > nodesArray->maxNameSize)
		{
			nodesArray->maxNameSize = nameLen;
		}

		if (hostLen > nodesArray->maxHostSize)
		{
			nodesArray->maxHostSize = hostLen;
		}

		if (nodeLen > nodesArray->maxNodeSize)
		{
			nodesArray->maxNodeSize = nodeLen;
		}
	}

	/* prepare a nice dynamic string of '-' as a header separator */
	(void) prepareHostNameSeparator(nodesArray->nameSeparatorHeader,
									nodesArray->maxNameSize);

	(void) prepareHostNameSeparator(nodesArray->hostSeparatorHeader,
									nodesArray->maxHostSize);

	(void) prepareHostNameSeparator(nodesArray->nodeSeparatorHeader,
									nodesArray->maxNodeSize);
}


/*
 * nodestatePrintHeader prints the given CurrentNodeStateArray header.
 */
void
nodestatePrintHeader(CurrentNodeStateArray *nodesArray)
{
	fformat(stdout, "%*s | %*s | %*s | %17s | %17s | %17s | %6s\n",
			nodesArray->maxNameSize, "Name",
			nodesArray->maxHostSize, "Host:Port",
			nodesArray->maxNodeSize, "Node",
			"Current State", "Assigned State",
			"LSN", "Health");

	fformat(stdout, "%*s-+-%*s-+-%*s-+-%17s-+-%17s-+-%17s-+-%6s\n",
			nodesArray->maxNameSize, nodesArray->nameSeparatorHeader,
			nodesArray->maxHostSize, nodesArray->hostSeparatorHeader,
			nodesArray->maxNodeSize, nodesArray->nodeSeparatorHeader,
			"-----------------", "-----------------", "-----------------",
			"------");
}


/*
 * nodestatePrintNodeState prints the node at the given position in the given
 * nodesArray, using the nodesArray pre-computed sizes for the dynamic columns.
 */
void
nodestatePrintNodeState(CurrentNodeStateArray *nodesArray, int position)
{
	CurrentNodeState *nodeState = &(nodesArray->nodes[position]);

	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };

	sformat(hostport, sizeof(hostport), "%s:%d",
			nodeState->node.host,
			nodeState->node.port);

	sformat(composedId, sizeof(hostport), "%d/%d",
			nodeState->groupId,
			nodeState->node.nodeId);

	fformat(stdout, "%*s | %*s | %*s | %17s | %17s | %17s | %6s\n",
			nodesArray->maxNameSize, nodeState->node.name,
			nodesArray->maxHostSize, hostport,
			nodesArray->maxNodeSize, composedId,
			NodeStateToString(nodeState->reportedState),
			NodeStateToString(nodeState->goalState),
			nodeState->node.lsn,
			nodeState->health == 1 ? "✓" : "✗");
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
