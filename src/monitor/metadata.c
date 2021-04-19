/*-------------------------------------------------------------------------
 *
 * src/monitor/metadata.c
 *
 * Implementation of functions related to pg_auto_failover metadata.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"
#include "fmgr.h"

#include "metadata.h"
#include "version_compat.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_type.h"
#include "commands/sequence.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/hsearch.h"
#include "utils/rel.h"
#include "utils/relcache.h"

bool EnableVersionChecks = true; /* version checks are enabled */

/*
 * pgAutoFailoverRelationId returns the OID of a given relation in the
 * pgautofailover schema.
 */
Oid
pgAutoFailoverRelationId(const char *relname)
{
	Oid namespaceId = pgAutoFailoverSchemaId();

	Oid relationId = get_relname_relid(relname, namespaceId);
	if (relationId == InvalidOid)
	{
		ereport(ERROR, (errmsg("%s does not exist", relname)));
	}

	return relationId;
}


/*
 * pgAutoFailoverSchemaId returns the name of the schema in which metadata is
 * stored.
 */
Oid
pgAutoFailoverSchemaId(void)
{
	Oid namespaceId = get_namespace_oid(AUTO_FAILOVER_SCHEMA_NAME, true);
	if (namespaceId == InvalidOid)
	{
		ereport(ERROR,
				(errmsg("%s schema does not exist", AUTO_FAILOVER_SCHEMA_NAME),
				 errhint("Run: CREATE EXTENSION %s",
						 AUTO_FAILOVER_EXTENSION_NAME)));
	}

	return namespaceId;
}


/*
 * pgAutoFailoverExtensionOwner gets the owner of the extension and verifies
 * that this is the superuser.
 */
Oid
pgAutoFailoverExtensionOwner(void)
{
	ScanKeyData scanKey[1];
	bool indexOK = true;
	Form_pg_extension extensionForm = NULL;
	Oid extensionOwner = InvalidOid;

	Relation pgExtension = heap_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&scanKey[0], Anum_pg_extension_extname, BTEqualStrategyNumber,
				F_NAMEEQ, CStringGetDatum(AUTO_FAILOVER_EXTENSION_NAME));

	SysScanDesc scanDescriptor = systable_beginscan(pgExtension, ExtensionNameIndexId,
													indexOK,
													NULL, 1, scanKey);

	HeapTuple extensionTuple = systable_getnext(scanDescriptor);

	if (HeapTupleIsValid(extensionTuple))
	{
		extensionForm = (Form_pg_extension) GETSTRUCT(extensionTuple);

		if (!superuser_arg(extensionForm->extowner))
		{
			ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
							errmsg("extension needs to be owned by superuser")));
		}

		extensionOwner = extensionForm->extowner;
		Assert(OidIsValid(extensionOwner));
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						errmsg("extension not loaded"),
						errhint("Run: CREATE EXTENSION %s",
								AUTO_FAILOVER_EXTENSION_NAME)));
	}

	systable_endscan(scanDescriptor);
	heap_close(pgExtension, AccessShareLock);

	return extensionOwner;
}


/*
 * LockFormation takes a lock on a formation to prevent concurrent
 * membership changes.
 */
void
LockFormation(char *formationId, LOCKMODE lockMode)
{
	LOCKTAG tag;
	const bool sessionLock = false;
	const bool dontWait = false;

	uint32 formationIdHash = string_hash(formationId, NAMEDATALEN);

	SET_LOCKTAG_ADVISORY(tag, MyDatabaseId, 0, formationIdHash,
						 ADV_LOCKTAG_CLASS_AUTO_FAILOVER_FORMATION);

	(void) LockAcquire(&tag, lockMode, sessionLock, dontWait);
}


/*
 * LockNodeGroup takes a lock on a particular group in a formation to
 * prevent concurrent state changes.
 */
void
LockNodeGroup(char *formationId, int groupId, LOCKMODE lockMode)
{
	LOCKTAG tag;
	const bool sessionLock = false;
	const bool dontWait = false;

	uint32 formationIdHash = string_hash(formationId, NAMEDATALEN);

	SET_LOCKTAG_ADVISORY(tag, MyDatabaseId, formationIdHash, (uint32) groupId,
						 ADV_LOCKTAG_CLASS_AUTO_FAILOVER_NODE_GROUP);

	(void) LockAcquire(&tag, lockMode, sessionLock, dontWait);
}


/*
 * checkPgAutoFailoverVersion checks whether there is a version mismatch
 * between the available version and the loaded version or between the
 * installed version and the loaded version. Returns true if compatible, false
 * otherwise.
 *
 * We need to be careful that the pgautofailover.so that is currently loaded in
 * the Postgres backend is intended to work with the current extension version
 * definition (schema and SQL definitions of C coded functions).
 */
bool
checkPgAutoFailoverVersion()
{
	char *installedVersion = NULL;
	char *availableVersion = NULL;

	const int argCount = 1;
	Oid argTypes[] = { TEXTOID };
	Datum argValues[] = { CStringGetTextDatum(AUTO_FAILOVER_EXTENSION_NAME) };
	MemoryContext callerContext = CurrentMemoryContext;

	char *selectQuery =
		"SELECT default_version, installed_version "
		"FROM pg_catalog.pg_available_extensions WHERE name = $1;";

	if (!EnableVersionChecks)
	{
		return true;
	}

	SPI_connect();

	int spiStatus = SPI_execute_with_args(selectQuery, argCount, argTypes, argValues,
										  NULL, false, 1);
	if (spiStatus != SPI_OK_SELECT)
	{
		elog(ERROR, "could not select from pg_catalog.pg_available_extensions");
	}

	if (SPI_processed != 1)
	{
		elog(ERROR, "expected a single entry for extension \"%s\"",
			 AUTO_FAILOVER_EXTENSION_NAME);
	}
	else
	{
		TupleDesc tupleDescriptor = SPI_tuptable->tupdesc;
		HeapTuple heapTuple = SPI_tuptable->vals[0];
		bool defaultIsNull = false, installedIsNull = false;
		MemoryContext spiContext = MemoryContextSwitchTo(callerContext);

		Datum defaultVersionDatum =
			heap_getattr(heapTuple, 1, tupleDescriptor, &defaultIsNull);

		Datum installedVersionDatum =
			heap_getattr(heapTuple, 2, tupleDescriptor, &installedIsNull);

		if (!defaultIsNull)
		{
			availableVersion = TextDatumGetCString(defaultVersionDatum);
		}

		if (!installedIsNull)
		{
			installedVersion = TextDatumGetCString(installedVersionDatum);
		}

		MemoryContextSwitchTo(spiContext);
	}

	SPI_finish();

	if (strcmp(AUTO_FAILOVER_EXTENSION_VERSION, availableVersion) != 0)
	{
		ereport(ERROR,
				(errmsg("loaded \"%s\" library version differs from latest "
						"available extension version",
						AUTO_FAILOVER_EXTENSION_NAME),
				 errdetail("Loaded library requires %s, but the latest control "
						   "file specifies %s.",
						   AUTO_FAILOVER_EXTENSION_VERSION,
						   availableVersion),
				 errhint("Restart the database to load the latest version "
						 "of the \"%s\" library.",
						 AUTO_FAILOVER_EXTENSION_NAME)));
		return false;
	}

	if (strcmp(AUTO_FAILOVER_EXTENSION_VERSION, installedVersion) != 0)
	{
		ereport(ERROR,
				(errmsg("loaded \"%s\" library version differs from installed "
						"extension version",
						AUTO_FAILOVER_EXTENSION_NAME),
				 errdetail("Loaded library requires %s, but the installed "
						   "extension version is %s.",
						   AUTO_FAILOVER_EXTENSION_VERSION,
						   installedVersion),
				 errhint("Run ALTER EXTENSION %s UPDATE and try again.",
						 AUTO_FAILOVER_EXTENSION_NAME)));
		return false;
	}

	return true;
}
