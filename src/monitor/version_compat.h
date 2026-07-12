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

/* we support Postgres versions 10, 11, 12, 13, 14, 15, 16, 17, 18, 19. */
#if (PG_VERSION_NUM < 100000 || PG_VERSION_NUM >= 200000)
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

/*
 * PG19 API changes:
 *
 * 1. LWLockNewTrancheId() now takes the tranche name as a parameter and
 *    internally registers it, so LWLockRegisterTranche() was removed.
 *
 * 2. ShmemInitHash() dropped the separate init/max size arguments; the single
 *    nelems argument now covers both.
 *
 * 3. PG_SIG_IGN/PG_SIG_DFL replace raw SIG_IGN/SIG_DFL for pqsignal() calls
 *    because pqsigfunc changed to use the SA_SIGINFO two-argument form.
 *
 * 4. utils/wait_event.h (and the generated wait_event_types.h) must be
 *    included explicitly; pgstat.h no longer pulls it in transitively.
 */
#if (PG_VERSION_NUM >= 190000)

#include "utils/wait_event.h"

/* LWLockNewTrancheId(name) merged LWLockRegisterTranche into itself */
#define LWLockNewTrancheIdCompat(id_out, name) \
	do { (id_out) = LWLockNewTrancheId(name); } while (0)

/* ShmemInitHash lost the separate init_size parameter */
#define ShmemInitHashCompat(name, nelems, infoP, flags) \
	ShmemInitHash(name, nelems, infoP, flags)

#else

#define LWLockNewTrancheIdCompat(id_out, name) \
	do { \
		(id_out) = LWLockNewTrancheId(); \
		LWLockRegisterTranche((id_out), (name)); \
	} while (0)

#define ShmemInitHashCompat(name, nelems, infoP, flags) \
	ShmemInitHash(name, nelems, nelems, infoP, flags)

#endif /* PG_VERSION_NUM >= 190000 */

/*
 * PG_SIG_IGN and PG_SIG_DFL were introduced in PG19 as properly-typed
 * wrappers around SIG_IGN/SIG_DFL for use with pqsignal().
 */
#if (PG_VERSION_NUM < 190000)
#ifndef PG_SIG_IGN
#define PG_SIG_IGN SIG_IGN
#endif
#ifndef PG_SIG_DFL
#define PG_SIG_DFL SIG_DFL
#endif
#endif

/*
 * pg_fallthrough was introduced in PG12.  Provide a no-op for older versions
 * and an attribute-based version for compilers that support it when building
 * against PG12+.
 */
#ifndef pg_fallthrough
#if __has_attribute(fallthrough)
#define pg_fallthrough __attribute__((fallthrough))
#else
#define pg_fallthrough
#endif
#endif

/*
 * PgStartTime was renamed to MyStartTimestamp in PG19.
 */
#if (PG_VERSION_NUM >= 190000)
#define PgStartTime MyStartTimestamp
#endif

/* utils/timestamp.h is no longer pulled in transitively by miscadmin.h
 * in PG19; include it here so monitors that use timestamp functions compile. */
#if (PG_VERSION_NUM >= 190000)
#include "utils/timestamp.h"
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
