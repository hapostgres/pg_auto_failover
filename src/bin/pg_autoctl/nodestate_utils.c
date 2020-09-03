/*
 * src/bin/pg_autoctl/nodestate_utils.c
 *     Functions for printing node states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */

#include "file_utils.h"
#include "log.h"
#include "nodestate_utils.h"
#include "string_utils.h"

/*
 * nodestatePrepareHeaders computes the maximum length needed for variable
 * length columns and prepare the separation strings, filling them with the
 * right amount of dashes.
 */
void
nodestatePrepareHeaders(CurrentNodeStateArray *nodesArray,
						PgInstanceKind nodeKind)
{
	int index = 0;

	nodesArray->headers.nodeKind = nodeKind;

	nodesArray->headers.maxNameSize = 4;  /* "Name" */
	nodesArray->headers.maxHostSize = 10; /* "Host:Port" */
	nodesArray->headers.maxNodeSize = 5;  /* "Node" */
	nodesArray->headers.maxLSNSize = 3;   /* "LSN" */
	nodesArray->headers.maxStateSize = MAX_NODE_STATE_LEN;
	nodesArray->headers.maxHealthSize = strlen("Reachable");

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
							   NodeAddressArray *nodesArray,
							   int groupId,
							   PgInstanceKind nodeKind)
{
	int index = 0;

	headers->nodeKind = nodeKind;

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

	(void) prepareHostNameSeparator(headers->lsnSeparatorHeader,
									headers->maxLSNSize);

	(void) prepareHostNameSeparator(headers->stateSeparatorHeader,
									headers->maxStateSize);

	(void) prepareHostNameSeparator(headers->healthSeparatorHeader,
									headers->maxHealthSize);
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
	int nodeLen = 0;

	int lsnLen = strlen(node->lsn);

	switch (headers->nodeKind)
	{
		case NODE_KIND_STANDALONE:
		{
			nodeLen = strlen(nodeIdString.strValue);
			break;
		}

		default:
		{
			IntString groupIdString = intToString(groupId);

			nodeLen =
				strlen(groupIdString.strValue) +
				strlen(nodeIdString.strValue) + 1;

			break;
		}
	}

	/*
	 * In order to have a static nice table output even when using
	 * auto-refreshing commands such as `watch(1)` when states are changing, we
	 * always use the max known state length.
	 */
	headers->maxStateSize = MAX_NODE_STATE_LEN;

	/* initialize to mininum values, if needed */
	if (headers->maxNameSize == 0)
	{
		/* Name */
		headers->maxNameSize = strlen("Name");
	}

	if (headers->maxHostSize == 0)
	{
		/* Host:Port */
		headers->maxHostSize = strlen("Host:Port");
	}

	if (headers->maxNodeSize == 0)
	{
		/* groupId/nodeId */
		headers->maxNodeSize = 5;
	}

	if (headers->maxLSNSize == 0)
	{
		/* Unknown LSN is going to be "0/0" */
		headers->maxLSNSize = 3;
	}

	if (headers->maxHealthSize == 0)
	{
		/*
		 * Reachable, which is longer than "unknown", "yes", and "no", so
		 * that's all we use here, a static length actually. Which is good,
		 * because a NodeAddress does not know its own health anyway.
		 */
		headers->maxHealthSize = strlen("Reachable");
	}

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

	if (lsnLen > headers->maxLSNSize)
	{
		headers->maxLSNSize = lsnLen;
	}
}


/*
 * nodestatePrintHeader prints the given CurrentNodeStateArray header.
 */
void
nodestatePrintHeader(NodeAddressHeaders *headers)
{
	fformat(stdout, "%*s | %*s | %*s | %*s | %*s | %*s | %*s\n",
			headers->maxNameSize, "Name",
			headers->maxNodeSize, "Node",
			headers->maxHostSize, "Host:Port",
			headers->maxLSNSize, "LSN",
			headers->maxHealthSize, "Reachable",
			headers->maxStateSize, "Current State",
			headers->maxStateSize, "Assigned State");

	fformat(stdout, "%*s-+-%*s-+-%*s-+-%*s-+-%*s-+-%*s-+-%*s\n",
			headers->maxNameSize, headers->nameSeparatorHeader,
			headers->maxNodeSize, headers->nodeSeparatorHeader,
			headers->maxHostSize, headers->hostSeparatorHeader,
			headers->maxLSNSize, headers->lsnSeparatorHeader,
			headers->maxHealthSize, headers->healthSeparatorHeader,
			headers->maxStateSize, headers->stateSeparatorHeader,
			headers->maxStateSize, headers->stateSeparatorHeader);
}


/*
 * nodestatePrintNodeState prints the node at the given position in the given
 * nodesArray, using the nodesArray pre-computed sizes for the dynamic columns.
 */
void
nodestatePrintNodeState(NodeAddressHeaders *headers,
						CurrentNodeState *nodeState)
{
	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };

	(void) nodestatePrepareNode(headers,
								&(nodeState->node),
								nodeState->groupId,
								hostport,
								composedId);

	fformat(stdout, "%*s | %*s | %*s | %*s | %*s | %*s | %*s\n",
			headers->maxNameSize, nodeState->node.name,
			headers->maxNodeSize, composedId,
			headers->maxHostSize, hostport,
			headers->maxLSNSize, nodeState->node.lsn,
			headers->maxHealthSize, nodestateHealthToString(nodeState->health),
			headers->maxStateSize, NodeStateToString(nodeState->reportedState),
			headers->maxStateSize, NodeStateToString(nodeState->goalState));
}


/*
 * nodestatePrepareNode prepares the "host:port" and the "Node" computed
 * columns used to display a node. The hostport and composedId parameters must
 * be pre-allocated string buffers.
 */
void
nodestatePrepareNode(NodeAddressHeaders *headers, NodeAddress *node,
					 int groupId, char *hostport, char *composedId)
{
	sformat(hostport, BUFSIZE, "%s:%d", node->host, node->port);

	switch (headers->nodeKind)
	{
		case NODE_KIND_STANDALONE:
		{
			sformat(composedId, BUFSIZE, "%d", node->nodeId);
			break;
		}

		default:
		{
			sformat(composedId, BUFSIZE, "%d/%d", groupId, node->nodeId);
			break;
		}
	}
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


/*
 * nodestateAsJSON populates a given JSON_Value with an JSON object that mimics
 * the output from SELECT * FROM pgautofailover.current_state() by taking the
 * information bits from the given nodeState.
 */
bool
nodestateAsJSON(CurrentNodeState *nodeState, JSON_Value *js)
{
	JSON_Object *jsobj = json_value_get_object(js);

	/* same field names as SELECT * FROM pgautofailover.current_state() */
	json_object_set_number(jsobj, "node_id", (double) nodeState->node.nodeId);
	json_object_set_number(jsobj, "group_id", (double) nodeState->groupId);
	json_object_set_string(jsobj, "nodename", nodeState->node.name);
	json_object_set_string(jsobj, "nodehost", nodeState->node.host);
	json_object_set_number(jsobj, "nodeport", (double) nodeState->node.port);

	json_object_set_string(jsobj, "current_group_state",
						   NodeStateToString(nodeState->reportedState));

	json_object_set_string(jsobj, "assigned_group_state",
						   NodeStateToString(nodeState->goalState));

	json_object_set_string(jsobj, "Minimum Recovery Ending LSN",
						   nodeState->node.lsn);

	json_object_set_string(jsobj, "reachable",
						   nodestateHealthToString(nodeState->health));

	return true;
}


/*
 * Transform the health column from a monitor into a string.
 */
char *
nodestateHealthToString(int health)
{
	switch (health)
	{
		case -1:
		{
			return "unknown";
		}

		case 0:
		{
			return "no";
		}

		case 1:
		{
			return "yes";
		}

		default:
		{
			log_error("BUG in nodestateHealthToString: health = %d", health);
			return "unknown";
		}
	}
}
