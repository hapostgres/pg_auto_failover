/*
 * src/bin/pg_autoctl/nodestate_utils.c
 *     Functions for printing node states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>

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
	nodesArray->headers.maxLSNSize = 9;   /* "TLI:  LSN" */
	nodesArray->headers.maxStateSize = MAX_NODE_STATE_LEN;
	nodesArray->headers.maxHealthSize = strlen("read-write *");

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
	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };
	char tliLSN[BUFSIZE] = { 0 };

	(void) nodestatePrepareNode(headers,
								node,
								groupId,
								hostport,
								composedId,
								tliLSN);

	int nameLen = strlen(node->name);
	int hostLen = strlen(hostport);
	int nodeLen = strlen(composedId);
	int lsnLen = strlen(tliLSN);

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
		/* Unknown LSN is going to be "  1: 0/0" */
		headers->maxLSNSize = 9;
	}

	if (headers->maxHealthSize == 0)
	{
		/*
		 * Connection is one of "read-only", "read-write", or "unknown",
		 * followed by a mark for the health check (*, !, or ?), so we need as
		 * much space as the full sample "read-write *":
		 */
		headers->maxHealthSize = strlen("read-write *");
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
			headers->maxLSNSize, "TLI: LSN",
			headers->maxHealthSize, "Connection",
			headers->maxStateSize, "Reported State",
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
	char tliLSN[BUFSIZE] = { 0 };
	char connection[BUFSIZE] = { 0 };
	char healthChar = nodestateHealthToChar(nodeState->health);

	(void) nodestatePrepareNode(headers,
								&(nodeState->node),
								nodeState->groupId,
								hostport,
								composedId,
								tliLSN);

	if (healthChar == ' ')
	{
		sformat(connection, BUFSIZE, "%s", nodestateConnectionType(nodeState));
	}
	else
	{
		sformat(connection, BUFSIZE, "%s %c",
				nodestateConnectionType(nodeState), healthChar);
	}

	fformat(stdout, "%*s | %*s | %*s | %*s | %*s | %*s | %*s\n",
			headers->maxNameSize, nodeState->node.name,
			headers->maxNodeSize, composedId,
			headers->maxHostSize, hostport,
			headers->maxLSNSize, tliLSN,
			headers->maxHealthSize, connection,
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
					 int groupId, char *hostport,
					 char *composedId, char *tliLSN)
{
	sformat(hostport, BUFSIZE, "%s:%d", node->host, node->port);
	sformat(tliLSN, BUFSIZE, "%3d: %s", node->tli, node->lsn);

	switch (headers->nodeKind)
	{
		case NODE_KIND_STANDALONE:
		{
			sformat(composedId, BUFSIZE, "%" PRId64, node->nodeId);
			break;
		}

		default:
		{
			sformat(composedId, BUFSIZE, "%d/%" PRId64, groupId, node->nodeId);
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

	json_object_set_number(jsobj, "timeline", (double) nodeState->node.tli);

	json_object_set_string(jsobj, "Minimum Recovery Ending LSN",
						   nodeState->node.lsn);

	json_object_set_string(jsobj, "reachable",
						   nodestateHealthToString(nodeState->health));

	json_object_set_string(jsobj, "conntype",
						   nodestateConnectionType(nodeState));

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


/*
 * Transform the health column from a monitor into a single char.
 */
char
nodestateHealthToChar(int health)
{
	switch (health)
	{
		case -1:
		{
			return '?';
		}

		case 0:
		{
			return '!';
		}

		case 1:
		{
			return ' ';
		}

		default:
		{
			log_error("BUG in nodestateHealthToString: health = %d", health);
			return '-';
		}
	}
}


/*
 * nodestateConnectionType returns one of "read-write" or "read-only".
 */
char *
nodestateConnectionType(CurrentNodeState *nodeState)
{
	switch (nodeState->reportedState)
	{
		case SINGLE_STATE:
		case PRIMARY_STATE:
		case WAIT_PRIMARY_STATE:
		case JOIN_PRIMARY_STATE:
		case PREPARE_MAINTENANCE_STATE:
		case APPLY_SETTINGS_STATE:
		{
			return "read-write";
		}

		case SECONDARY_STATE:
		case CATCHINGUP_STATE:
		case PREP_PROMOTION_STATE:
		case STOP_REPLICATION_STATE:
		case WAIT_MAINTENANCE_STATE:
		case FAST_FORWARD_STATE:
		case JOIN_SECONDARY_STATE:
		case REPORT_LSN_STATE:
		{
			return "read-only";
		}

		/* in those states Postgres is known to be stopped/down */
		case NO_STATE:
		case INIT_STATE:
		case DROPPED_STATE:
		case WAIT_STANDBY_STATE:
		case DEMOTED_STATE:
		case DEMOTE_TIMEOUT_STATE:
		case DRAINING_STATE:
		case MAINTENANCE_STATE:
		{
			return "none";
		}

		case ANY_STATE:
		{
			return "unknown";
		}

			/* default: is intentionally left out to have compiler check */
	}

	return "unknown";
}


/*
 * nodestate_log logs a CurrentNodeState, usually that comes from a
 * notification message we parse.
 */
void
nodestate_log(CurrentNodeState *nodeState, int logLevel, int64_t nodeId)
{
	if (nodeState->node.nodeId == nodeId)
	{
		log_level(logLevel,
				  "New state for this node "
				  "(node %" PRId64 ", \"%s\") (%s:%d): %s ➜ %s",
				  nodeState->node.nodeId,
				  nodeState->node.name,
				  nodeState->node.host,
				  nodeState->node.port,
				  NodeStateToString(nodeState->reportedState),
				  NodeStateToString(nodeState->goalState));
	}
	else
	{
		log_level(logLevel,
				  "New state for node %" PRId64 " \"%s\" (%s:%d): %s ➜ %s",
				  nodeState->node.nodeId,
				  nodeState->node.name,
				  nodeState->node.host,
				  nodeState->node.port,
				  NodeStateToString(nodeState->reportedState),
				  NodeStateToString(nodeState->goalState));
	}
}


/*
 * printCurrentState loops over pgautofailover.current_state() results and prints
 * them, one per line.
 */
void
printNodeArray(NodeAddressArray *nodesArray)
{
	NodeAddressHeaders headers = { 0 };

	/* We diplsay nodes all from the same group and don't have their groupId */
	(void) nodeAddressArrayPrepareHeaders(&headers,
										  nodesArray,
										  0,
										  NODE_KIND_STANDALONE);

	(void) printNodeHeader(&headers);

	for (int index = 0; index < nodesArray->count; index++)
	{
		NodeAddress *node = &(nodesArray->nodes[index]);

		printNodeEntry(&headers, node);
	}

	fformat(stdout, "\n");
}


/*
 * printNodeHeader pretty prints a header for a node list.
 */
void
printNodeHeader(NodeAddressHeaders *headers)
{
	fformat(stdout, "%*s | %*s | %*s | %21s | %8s\n",
			headers->maxNameSize, "Name",
			headers->maxNodeSize, "Node",
			headers->maxHostSize, "Host:Port",
			"TLI: LSN",
			"Primary?");

	fformat(stdout, "%*s-+-%*s-+-%*s-+-%21s-+-%8s\n",
			headers->maxNameSize, headers->nameSeparatorHeader,
			headers->maxNodeSize, headers->nodeSeparatorHeader,
			headers->maxHostSize, headers->hostSeparatorHeader,
			"------------------", "--------");
}


/*
 * printNodeEntry pretty prints a node.
 */
void
printNodeEntry(NodeAddressHeaders *headers, NodeAddress *node)
{
	char hostport[BUFSIZE] = { 0 };
	char composedId[BUFSIZE] = { 0 };
	char tliLSN[BUFSIZE] = { 0 };

	(void) nodestatePrepareNode(headers, node, 0, hostport, composedId, tliLSN);

	fformat(stdout, "%*s | %*s | %*s | %21s | %8s\n",
			headers->maxNameSize, node->name,
			headers->maxNodeSize, composedId,
			headers->maxHostSize, hostport,
			tliLSN,
			node->isPrimary ? "yes" : "no");
}


/*
 * nodestateFilterArrayGroup filters the given nodesArray to only the nodes
 * that are in the same group as the given node name.
 */
bool
nodestateFilterArrayGroup(CurrentNodeStateArray *nodesArray, const char *name)
{
	int groupId = -1;
	CurrentNodeStateArray nodesInSameGroup = { 0 };

	/* first, find the groupId of the target node name */
	for (int index = 0; index < nodesArray->count; index++)
	{
		CurrentNodeState *nodeState = &(nodesArray->nodes[index]);

		if (strcmp(nodeState->node.name, name) == 0)
		{
			groupId = nodeState->groupId;

			break;
		}
	}

	/* return false when the node name was not found */
	if (groupId == -1)
	{
		/* turn the given nodesArray into a all-zero empty array */
		memset(nodesArray, 0, sizeof(CurrentNodeStateArray));

		return false;
	}

	/* now, build a new nodesArray with only the nodes in the same group */
	for (int index = 0; index < nodesArray->count; index++)
	{
		CurrentNodeState *nodeState = &(nodesArray->nodes[index]);

		if (nodeState->groupId == groupId)
		{
			nodesInSameGroup.nodes[nodesInSameGroup.count] = *nodeState;
			++nodesInSameGroup.count;
		}
	}

	/*
	 * Finally, override the nodesArray parameter with the new contents. Note
	 * that we want to preserve the headers.
	 */
	NodeAddressHeaders headers = nodesArray->headers;

	*nodesArray = nodesInSameGroup;
	nodesArray->headers = headers;

	return true;
}
