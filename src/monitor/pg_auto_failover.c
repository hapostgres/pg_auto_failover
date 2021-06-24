/*-------------------------------------------------------------------------
 *
 * src/monitor/pg_auto_failover.c
 *
 * Implementation of the pg_auto_failover extension.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

/* these are internal headers */
#include "health_check.h"
#include "group_state_machine.h"
#include "metadata.h"
#include "version_compat.h"

/* these are always necessary for a bgworker */
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "commands/dbcommands.h"
#include "postmaster/postmaster.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "tcop/utility.h"


ProcessUtility_hook_type PreviousProcessUtility_hook = NULL;


void _PG_init(void);
static void StartMonitorNode(void);

#if (PG_VERSION_NUM < 140000)
static void pgautofailover_ProcessUtility(PlannedStmt *pstmt,
										  const char *queryString,
										  ProcessUtilityContext context,
										  ParamListInfo params,
										  struct QueryEnvironment *queryEnv,
										  DestReceiver *dest,
										  QueryCompletion *completionTag);
#else
static void pgautofailover_ProcessUtility(PlannedStmt *pstmt,
										  const char *queryString,
										  bool readOnlyTree,
										  ProcessUtilityContext context,
										  ParamListInfo params,
										  struct QueryEnvironment *queryEnv,
										  DestReceiver *dest,
										  QueryCompletion *completionTag);
#endif

PG_MODULE_MAGIC;


/*
 * Entrypoint of this module.
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
	{
		ereport(ERROR,
				(errmsg("pgautofailover can only be loaded via shared_preload_libraries"),
				 errhint("Add pgautofailover to shared_preload_libraries "
						 "configuration variable in postgresql.conf.")));
	}

	StartMonitorNode();
}


/*
 * StartMonitor register GUCs for monitor mode and starts the
 * health check worker.
 */
static void
StartMonitorNode(void)
{
	BackgroundWorker worker;

	DefineCustomBoolVariable("pgautofailover.enable_version_checks",
							 "Enable extension version compatiblity checks",
							 NULL, &EnableVersionChecks, true, PGC_SIGHUP,
							 GUC_NO_SHOW_ALL, NULL, NULL, NULL);

	DefineCustomBoolVariable("pgautofailover.enable_health_checks",
							 "Enable background health checks",
							 NULL, &HealthChecksEnabled, true, PGC_SIGHUP,
							 GUC_NO_SHOW_ALL, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.health_check_period",
							"Duration between each check (in milliseconds).",
							NULL, &HealthCheckPeriod, 5 * 1000, 1, INT_MAX, PGC_SIGHUP,
							GUC_UNIT_MS, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.health_check_timeout",
							"Connect timeout (in milliseconds).",
							NULL, &HealthCheckTimeout, 5 * 1000, 1, INT_MAX, PGC_SIGHUP,
							GUC_UNIT_MS, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.health_check_max_retries",
							"Maximum number of re-tries before marking a node as failed.",
							NULL, &HealthCheckMaxRetries, 2, 1, 100, PGC_SIGHUP,
							0, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.health_check_retry_delay",
							"Delay between consecutive retries.",
							NULL, &HealthCheckRetryDelay, 2 * 1000, 1, INT_MAX,
							PGC_SIGHUP, GUC_UNIT_MS, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.enable_sync_wal_log_threshold",
							"Don't enable synchronous replication until secondary xlog"
							" is within this many bytes of the primary's",
							NULL, &EnableSyncXlogThreshold, DEFAULT_XLOG_SEG_SIZE, 1,
							INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.promote_wal_log_threshold",
							"Don't promote secondary unless xlog is with this many bytes"
							" of the master",
							NULL, &PromoteXlogThreshold, DEFAULT_XLOG_SEG_SIZE, 1,
							INT_MAX, PGC_SIGHUP, 0, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.primary_demote_timeout",
							"Give the primary this long to drain before promoting the secondary",
							NULL, &DrainTimeoutMs, 30 * 1000, 1, INT_MAX,
							PGC_SIGHUP, GUC_UNIT_MS, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.node_considered_unhealthy_timeout",
							"Mark node unhealthy if last ping was over this long ago",
							NULL, &UnhealthyTimeoutMs, 20 * 1000, 1, INT_MAX,
							PGC_SIGHUP, GUC_UNIT_MS, NULL, NULL, NULL);

	DefineCustomIntVariable("pgautofailover.startup_grace_period",
							"Wait for at least this much time after startup before "
							"initiating a failover.",
							NULL, &StartupGracePeriodMs, 10 * 1000, 1, INT_MAX,
							PGC_SIGHUP, GUC_UNIT_MS, NULL, NULL, NULL);

	PreviousProcessUtility_hook = ProcessUtility_hook;
	ProcessUtility_hook = pgautofailover_ProcessUtility;

	InitializeHealthCheckWorker();

	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = 1;
	worker.bgw_main_arg = Int32GetDatum(0);
	worker.bgw_notify_pid = 0;
	strlcpy(worker.bgw_library_name, "pgautofailover", sizeof(worker.bgw_library_name));
	strlcpy(worker.bgw_name, "pg_auto_failover monitor", sizeof(worker.bgw_name));
	strlcpy(worker.bgw_function_name, "HealthCheckWorkerLauncherMain",
			sizeof(worker.bgw_function_name));

	RegisterBackgroundWorker(&worker);
}


/*
 * pgautofailover_ProcessUtility is a PostgreSQL utility hook that allows terminating
 * background workers attached to a database when a DROP DATABASE command is
 * executed. As long as the background worker is connected, the DROP DATABASE
 * command would otherwise fail to complete.
 */
#if (PG_VERSION_NUM < 140000)
void
pgautofailover_ProcessUtility(PlannedStmt *pstmt,
							  const char *queryString,
							  ProcessUtilityContext context,
							  ParamListInfo params,
							  struct QueryEnvironment *queryEnv,
							  DestReceiver *dest,
							  QueryCompletion *completionTag)
#else
void
pgautofailover_ProcessUtility(PlannedStmt * pstmt,
							  const char * queryString,
							  bool readOnlyTree,
							  ProcessUtilityContext context,
							  ParamListInfo params,
							  struct QueryEnvironment *queryEnv,
							  DestReceiver * dest,
							  QueryCompletion * completionTag)
#endif
{
	Node *parsetree = pstmt->utilityStmt;

	/*
	 * Make sure that on DROP DATABASE we terminate the background deamon
	 * associated with it.
	 */
	if (IsA(parsetree, DropdbStmt))
	{
		DropdbStmt *dropDbStatement = (DropdbStmt *) parsetree;
		char *dbname = dropDbStatement->dbname;
		Oid databaseOid = get_database_oid(dbname, true);

		if (databaseOid != InvalidOid)
		{
			StopHealthCheckWorker(databaseOid);
		}
	}

	if (PreviousProcessUtility_hook)
	{
#if (PG_VERSION_NUM < 140000)
		PreviousProcessUtility_hook(pstmt, queryString, context,
									params, queryEnv, dest, completionTag);
#else
		PreviousProcessUtility_hook(pstmt, queryString, readOnlyTree, context,
									params, queryEnv, dest, completionTag);
#endif
	}
	else
	{
#if (PG_VERSION_NUM < 140000)
		standard_ProcessUtility(pstmt, queryString, context,
								params, queryEnv, dest, completionTag);
#else
		standard_ProcessUtility(pstmt, queryString, readOnlyTree, context,
								params, queryEnv, dest, completionTag);
#endif
	}
}
