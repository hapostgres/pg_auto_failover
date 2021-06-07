/*-------------------------------------------------------------------------
 *
 * src/monitor/archiver_protocol.c
 *
 * Implementation of the functions used to communicate with PostgreSQL
 * nodes.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "access/xact.h"

#include "archiver_metadata.h"
#include "group_state_machine.h"
#include "metadata.h"
#include "notifications.h"

#include "access/htup_details.h"
#include "access/xlogdefs.h"
#include "utils/builtins.h"

/* SQL-callable function declarations */
PG_FUNCTION_INFO_V1(register_archiver);
PG_FUNCTION_INFO_V1(register_archiver_node);
PG_FUNCTION_INFO_V1(remove_archiver_by_archiverid);


/*
 * register_node adds a node to a given formation
 *
 * At register time the monitor connects to the node to check that nodehost and
 * nodeport are valid, and it does a SELECT pg_is_in_recovery() to help decide
 * what initial role to attribute the entering node.
 */
Datum
register_archiver(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *nodeNameText = PG_GETARG_TEXT_P(0);
	char *nodeName = text_to_cstring(nodeNameText);

	text *nodeHostText = PG_GETARG_TEXT_P(1);
	char *nodeHost = text_to_cstring(nodeHostText);

	/*
	 * We want to benefit from the STRICT aspect of the function declaration at
	 * the SQL level to bypass checking all the arguments one after the other,
	 * but at the same time we don't want to force a name to be given, we can
	 * assign a default name to an archiver ("archiver_%d").
	 *
	 * Omitting the nodename argument is the same as passing in an explicit
	 * empty string (''), and results in the code here assigning a default
	 * archiver name.
	 */
	int archiverId =
		AddArchiver(
			strcmp(nodeName, "") == 0 ? NULL : nodeName,
			nodeHost);

	AutoFailoverArchiver *archiver = GetArchiver(archiverId);

	if (archiver == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("archiver host \"%s\" with name \"%s\" could "
						"not be registered: "
						"could not get information for node that was inserted",
						nodeHost, nodeName)));
	}
	else
	{
		char message[BUFSIZE] = { 0 };

		LogAndNotifyMessage(
			message, BUFSIZE,
			"Registering archiver %d \"%s\" (\"%s\")",
			archiver->archiverId, archiver->nodeName, archiver->nodeHost);
	}

	/* Add a default set of policies for the monitor */
	AddArchiverPolicyForMonitor(archiver);

	/* Now return our result tuple */
	TupleDesc resultDescriptor = NULL;
	Datum values[3];
	bool isNulls[3];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(archiver->archiverId);
	values[1] = CStringGetTextDatum(archiver->nodeName);
	values[2] = CStringGetTextDatum(archiver->nodeHost);

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * register_archiver_node adds an archiver node to a given formation
 */
Datum
register_archiver_node(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int32 archiverId = PG_GETARG_INT32(0);

	text *formationIdText = PG_GETARG_TEXT_P(1);
	char *formationId = text_to_cstring(formationIdText);

	text *nodeHostText = PG_GETARG_TEXT_P(2);
	char *nodeHost = text_to_cstring(nodeHostText);
	int32 nodePort = PG_GETARG_INT32(3);

	Name dbnameName = PG_GETARG_NAME(4);
	char *expectedDBName = NameStr(*dbnameName);

	text *nodeNameText = PG_GETARG_TEXT_P(5);
	char *nodeName = text_to_cstring(nodeNameText);

	uint64 sysIdentifier = PG_GETARG_INT64(6);

	int32 currentNodeId = PG_GETARG_INT32(7);
	int32 currentGroupId = PG_GETARG_INT32(8);
	Oid currentReplicationStateOid = PG_GETARG_OID(9);

	text *nodeKindText = PG_GETARG_TEXT_P(10);
	char *nodeKind = text_to_cstring(nodeKindText);

	bool replicationQuorum = PG_GETARG_BOOL(11);

	AutoFailoverNodeState currentNodeState = { 0 };

	currentNodeState.nodeId = currentNodeId;
	currentNodeState.groupId = currentGroupId;
	currentNodeState.replicationState =
		EnumGetReplicationState(currentReplicationStateOid);
	currentNodeState.reportedLSN = 0;
	currentNodeState.candidatePriority = 0;
	currentNodeState.replicationQuorum = replicationQuorum;

	/* when registering an archiver node, the target group id must exists */
	List *nodesGroupList = AutoFailoverNodeGroup(formationId, currentGroupId);

	if (list_length(nodesGroupList) == 0)
	{
		ereport(ERROR,
				(errmsg("group %d in formation \"%s\" is empty",
						currentGroupId, formationId)));
	}

	AutoFailoverArchiver *archiver = GetArchiver(archiverId);

	if (archiver == NULL)
	{
		ereport(ERROR,
				(errmsg("couldn't find archiver with id %d", archiverId)));
	}

	/*
	 * We want the benefits of a STRICT function (we don't have to check any of
	 * the 12 args to see if they might be NULL), and we also want to have an
	 * optional nodename parameter.
	 */
	if (strcmp(nodeName, "") == 0)
	{
		StringInfo archiverNodeName = makeStringInfo();

		/* pgautofailover.archiver_node has UNIQUE (archiverid, groupid) */
		appendStringInfo(archiverNodeName,
						 "archiver_node_%d_%d",
						 archiverId,
						 currentGroupId);
	}

	/*
	 * TODO: compute the nodeName for the archiver if needed, something like
	 * 'archiver_node_%d'.
	 */
	AutoFailoverNodeRegistration nodeRegistration = {
		.formationId = formationId,
		.currentNodeState = &currentNodeState,
		.nodeName = nodeName,
		.nodeHost = nodeHost,
		.nodePort = nodePort,
		.expectedDBName = expectedDBName,
		.sysIdentifier = sysIdentifier,
		.nodeKind = nodeKind,
		.nodeCluster = "default",
		.pgAutoFailoverNode = NULL
	};

	/* first, register a new node */
	AutoFailoverNodeState *assignedNodeState = RegisterNode(&nodeRegistration);

	AutoFailoverNode *pgAutoFailoverNode = nodeRegistration.pgAutoFailoverNode;

	/* now, register an archiver_node that uses the new nodeid */
	AddArchiverNode(archiver,
					assignedNodeState->nodeId,
					assignedNodeState->groupId);

	TupleDesc resultDescriptor = NULL;
	Datum values[6];
	bool isNulls[6];

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = Int32GetDatum(assignedNodeState->nodeId);
	values[1] = Int32GetDatum(assignedNodeState->groupId);
	values[2] = ObjectIdGetDatum(
		ReplicationStateGetEnum(pgAutoFailoverNode->goalState));
	values[3] = Int32GetDatum(assignedNodeState->candidatePriority);
	values[4] = BoolGetDatum(assignedNodeState->replicationQuorum);
	values[5] = CStringGetTextDatum(pgAutoFailoverNode->nodeName);

	TypeFuncClass resultTypeClass =
		get_call_result_type(fcinfo, NULL, &resultDescriptor);

	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * remove_archiver_by_archiverid removes given archiver by id.
 */
Datum
remove_archiver_by_archiverid(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	int32 archiverId = PG_GETARG_INT32(0);

	AutoFailoverArchiver *archiver = GetArchiver(archiverId);

	if (archiver == NULL)
	{
		ereport(ERROR,
				(errmsg("couldn't find archiver with id %d", archiverId)));
	}

	/* elog(ERROR) when an error occurs */
	RemoveArchiver(archiver);

	PG_RETURN_BOOL(true);
}
