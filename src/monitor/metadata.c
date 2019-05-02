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

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/tupdesc.h"
#include "access/xact.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_extension.h"
#include "commands/sequence.h"
#include "lib/stringinfo.h"
#include "nodes/pg_list.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/hsearch.h"
#include "utils/rel.h"
#include "utils/relcache.h"


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
	Relation pgExtension = NULL;
	SysScanDesc scanDescriptor;
	ScanKeyData scanKey[1];
	bool indexOK = true;
	HeapTuple extensionTuple = NULL;
	Form_pg_extension extensionForm = NULL;
	Oid extensionOwner = InvalidOid;

	pgExtension = heap_open(ExtensionRelationId, AccessShareLock);

	ScanKeyInit(&scanKey[0], Anum_pg_extension_extname, BTEqualStrategyNumber,
				F_NAMEEQ, CStringGetDatum(AUTO_FAILOVER_EXTENSION_NAME));

	scanDescriptor = systable_beginscan(pgExtension, ExtensionNameIndexId, indexOK,
										NULL, 1, scanKey);

	extensionTuple = systable_getnext(scanDescriptor);

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
