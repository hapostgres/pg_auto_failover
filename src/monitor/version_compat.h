/*-------------------------------------------------------------------------
 *
 * src/monitor/version_compat.h
 *	  Compatibility macros for writing code agnostic to PostgreSQL versions
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#ifndef VERSION_COMPAT_H
#define VERSION_COMPAT_H

#include "postgres.h"

/* we support Postgres versions 10, 11, 12, 13, 14, 15, 16, and 17. */
#if (PG_VERSION_NUM < 100000 || PG_VERSION_NUM >= 180000)
#error "Unknown or unsupported postgresql version"
#endif

#if (PG_VERSION_NUM < 110000)

#include "postmaster/bgworker.h"
#include "utils/memutils.h"

#define DEFAULT_XLOG_SEG_SIZE XLOG_SEG_SIZE

#define BackgroundWorkerInitializeConnection(dbname, username, flags) \
	BackgroundWorkerInitializeConnection(dbname, username)

#define BackgroundWorkerInitializeConnectionByOid(dboid, useroid, flags) \
	BackgroundWorkerInitializeConnectionByOid(dboid, useroid)

#include "nodes/pg_list.h"

typedef int (*list_qsort_comparator) (const void *a, const void *b);
extern List * list_qsort(const List *list, list_qsort_comparator cmp);

#endif

#if (PG_VERSION_NUM < 120000)

#define table_beginscan_catalog heap_beginscan_catalog
#define TableScanDesc HeapScanDesc

#endif

#if (PG_VERSION_NUM >= 120000)

#include "access/htup_details.h"
#include "catalog/pg_database.h"

static inline Oid
HeapTupleGetOid(HeapTuple tuple)
{
	Form_pg_database dbForm = (Form_pg_database) GETSTRUCT(tuple);
	return dbForm->oid;
}


#endif

#if (PG_VERSION_NUM >= 130000)

#include "common/hashfn.h"

#define heap_open(r, l) table_open(r, l)
#define heap_close(r, l) table_close(r, l)

#endif

#if (PG_VERSION_NUM < 130000)

/* Compatibility for ProcessUtility hook */
#define QueryCompletion char

#endif

/* Removed in Postgres 16 development */
#ifndef Abs

/*
 * Abs
 *		Return the absolute value of the argument.
 */
#define Abs(x) ((x) >= 0 ? (x) : -(x))

#endif

#endif   /* VERSION_COMPAT_H */
