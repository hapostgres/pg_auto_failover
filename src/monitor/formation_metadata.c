/*-------------------------------------------------------------------------
 *
 * src/monitor/formation_metadata.c
 *
 * Implementation of functions related to formation metadata.
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

#include "health_check.h"
#include "metadata.h"
#include "formation_metadata.h"
#include "node_metadata.h"
#include "notifications.h"

#include "access/htup_details.h"
#include "access/xlogdefs.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "parser/parse_type.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/syscache.h"


PG_FUNCTION_INFO_V1(create_formation);
PG_FUNCTION_INFO_V1(drop_formation);
PG_FUNCTION_INFO_V1(enable_secondary);
PG_FUNCTION_INFO_V1(disable_secondary);
PG_FUNCTION_INFO_V1(set_formation_number_sync_standbys);

Datum AutoFailoverFormationGetDatum(FunctionCallInfo fcinfo,
									AutoFailoverFormation *formation);

/*
 * GetFormation returns an AutoFailoverFormation structure with the formationId
 * and its kind, when the formation has already been created, or NULL
 * otherwise.
 */
AutoFailoverFormation *
GetFormation(const char *formationId)
{
	AutoFailoverFormation *formation = NULL;
	MemoryContext callerContext = CurrentMemoryContext;

	Oid argTypes[] = {
		TEXTOID /* formationid */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId), /* formationid */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *selectQuery =
		"SELECT * FROM " AUTO_FAILOVER_FORMATION_TABLE " WHERE formationId = $1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
										  NULL, false, 1);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from " AUTO_FAILOVER_FORMATION_TABLE);
	}

	if (SPI_processed > 0)
	{
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);
		TupleDesc tupleDescriptor = SPI_tuptable->tupdesc;
		HeapTuple heapTuple = SPI_tuptable->vals[0];
		bool isNull = false;

		Datum formationId =
			heap_getattr(heapTuple, Anum_pgautofailover_formation_formationid,
						 tupleDescriptor, &isNull);
		Datum kind =
			heap_getattr(heapTuple, Anum_pgautofailover_formation_kind,
						 tupleDescriptor, &isNull);
		Datum dbname =
			heap_getattr(heapTuple, Anum_pgautofailover_formation_dbname,
						 tupleDescriptor, &isNull);
		Datum opt_secondary =
			heap_getattr(heapTuple, Anum_pgautofailover_formation_opt_secondary,
						 tupleDescriptor, &isNull);
		Datum number_sync_standbys =
			heap_getattr(heapTuple, Anum_pgautofailover_formation_number_sync_standbys,
						 tupleDescriptor, &isNull);

		formation =
			(AutoFailoverFormation *) palloc0(sizeof(AutoFailoverFormation));

		formation->formationId = TextDatumGetCString(formationId);
		formation->kind = FormationKindFromString(TextDatumGetCString(kind));
		strlcpy(formation->dbname, NameStr(*DatumGetName(dbname)), NAMEDATALEN);
		formation->opt_secondary = DatumGetBool(opt_secondary);
		formation->number_sync_standbys = DatumGetInt32(number_sync_standbys);

		MemoryContextSwitchTo(spiContext);
	}
	else
	{
		formation = NULL;
	}

	SPI_finish();

	return formation;
}


/*
 * create_formation inserts a new tuple in pgautofailover.formation table, of
 * the given formation kind. We know only two formation kind at the moment,
 * 'pgsql' and 'citus'. Support is only implemented for 'pgsql'.
 */
Datum
create_formation(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	text *formationKindText = PG_GETARG_TEXT_P(1);
	char *formationKindCString = text_to_cstring(formationKindText);
	FormationKind formationKind = FormationKindFromString(formationKindCString);
	Name formationDBNameName = PG_GETARG_NAME(2);
	bool formationOptionSecondary = PG_GETARG_BOOL(3);
	int formationNumberSyncStandbys = PG_GETARG_INT32(4);

	AddFormation(formationId, formationKind, formationDBNameName,
				 formationOptionSecondary, formationNumberSyncStandbys);

	AutoFailoverFormation *formation = GetFormation(formationId);
	Datum resultDatum = AutoFailoverFormationGetDatum(fcinfo, formation);

	PG_RETURN_DATUM(resultDatum);
}


/*
 * drop_formation removes a formation from the pgautofailover.formation table,
 * and may only succeed when no nodes belong to target formation. This is
 * checked by the foreign key reference installed in the pgautofailover schema.
 */
Datum
drop_formation(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	RemoveFormation(formationId);

	PG_RETURN_VOID();
}


/*
 * enable_secondary enables secondaries to be added to a formation. This is
 * done by changing the hassecondary field on pgautofailover.formation to true.
 * Subsequent nodes added to the formation will be assigned secondary of an
 * already running node as long as there are nodes without a secondary.
 */
Datum
enable_secondary(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	SetFormationOptSecondary(formationId, true);

	PG_RETURN_VOID();
}


/*
 * disable_secondary disables secondaries on a formation, it will only succeed
 * when no nodes of the formation are currently in the secondary role. This is
 * enforced by a trigger on the formation table.
 */
Datum
disable_secondary(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);

	SetFormationOptSecondary(formationId, false);

	PG_RETURN_VOID();
}


/*
 * AddFormation adds given formationId and kind to the pgautofailover.formation
 * table.
 *
 * It returns nothing: either the INSERT happened and we have the exact same
 * information as given in the table, or it failed and we raise an exception
 * here.
 */
void
AddFormation(const char *formationId,
			 FormationKind kind, Name dbname, bool optionSecondary, int
			 numberSyncStandbys)
{
	Oid argTypes[] = {
		TEXTOID, /* formationid */
		TEXTOID, /* kind */
		NAMEOID, /* dbname */
		BOOLOID, /* opt_secondary */
		INT4OID  /* number_sync_standbys */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),                 /* formationid */
		CStringGetTextDatum(FormationKindToString(kind)), /* kind */
		NameGetDatum(dbname),                             /* dbname */
		BoolGetDatum(optionSecondary),                    /* opt_secondary */
		Int32GetDatum(numberSyncStandbys)                 /* number_sync_standbys */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_FORMATION_TABLE
		" (formationid, kind, dbname, opt_secondary, number_sync_standbys)"
		" VALUES ($1, $2, $3, $4, $5)";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(insertQuery, argCount, argTypes,
										  argValues, NULL, false, 0);

	if (spiStatus != SPI_OK_INSERT)
	{
		elog(ERROR, "could not insert into " AUTO_FAILOVER_FORMATION_TABLE);
	}

	SPI_finish();
}


/*
 * RemoveFormation deletes a formation, erroring out if there are still nodes
 * attached to it. We use the foreign key declaration to protect against that
 * case.
 */
void
RemoveFormation(const char *formationId)
{
	Oid argTypes[] = {
		TEXTOID, /* formationId */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId) /* formationId */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *deleteQuery =
		"DELETE FROM " AUTO_FAILOVER_FORMATION_TABLE " WHERE formationid = $1";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(deleteQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);

	if (spiStatus != SPI_OK_DELETE)
	{
		elog(ERROR, "could not delete from " AUTO_FAILOVER_FORMATION_TABLE);
	}

	if (SPI_processed == 0)
	{
		elog(ERROR, "couldn't find formation \"%s\"", formationId);
	}
	else if (SPI_processed > 1)
	{
		/* that's a primary key index corruption or something nastly here. */
		elog(ERROR,
			 "formation name \"%s\" belongs to several formations",
			 formationId);
	}

	SPI_finish();
}


/*
 * SetFormationKind updates the formation kind to be the one given.
 */
void
SetFormationKind(const char *formationId, FormationKind kind)
{
	Oid argTypes[] = {
		TEXTOID, /* formationKind */
		TEXTOID  /* formationId */
	};

	Datum argValues[] = {
		CStringGetTextDatum(FormationKindToString(kind)), /* formationKind */
		CStringGetTextDatum(formationId)                  /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET kind = $1"
		" WHERE formationid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_FORMATION_TABLE);
	}

	SPI_finish();
}


/*
 * SetFormationDBName updates the formation dbname to be the one given.
 */
void
SetFormationDBName(const char *formationId, const char *dbname)
{
	Oid argTypes[] = {
		TEXTOID,     /* dbname */
		TEXTOID      /* formationId */
	};

	Datum argValues[] = {
		CStringGetTextDatum(dbname),         /* dbname */
		CStringGetTextDatum(formationId)     /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET dbname = $1"
		" WHERE formationid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_FORMATION_TABLE);
	}

	SPI_finish();
}


/*
 * SetFormationOptSecondary updates the formation to enable or disable
 * secondary nodes for a formation. When enabling the user is responsible for
 * adding new nodes to actually add secondaries to the formation. When
 * disabling the user should have shutdown the secondary nodes before, command
 * errors otherwise.
 */
void
SetFormationOptSecondary(const char *formationId, bool optSecondary)
{
	Oid argTypes[] = {
		BOOLOID,     /* opt_secondary */
		TEXTOID      /* formationId */
	};

	Datum argValues[] = {
		BoolGetDatum(optSecondary),          /* opt_secondary */
		CStringGetTextDatum(formationId)     /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET opt_secondary = $1"
		" WHERE formationid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);
	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_FORMATION_TABLE);
	}

	SPI_finish();
}


/*
 * FormationKindFromString returns an enum value for FormationKind when given a
 * text representation of the value.
 */
FormationKind
FormationKindFromString(const char *kind)
{
	FormationKind kindArray[] = {
		FORMATION_KIND_UNKNOWN,
		FORMATION_KIND_UNKNOWN,
		FORMATION_KIND_PGSQL,
		FORMATION_KIND_CITUS
	};
	char *kindList[] = { "", "unknown", "pgsql", "citus", NULL };


	for (int listIndex = 0; kindList[listIndex] != NULL; listIndex++)
	{
		char *candidate = kindList[listIndex];

		if (strcmp(kind, candidate) == 0)
		{
			return kindArray[listIndex];
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("unknown formation kind \"%s\"", kind)));

	/* never happens, make compiler happy */
	return FORMATION_KIND_UNKNOWN;
}


/*
 * FormationKindToString returns the string representation of a FormationKind.
 */
char *
FormationKindToString(FormationKind kind)
{
	switch (kind)
	{
		case FORMATION_KIND_UNKNOWN:
		{
			return "unknown";
		}

		case FORMATION_KIND_PGSQL:
		{
			return "pgsql";
		}

		case FORMATION_KIND_CITUS:
		{
			return "citus";
		}

		default:
			ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
							errmsg("unknown formation kind value %d", kind)));
	}

	/* keep compiler happy */
	return "";
}


/*
 * FormationKindFromNodeKindString returns a FormationKind value when given the
 * kind of a NODE in the formation: either standalone, coordinator, or worker.
 */
FormationKind
FormationKindFromNodeKindString(const char *nodeKind)
{
	FormationKind kindArray[] = {
		FORMATION_KIND_UNKNOWN,
		FORMATION_KIND_UNKNOWN,
		FORMATION_KIND_PGSQL,
		FORMATION_KIND_CITUS,
		FORMATION_KIND_CITUS
	};
	char *kindList[] = {
		"", "unknown", "standalone", "coordinator", "worker", NULL
	};

	for (int listIndex = 0; kindList[listIndex] != NULL; listIndex++)
	{
		char *candidate = kindList[listIndex];

		if (strcmp(nodeKind, candidate) == 0)
		{
			return kindArray[listIndex];
		}
	}

	ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("unknown formation kind \"%s\"", nodeKind)));

	/* never happens, make compiler happy */
	return FORMATION_KIND_UNKNOWN;
}


/*
 * IsCitusFormation returns whether the formation is a citus formation.
 */
bool
IsCitusFormation(AutoFailoverFormation *formation)
{
	return formation->kind == FORMATION_KIND_CITUS;
}


/*
 * set_formation_number_sync_standbys sets number_sync_standbys property of a
 * formation. The function returns true on success.
 */
Datum
set_formation_number_sync_standbys(PG_FUNCTION_ARGS)
{
	checkPgAutoFailoverVersion();

	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	int number_sync_standbys = PG_GETARG_INT32(1);

	AutoFailoverFormation *formation = GetFormation(formationId);

	/* at the moment, only test with the number of standbys in group 0 */
	int groupId = 0;
	int standbyCount = 0;


	char message[BUFSIZE] = { 0 };

	if (formation == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						errmsg("unknown formation \"%s\"", formationId)));
	}

	LockFormation(formationId, ExclusiveLock);

	if (number_sync_standbys < 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid value for number_sync_standbys: \"%d\"",
						number_sync_standbys),
				 errdetail("A non-negative integer is expected")));
	}

	AutoFailoverNode *primaryNode = GetPrimaryNodeInGroup(formation->formationId,
														  groupId);

	if (primaryNode == NULL)
	{
		ereport(ERROR,
				(errmsg("Couldn't find the primary node in formation \"%s\", "
						"group %d", formation->formationId, groupId)));
	}

	/*
	 * We require a stable group state to apply new formation settings.
	 *
	 * The classic stable state is of course both reported and goal state being
	 * "primary". That said, when number_sync_standbys is zero (0) and the
	 * standby nodes are unavailable, then another stable state is when both
	 * reported and goal state are "wait_primary".
	 */
	if (!IsCurrentState(primaryNode, REPLICATION_STATE_PRIMARY) &&
		!IsCurrentState(primaryNode, REPLICATION_STATE_WAIT_PRIMARY))
	{
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot set number_sync_standbys when current "
						"goal state for primary " NODE_FORMAT
						" is \"%s\", and current reported state is \"%s\"",
						NODE_FORMAT_ARGS(primaryNode),
						ReplicationStateGetName(primaryNode->goalState),
						ReplicationStateGetName(primaryNode->reportedState)),
				 errdetail("The primary node so must be in state \"primary\" "
						   "or \"wait_primary\" "
						   "to be able to apply configuration changes to "
						   "its synchronous_standby_names setting")));
	}

	/* set the formation property to see if that is a valid choice */
	formation->number_sync_standbys = number_sync_standbys;

	if (!FormationNumSyncStandbyIsValid(formation,
										primaryNode,
										groupId,
										&standbyCount))
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid value for number_sync_standbys: \"%d\"",
						number_sync_standbys),
				 errdetail("At least %d standby nodes are required, "
						   "and only %d are currently participating in "
						   "the replication quorum",
						   number_sync_standbys + 1, standbyCount)));
	}

	/* SetFormationNumberSyncStandbys reports ERROR when returning false */
	bool success = SetFormationNumberSyncStandbys(formationId, number_sync_standbys);

	/* and now ask the primary to change its settings */
	LogAndNotifyMessage(
		message, BUFSIZE,
		"Setting goal state of " NODE_FORMAT
		" to apply_settings "
		"after updating number_sync_standbys to %d for formation %s.",
		NODE_FORMAT_ARGS(primaryNode),
		formation->number_sync_standbys,
		formation->formationId);

	SetNodeGoalState(primaryNode, REPLICATION_STATE_APPLY_SETTINGS, message);

	PG_RETURN_BOOL(success);
}


/*
 * FormationNumSyncStandbyIsValid returns true if the current setting for
 * number_sync_standbys on the given formation makes sense with the registered
 * standbys.
 */
bool
FormationNumSyncStandbyIsValid(AutoFailoverFormation *formation,
							   AutoFailoverNode *primaryNode,
							   int groupId,
							   int *standbyCount)
{
	ListCell *nodeCell = NULL;
	int count = 0;

	if (formation == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("the given formation must not be NULL")));
	}

	List *standbyNodesGroupList = AutoFailoverOtherNodesList(primaryNode);

	foreach(nodeCell, standbyNodesGroupList)
	{
		AutoFailoverNode *node = (AutoFailoverNode *) lfirst(nodeCell);

		if (node->replicationQuorum)
		{
			++count;
		}
	}

	*standbyCount = count;

	/*
	 * number_sync_standbys = 0 is a special case in our FSM, because we have
	 * special handling of a missing standby then, switching to wait_primary to
	 * disable synchronous replication when the standby is not available.
	 *
	 * For other values (N) of number_sync_standbys, we require N+1 known
	 * standby nodes, so that you can lose a standby at any point in time and
	 * still accept writes. That's the service availability trade-off and cost.
	 */
	if (formation->number_sync_standbys == 0)
	{
		return true;
	}

	return (formation->number_sync_standbys + 1) <= count;
}


/*
 * SetFormationNumberSyncStandbys sets numberSyncStandbys property
 * of a formation entry. Returns true if successfull.
 */
bool
SetFormationNumberSyncStandbys(const char *formationId, int numberSyncStandbys)
{
	Oid argTypes[] = {
		INT4OID,     /* numberSyncStandbys */
		TEXTOID      /* formationId */
	};

	Datum argValues[] = {
		Int32GetDatum(numberSyncStandbys),     /* numberSyncStandbys */
		CStringGetTextDatum(formationId)      /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET number_sync_standbys = $1"
		" WHERE formationid = $2";

	SPI_connect();

	int spiStatus = SPI_execute_with_args(updateQuery,
										  argCount, argTypes, argValues,
										  NULL, false, 0);
	SPI_finish();

	if (spiStatus != SPI_OK_UPDATE)
	{
		elog(ERROR, "could not update " AUTO_FAILOVER_FORMATION_TABLE);
		return false;
	}

	return true;
}


/*
 * AutoFailoverFormationGetDatum prepares a Datum from given formation.
 * Caller is expected to provide fcinfo structure that contains compatible
 * call result type.
 */
Datum
AutoFailoverFormationGetDatum(FunctionCallInfo fcinfo, AutoFailoverFormation *formation)
{
	TupleDesc resultDescriptor = NULL;

	Datum values[5];
	bool isNulls[5];

	if (formation == NULL)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("the given formation must not be NULL")));
	}

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(formation->formationId);
	values[1] = CStringGetTextDatum(FormationKindToString(formation->kind));
	values[2] = CStringGetDatum(formation->dbname);
	values[3] = BoolGetDatum(formation->opt_secondary);
	values[4] = Int32GetDatum(formation->number_sync_standbys);

	TypeFuncClass resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	HeapTuple resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	Datum resultDatum = HeapTupleGetDatum(resultTuple);

	PG_RETURN_DATUM(resultDatum);
}
