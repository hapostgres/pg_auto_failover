/*-------------------------------------------------------------------------
 *
 * src/monitor/replication_state.c
 *
 * Implementation of functions related to (de)serialising replication
 * states.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "c.h"

#include "metadata.h"
#include "replication_state.h"
#include "version_compat.h"

#include "access/htup.h"
#include "access/htup_details.h"
#include "catalog/pg_enum.h"
#include "nodes/makefuncs.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "parser/parse_type.h"
#include "utils/syscache.h"


/* private function forward declarations */
static bool IsReplicationStateName(char *name, ReplicationState replicationState);


/*
 * ReplicationStateTypeOid returns the OID of the
 * pgautofailover.replication_state type.
 */
Oid
ReplicationStateTypeOid(void)
{
/* new String type in version 15devel  */
#if (PG_VERSION_NUM >= 150000)
	String *schemaName = makeString(AUTO_FAILOVER_SCHEMA_NAME);
	String *typeName = makeString(REPLICATION_STATE_TYPE_NAME);
#else
	Value *schemaName = makeString(AUTO_FAILOVER_SCHEMA_NAME);
	Value *typeName = makeString(REPLICATION_STATE_TYPE_NAME);
#endif

	List *enumTypeNameList = list_make2(schemaName, typeName);
	TypeName *enumTypeName = makeTypeNameFromNameList(enumTypeNameList);
	Oid enumTypeOid = typenameTypeId(NULL, enumTypeName);

	return enumTypeOid;
}


/*
 * EnumGetReplicationState returns the internal value of a replication state enum.
 */
ReplicationState
EnumGetReplicationState(Oid replicationStateOid)
{
	HeapTuple enumTuple = SearchSysCache1(ENUMOID, ObjectIdGetDatum(replicationStateOid));
	if (!HeapTupleIsValid(enumTuple))
	{
		ereport(ERROR, (errmsg("invalid input value for enum: %u",
							   replicationStateOid)));
	}

	Form_pg_enum enumForm = (Form_pg_enum) GETSTRUCT(enumTuple);
	char *enumName = NameStr(enumForm->enumlabel);
	ReplicationState replicationState = NameGetReplicationState(enumName);

	ReleaseSysCache(enumTuple);

	return replicationState;
}


/*
 * ReplicationStateGetEnum returns the enum value of an internal replication
 * state.
 */
Oid
ReplicationStateGetEnum(ReplicationState replicationState)
{
	const char *enumName = ReplicationStateGetName(replicationState);
	Oid enumTypeOid = ReplicationStateTypeOid();

	HeapTuple enumTuple = SearchSysCache2(ENUMTYPOIDNAME,
										  ObjectIdGetDatum(enumTypeOid),
										  CStringGetDatum(enumName));
	if (!HeapTupleIsValid(enumTuple))
	{
		ereport(ERROR, (errmsg("invalid value for enum: %d",
							   replicationState)));
	}

	Oid replicationStateOid = HeapTupleGetOid(enumTuple);

	ReleaseSysCache(enumTuple);

	return replicationStateOid;
}


/*
 * NameGetReplicationState returns the value of a replication state as an
 * integer.
 */
ReplicationState
NameGetReplicationState(char *replicationStateName)
{
	ReplicationState replicationState = REPLICATION_STATE_INITIAL;

	for (replicationState = REPLICATION_STATE_INITIAL;
		 !IsReplicationStateName(replicationStateName, replicationState) &&
		 replicationState < REPLICATION_STATE_UNKNOWN;
		 replicationState++)
	{ }

	return replicationState;
}


/*
 * IsReplicationStateName returns true if the given name is the name of the
 * replication state, and false otherwise.
 */
static bool
IsReplicationStateName(char *name, ReplicationState replicationState)
{
	const char *replicationStateName = ReplicationStateGetName(replicationState);

	if (strncmp(name, replicationStateName, NAMEDATALEN) == 0)
	{
		return true;
	}

	return false;
}


/*
 * ReplicationStateGetName returns the (enum) name of a replication state.
 */
const char *
ReplicationStateGetName(ReplicationState replicationState)
{
	switch (replicationState)
	{
		case REPLICATION_STATE_INITIAL:
		{
			return "init";
		}

		case REPLICATION_STATE_SINGLE:
		{
			return "single";
		}

		case REPLICATION_STATE_WAIT_PRIMARY:
		{
			return "wait_primary";
		}

		case REPLICATION_STATE_PRIMARY:
		{
			return "primary";
		}

		case REPLICATION_STATE_DRAINING:
		{
			return "draining";
		}

		case REPLICATION_STATE_DEMOTE_TIMEOUT:
		{
			return "demote_timeout";
		}

		case REPLICATION_STATE_DEMOTED:
		{
			return "demoted";
		}

		case REPLICATION_STATE_CATCHINGUP:
		{
			return "catchingup";
		}

		case REPLICATION_STATE_SECONDARY:
		{
			return "secondary";
		}

		case REPLICATION_STATE_PREPARE_PROMOTION:
		{
			return "prepare_promotion";
		}

		case REPLICATION_STATE_STOP_REPLICATION:
		{
			return "stop_replication";
		}

		case REPLICATION_STATE_WAIT_STANDBY:
		{
			return "wait_standby";
		}

		case REPLICATION_STATE_MAINTENANCE:
		{
			return "maintenance";
		}

		case REPLICATION_STATE_JOIN_PRIMARY:
		{
			return "join_primary";
		}

		case REPLICATION_STATE_APPLY_SETTINGS:
		{
			return "apply_settings";
		}

		case REPLICATION_STATE_PREPARE_MAINTENANCE:
		{
			return "prepare_maintenance";
		}

		case REPLICATION_STATE_WAIT_MAINTENANCE:
		{
			return "wait_maintenance";
		}

		case REPLICATION_STATE_REPORT_LSN:
		{
			return "report_lsn";
		}

		case REPLICATION_STATE_FAST_FORWARD:
		{
			return "fast_forward";
		}

		case REPLICATION_STATE_JOIN_SECONDARY:
		{
			return "join_secondary";
		}

		case REPLICATION_STATE_DROPPED:
		{
			return "dropped";
		}

		default:
		{
			ereport(ERROR,
					(errmsg("bug: unknown replication state (%d)",
							replicationState)));
		}
	}
}
