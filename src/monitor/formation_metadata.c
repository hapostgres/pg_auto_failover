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
	int spiStatus = 0;

	const char *selectQuery =
		"SELECT * FROM " AUTO_FAILOVER_FORMATION_TABLE " WHERE formationId = $1";

	SPI_connect();

	spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
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

		formation =
			(AutoFailoverFormation *) palloc0(sizeof(AutoFailoverFormation));

		formation->formationId = TextDatumGetCString(formationId);
		formation->kind = FormationKindFromString(TextDatumGetCString(kind));
		strlcpy(formation->dbname, NameStr(*DatumGetName(dbname)), NAMEDATALEN);
		formation->opt_secondary = DatumGetBool(opt_secondary);

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
	text *formationIdText = PG_GETARG_TEXT_P(0);
	char *formationId = text_to_cstring(formationIdText);
	text *formationKindText = PG_GETARG_TEXT_P(1);
	char *formationKindCString = text_to_cstring(formationKindText);
	FormationKind formationKind = FormationKindFromString(formationKindCString);
	Name formationDBNameName = PG_GETARG_NAME(2);
	bool formationOptionSecondary = PG_GETARG_BOOL(3);

	TupleDesc resultDescriptor = NULL;
	TypeFuncClass resultTypeClass = 0;
	Datum resultDatum = 0;
	HeapTuple resultTuple = NULL;
	Datum values[4];
	bool isNulls[4];

	AddFormation(formationId, formationKind, formationDBNameName, formationOptionSecondary);

	memset(values, 0, sizeof(values));
	memset(isNulls, false, sizeof(isNulls));

	values[0] = CStringGetTextDatum(formationId);
	values[1] = CStringGetTextDatum(formationKindCString);
	values[2] = NameGetDatum(formationDBNameName);
	values[3] = BoolGetDatum(formationOptionSecondary);

	resultTypeClass = get_call_result_type(fcinfo, NULL, &resultDescriptor);
	if (resultTypeClass != TYPEFUNC_COMPOSITE)
	{
		ereport(ERROR, (errmsg("return type must be a row type")));
	}

	resultTuple = heap_form_tuple(resultDescriptor, values, isNulls);
	resultDatum = HeapTupleGetDatum(resultTuple);

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
			 FormationKind kind, Name dbname, bool optionSecondary)
{
	Oid argTypes[] = {
		TEXTOID, /* formationid */
		TEXTOID, /* kind */
		NAMEOID, /* dbname */
		BOOLOID  /* opt_secondary */
	};

	Datum argValues[] = {
		CStringGetTextDatum(formationId),				  /* formationid */
		CStringGetTextDatum(FormationKindToString(kind)), /* kind */
		NameGetDatum(dbname),                             /* dbname */
		BoolGetDatum(optionSecondary)                     /* opt_secondary */
	};

	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *insertQuery =
		"INSERT INTO " AUTO_FAILOVER_FORMATION_TABLE
		" (formationid, kind, dbname, opt_secondary)"
		" VALUES ($1, $2, $3, $4)";

	SPI_connect();

	spiStatus = SPI_execute_with_args(insertQuery, argCount, argTypes,
									  argValues, NULL, false, 0);

	if (spiStatus != SPI_OK_INSERT)
	{
		elog(ERROR, "could not insert into " AUTO_FAILOVER_NODE_TABLE);
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
	int spiStatus = 0;

	const char *deleteQuery =
		"DELETE FROM " AUTO_FAILOVER_FORMATION_TABLE " WHERE formationid = $1";

	SPI_connect();

	spiStatus = SPI_execute_with_args(deleteQuery,
									  argCount, argTypes, argValues,
									  NULL, false, 0);

	if (spiStatus != SPI_OK_DELETE)
	{
		elog(ERROR, "could not delete from " AUTO_FAILOVER_NODE_TABLE);
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
		TEXTOID	 /* formationId */
	};

	Datum argValues[] = {
		CStringGetTextDatum(FormationKindToString(kind)), /* formationKind */
		CStringGetTextDatum(formationId)				  /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET kind = $1"
		" WHERE formationid = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
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
			TEXTOID, /* dbname */
			TEXTOID	 /* formationId */
	};

	Datum argValues[] = {
			CStringGetTextDatum(dbname),     /* dbname */
			CStringGetTextDatum(formationId) /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
		"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
		" SET dbname = $1"
		" WHERE formationid = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
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
			BOOLOID, /* opt_secondary */
			TEXTOID	 /* formationId */
	};

	Datum argValues[] = {
			BoolGetDatum(optSecondary),      /* opt_secondary */
			CStringGetTextDatum(formationId) /* formationId */
	};
	const int argCount = sizeof(argValues) / sizeof(argValues[0]);
	int spiStatus = 0;

	const char *updateQuery =
			"UPDATE " AUTO_FAILOVER_FORMATION_TABLE
			" SET opt_secondary = $1"
			" WHERE formationid = $2";

	SPI_connect();

	spiStatus = SPI_execute_with_args(updateQuery,
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
	FormationKind kindArray[] = { FORMATION_KIND_UNKNOWN,
								  FORMATION_KIND_UNKNOWN,
								  FORMATION_KIND_PGSQL,
								  FORMATION_KIND_CITUS };
	char *kindList[] = {"", "unknown", "pgsql", "citus", NULL };


	for(int listIndex = 0; kindList[listIndex] != NULL; listIndex++)
	{
		char *candidate = kindList[listIndex];

		if (strcmp(kind, candidate) == 0)
		{
			return kindArray[listIndex];
		}
	}

	ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
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
			return "unknown";

		case FORMATION_KIND_PGSQL:
			return "pgsql";

		case FORMATION_KIND_CITUS:
			return "citus";

		default:
			ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
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
	FormationKind kindArray[] = { FORMATION_KIND_UNKNOWN,
								  FORMATION_KIND_UNKNOWN,
								  FORMATION_KIND_PGSQL,
								  FORMATION_KIND_CITUS,
								  FORMATION_KIND_CITUS };
	char *kindList[] = {
		"", "unknown", "standalone", "coordinator", "worker", NULL
	};

	for(int listIndex = 0; kindList[listIndex] != NULL; listIndex++)
	{
		char *candidate = kindList[listIndex];

		if (strcmp(nodeKind, candidate) == 0)
		{
			return kindArray[listIndex];
		}
	}

	ereport(ERROR, (ERRCODE_INVALID_PARAMETER_VALUE,
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
