/*-------------------------------------------------------------------------
 *
 * src/monitor/health_check_worker.c
 *
 * Implementation of the health check worker.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

/* these are internal headers */
#include "health_check.h"
#include "metadata.h"
#include "version_compat.h"

/* these are always necessary for a bgworker */
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/pg_database.h"
#include "commands/extension.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "storage/lmgr.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/shmem.h"

/* these headers are used by this particular worker's code */
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "libpq-fe.h"
#include "libpq-int.h"
#include "libpq/pqsignal.h"
#include "poll.h"
#include "sys/time.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "tcop/utility.h"

#define CONN_INFO_TEMPLATE \
	"host=%s port=%u user=pgautofailover_monitor dbname=postgres " \
	"connect_timeout=%u"
#define MAX_CONN_INFO_SIZE 1024

#define CANNOT_CONNECT_NOW "57P03"


typedef enum
{
	HEALTH_CHECK_INITIAL = 0,
	HEALTH_CHECK_CONNECTING = 1,
	HEALTH_CHECK_OK = 2,
	HEALTH_CHECK_RETRY = 3,
	HEALTH_CHECK_DEAD = 4
} HealthCheckState;

typedef struct HealthCheck
{
	NodeHealth *node;
	HealthCheckState state;
	PGconn *connection;
	bool readyToPoll;
	PostgresPollingStatusType pollingStatus;
	int numTries;
	struct timeval nextEventTime;
} HealthCheck;


/*
 * Shared memory data for all maintenance workers.
 */
typedef struct HealthCheckHelperControlData
{
	/*
	 * Lock protecting the shared memory state.  This is to be taken when
	 * looking up (shared mode) or inserting (exclusive mode) per-database
	 * data in HealthCheckWorkerDBHash.
	 */
	int				trancheId;
	char		   *lockTrancheName;
	LWLock			lock;
} HealthCheckHelperControlData;

/*
 * Per database worker state.
 */
typedef struct HealthCheckHelperDatabase
{
	/* hash key: database to run on */
	Oid			dboid;
	pid_t		workerPid;
	bool isActive;
} HealthCheckHelperDatabase;

typedef struct DatabaseListEntry
{
	Oid		dboid;
	char   *dbname;
} DatabaseListEntry;


/*
 * Hash-table of workers, one entry for each database with pg_auto_failover
 * activated, and a lock to protect access to it.
 */
static HTAB *HealthCheckWorkerDBHash;
static HealthCheckHelperControlData *HealthCheckHelperControl = NULL;
static shmem_startup_hook_type prev_shmem_startup_hook = NULL;



/* private function declarations */
static void pg_auto_failover_monitor_sigterm(SIGNAL_ARGS);
static void pg_auto_failover_monitor_sighup(SIGNAL_ARGS);
static BackgroundWorkerHandle * StartHealthCheckWorker(DatabaseListEntry *db);
static List *BuildDatabaseList(void);
static bool pgAutoFailoverExtensionExists(void);
static List * CreateHealthChecks(List *nodeHealthList);
static HealthCheck * CreateHealthCheck(NodeHealth *nodeHealth);
static void DoHealthChecks(List *healthCheckList);
static void ManageHealthCheck(HealthCheck *healthCheck, struct timeval currentTime);
static int WaitForEvent(List *healthCheckList);
static int CompareTimes(struct timeval *leftTime, struct timeval *rightTime);
static int SubtractTimes(struct timeval base, struct timeval subtract);
static struct timeval AddTimeMillis(struct timeval base, uint32 additionalMs);
static void LatchWait(long timeoutMs);
static size_t HealthCheckWorkerShmemSize(void);
static void HealthCheckWorkerShmemInit(void);



/* flags set by signal handlers */
static volatile sig_atomic_t got_sighup = false;
static volatile sig_atomic_t got_sigterm = false;

/* GUC variables */
int HealthCheckPeriod = 20 * 1000;
int HealthCheckTimeout = 5 * 1000;
int HealthCheckMaxRetries = 2;
int HealthCheckRetryDelay = 2 * 1000;


/*
 * Signal handler for SIGTERM
 *		Set a flag to let the main loop to terminate, and set our latch to wake
 *		it up.
 */
static void
pg_auto_failover_monitor_sigterm(SIGNAL_ARGS)
{
	int save_errno = errno;

	got_sigterm = true;
	SetLatch(MyLatch);

	errno = save_errno;
}


/*
 * Signal handler for SIGHUP
 *		Set a flag to tell the main loop to reread the config file, and set
 *		our latch to wake it up.
 */
static void
pg_auto_failover_monitor_sighup(SIGNAL_ARGS)
{
	int save_errno = errno;

	got_sighup = true;
	SetLatch(MyLatch);

	errno = save_errno;
}


/*
 * InitializeHealthCheckWorker, called at server start, is responsible for
 * requesting shared memory and related infrastructure required by worker
 * daemons.
 */
void
InitializeHealthCheckWorker(void)
{
	if (!IsUnderPostmaster)
	{
		RequestAddinShmemSpace(HealthCheckWorkerShmemSize());
	}

	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = HealthCheckWorkerShmemInit;
}


/*
 * HealthCheckWorkerLauncherMain is the main entry point for the
 * pg_auto_failover Health Check workers.
 *
 * We start a background worker for each database because a single background
 * worker may only connect to a single database for its whole lifetime. Each
 * worker checks if the "pgautofailover" extension is installed locally, and
 * then does the health checks.
 */
void
HealthCheckWorkerLauncherMain(Datum arg)
{
	MemoryContext launcherContext = NULL;

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGHUP, pg_auto_failover_monitor_sighup);
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, pg_auto_failover_monitor_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/*
	 * Initialize a connection to shared catalogs only.
	 */
	BackgroundWorkerInitializeConnection(NULL, NULL, 0);

	/* Make background worker recognisable in pg_stat_activity */
	pgstat_report_appname("pg_auto_failover monitor launcher");

	launcherContext = AllocSetContextCreate(CurrentMemoryContext,
											"Health Check Launcher Context",
											ALLOCSET_DEFAULT_MINSIZE,
											ALLOCSET_DEFAULT_INITSIZE,
											ALLOCSET_DEFAULT_MAXSIZE);

	MemoryContextSwitchTo(launcherContext);

	while (!got_sigterm)
	{
		List	   *databaseList;
		ListCell   *databaseListCell;

		databaseList = BuildDatabaseList();

		foreach(databaseListCell, databaseList)
		{
			int pid;
			BackgroundWorkerHandle *handle;
			HealthCheckHelperDatabase *dbData;
			DatabaseListEntry *entry =
				(DatabaseListEntry *) lfirst(databaseListCell);
			bool isFound = false;

			LWLockAcquire(&HealthCheckHelperControl->lock, LW_EXCLUSIVE);

			dbData = hash_search(HealthCheckWorkerDBHash,
								 (void *) &entry->dboid, HASH_ENTER, &isFound);
			if (isFound && dbData->isActive)
			{
				/* This database has already been processed */
				LWLockRelease(&HealthCheckHelperControl->lock);
				continue;
			}

			/* start a worker for the entry database, in the background */
			handle = StartHealthCheckWorker(entry);
			if (handle)
			{
				/*
				 * Once started, the Health Check process will update its
				 * pid.
				 */
				dbData->workerPid = 0;
				dbData->isActive = true;
			}

			LWLockRelease(&HealthCheckHelperControl->lock);

			if (handle)
			{
				/*
				 * We need to release the lock for the worker to be able to
				 * complete its startup procedure: the per-database worker
				 * takes the control lock in SHARED mode to edit its own PID in
				 * its own entry in HealthCheckWorkerDBHash.
				 */
				WaitForBackgroundWorkerStartup(handle, &pid);
			}
		}

		MemoryContextReset(launcherContext);

		LatchWait(HealthCheckTimeout);

		if (got_sighup)
		{
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
		}
	}
}


/*
 * StartHealthCheckWorker registers a background worker in given target
 * database, and returns the background worker handle so that the caller can
 * wait until it is started.
 *
 * This is necessary because of locking management, we want to release the main
 * lock from the caller before waiting for the worker's start.
 */
static BackgroundWorkerHandle *
StartHealthCheckWorker(DatabaseListEntry *db)
{
	BackgroundWorker worker;
	BackgroundWorkerHandle *handle;

	memset(&worker, 0, sizeof(worker));

	worker.bgw_flags =
		BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = BGW_NEVER_RESTART;
	worker.bgw_main_arg = ObjectIdGetDatum(db->dboid);
	worker.bgw_notify_pid = MyProcPid;
	snprintf(worker.bgw_library_name, BGW_MAXLEN, "pgautofailover");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "HealthCheckWorkerMain");
	snprintf(worker.bgw_name, BGW_MAXLEN,
			 "pg_auto_failover monitor worker");

	if (!RegisterDynamicBackgroundWorker(&worker, &handle))
	{
		ereport(LOG,
				(errmsg("failed to start worker for pg_auto_failover health checks in \"%s\"",
						db->dbname)));
		return NULL;
	}

	return handle;
}


/*
 * BuildDatabaseList
 *		Compile a list of all currently available databases in the cluster
 */
static List *
BuildDatabaseList(void)
{
	List	   *databaseList = NIL;
	Relation	pgDatabaseRelation;
	TableScanDesc scan;
	HeapTuple	dbTuple;
	MemoryContext originalContext = CurrentMemoryContext;

	StartTransactionCommand();

	pgDatabaseRelation = heap_open(DatabaseRelationId, AccessShareLock);

	scan = table_beginscan_catalog(pgDatabaseRelation, 0, NULL);

	while (HeapTupleIsValid(dbTuple = heap_getnext(scan, ForwardScanDirection)))
	{
		MemoryContext oldContext;
		Form_pg_database dbForm = (Form_pg_database) GETSTRUCT(dbTuple);
		DatabaseListEntry *entry;

		/* only consider non-template databases that we can connect to */
		if (!dbForm->datistemplate && dbForm->datallowconn)
		{
			oldContext = MemoryContextSwitchTo(originalContext);

			entry = (DatabaseListEntry *) palloc(sizeof(DatabaseListEntry));

			entry->dboid = HeapTupleGetOid(dbTuple);
			entry->dbname = pstrdup(NameStr(dbForm->datname));

			databaseList = lappend(databaseList, entry);

			MemoryContextSwitchTo(oldContext);
		}
	}

	heap_endscan(scan);
	heap_close(pgDatabaseRelation, AccessShareLock);

	CommitTransactionCommand();
	MemoryContextSwitchTo(originalContext);

	return databaseList;
}


/*
 * HealthCheckWorkerMain is the main entry-point for the background worker that
 * performs health checks.
 */
void
HealthCheckWorkerMain(Datum arg)
{
	Oid dboid = DatumGetObjectId(arg);
	bool foundPgAutoFailoverExtension = false;
	MemoryContext healthCheckContext = NULL;
	HealthCheckHelperDatabase *myDbData;

	/*
	 * Look up this worker's configuration.
	 */
	LWLockAcquire(&HealthCheckHelperControl->lock, LW_SHARED);

	myDbData = (HealthCheckHelperDatabase *)
		hash_search(HealthCheckWorkerDBHash,
					(void *) &dboid, HASH_FIND, NULL);

	if (!myDbData)
	{
		/*
		 * When the database crashes, background workers are restarted, but
		 * the state in shared memory is lost. In that case, we exit and
		 * wait for HealthCheckWorkerLauncherMain to restart it.
		 */
		proc_exit(0);
	}

	/* from this point, DROP DATABASE will attempt to kill the worker */
	myDbData->workerPid = MyProcPid;

	/* Establish signal handlers before unblocking signals. */
	pqsignal(SIGHUP, pg_auto_failover_monitor_sighup);
	pqsignal(SIGINT, SIG_IGN);
	pqsignal(SIGTERM, pg_auto_failover_monitor_sigterm);

	/* We're now ready to receive signals */
	BackgroundWorkerUnblockSignals();

	/* we're also done editing our own hash table entry */
	LWLockRelease(&HealthCheckHelperControl->lock);

	/* Connect to our database */
	BackgroundWorkerInitializeConnectionByOid(dboid, InvalidOid, 0);

	/* Make background worker recognisable in pg_stat_activity */
	pgstat_report_appname("pg_auto_failover health check worker");

	/*
	 * Only process given database when the extension has been loaded.
	 * Otherwise, happily quit.
	 */
	healthCheckContext = AllocSetContextCreate(CurrentMemoryContext,
											   "Health check context",
											   ALLOCSET_DEFAULT_MINSIZE,
											   ALLOCSET_DEFAULT_INITSIZE,
											   ALLOCSET_DEFAULT_MAXSIZE);

	MemoryContextSwitchTo(healthCheckContext);

	/*
	 * Main loop: do this until the SIGTERM handler tells us to terminate
	 */
	while (!got_sigterm)
	{
		struct timeval currentTime = { 0, 0 };
		struct timeval roundEndTime = { 0, 0 };
		int timeout = 0;
		List *nodeHealthList = NIL;
		List *healthCheckList = NIL;

		gettimeofday(&currentTime, NULL);
		roundEndTime = AddTimeMillis(currentTime, HealthCheckPeriod);


		if (!foundPgAutoFailoverExtension)
		{
			if (pgAutoFailoverExtensionExists())
			{
				foundPgAutoFailoverExtension = true;
				elog(LOG,
					 "pg_auto_failover extension found in database %d, "
					 "starting Health Checks.", dboid);
			}
		}

		if (foundPgAutoFailoverExtension)
		{
			nodeHealthList = LoadNodeHealthList();
			healthCheckList = CreateHealthChecks(nodeHealthList);

			DoHealthChecks(healthCheckList);

			MemoryContextReset(healthCheckContext);
		}

		gettimeofday(&currentTime, NULL);
		timeout = SubtractTimes(roundEndTime, currentTime);

		if (timeout >= 0)
		{
			LatchWait(timeout);
		}

		if (got_sighup)
		{
			got_sighup = false;
			ProcessConfigFile(PGC_SIGHUP);
		}
	}

	elog(LOG,
		 "pg_auto_failover monitor exiting for database %d", dboid);

	proc_exit(0);
}


/*
 * pgAutoFailoverExtensionExists returns true when we can find the
 * "pgautofailover" extension in the pg_extension catalogs. Caller must have
 * already connected to a database before calling this function.
 */
static bool
pgAutoFailoverExtensionExists(void)
{
	Oid extensionOid;
	MemoryContext originalContext = CurrentMemoryContext;

	StartTransactionCommand();

	extensionOid = get_extension_oid(AUTO_FAILOVER_EXTENSION_NAME, true);

	CommitTransactionCommand();

	/* CommitTransactionCommand resets the memory context to TopMemoryContext */
	MemoryContextSwitchTo(originalContext);

	return (extensionOid != InvalidOid);
}


/*
 * CreateHealthChecks creates a list of health checks from a list of node health
 * descriptions.
 */
static List *
CreateHealthChecks(List *nodeHealthList)
{
	List *healthCheckList = NIL;
	ListCell *nodeHealthCell = NULL;

	foreach(nodeHealthCell, nodeHealthList)
	{
		NodeHealth *nodeHealth = (NodeHealth *) lfirst(nodeHealthCell);
		HealthCheck *healthCheck = CreateHealthCheck(nodeHealth);
		healthCheckList = lappend(healthCheckList, healthCheck);
	}

	return healthCheckList;
}


/*
 * CreateHealthCheck creates a health check from a health check description.
 */
static HealthCheck *
CreateHealthCheck(NodeHealth *nodeHealth)
{
	HealthCheck *healthCheck = NULL;
	struct timeval invalidTime = { 0, 0 };

	healthCheck = palloc0(sizeof(HealthCheck));
	healthCheck->node = nodeHealth;
	healthCheck->state = HEALTH_CHECK_INITIAL;
	healthCheck->connection = NULL;
	healthCheck->numTries = 0;
	healthCheck->nextEventTime = invalidTime;

	return healthCheck;
}


/*
 * DoHealthChecks performs the given health checks.
 */
static void
DoHealthChecks(List *healthCheckList)
{
	while (!got_sigterm)
	{
		int pendingCheckCount = 0;
		struct timeval currentTime = { 0, 0 };
		ListCell *healthCheckCell = NULL;

		gettimeofday(&currentTime, NULL);

		foreach(healthCheckCell, healthCheckList)
		{
			HealthCheck *healthCheck = (HealthCheck *) lfirst(healthCheckCell);

			ManageHealthCheck(healthCheck, currentTime);

			if (healthCheck->state != HEALTH_CHECK_OK &&
				healthCheck->state != HEALTH_CHECK_DEAD)
			{
				pendingCheckCount++;
			}
		}
		if (pendingCheckCount == 0)
		{
			break;
		}

		WaitForEvent(healthCheckList);
	}
}


/*
 * WaitForEvent sleeps until a time-based or I/O event occurs in any of the health
 * checks.
 */
static int
WaitForEvent(List *healthCheckList)
{
	ListCell *healthCheckCell = NULL;
	int healthCheckCount = list_length(healthCheckList);
	struct pollfd *pollFDs = NULL;
	struct timeval currentTime = { 0, 0 };
	struct timeval nextEventTime = { 0, 0 };
	int healthCheckIndex = 0;
	int pollResult = 0;
	int pollTimeout = 0;

	pollFDs = (struct pollfd *) palloc0(healthCheckCount * sizeof(struct pollfd));

	gettimeofday(&currentTime, NULL);

	foreach(healthCheckCell, healthCheckList)
	{
		HealthCheck *healthCheck = (HealthCheck *) lfirst(healthCheckCell);
		struct pollfd *pollFileDescriptor = &pollFDs[healthCheckIndex];

		pollFileDescriptor->fd = -1;
		pollFileDescriptor->events = 0;
		pollFileDescriptor->revents = 0;

		if (healthCheck->state == HEALTH_CHECK_CONNECTING ||
			healthCheck->state == HEALTH_CHECK_RETRY)
		{
			bool hasTimeout = healthCheck->nextEventTime.tv_sec != 0;

			if (hasTimeout &&
				(nextEventTime.tv_sec == 0 ||
				 CompareTimes(&healthCheck->nextEventTime, &nextEventTime) < 0))
			{
				nextEventTime = healthCheck->nextEventTime;
			}
		}

		if (healthCheck->state == HEALTH_CHECK_CONNECTING)
		{
			PGconn *connection = healthCheck->connection;
			int pollEventMask = 0;

			if (healthCheck->pollingStatus == PGRES_POLLING_READING)
			{
				pollEventMask = POLLERR | POLLIN;
			}
			else if (healthCheck->pollingStatus == PGRES_POLLING_WRITING)
			{
				pollEventMask = POLLERR | POLLOUT;
			}

			pollFileDescriptor->fd = PQsocket(connection);
			pollFileDescriptor->events = pollEventMask;
		}

		healthCheckIndex++;
	}

	pollTimeout = SubtractTimes(nextEventTime, currentTime);
	if (pollTimeout < 0)
	{
		pollTimeout = 0;
	}
	else if (pollTimeout > HealthCheckRetryDelay)
	{
		pollTimeout = HealthCheckRetryDelay;
	}

	pollResult = poll(pollFDs, healthCheckCount, pollTimeout);

	if (pollResult < 0)
	{
		return STATUS_ERROR;
	}

	healthCheckIndex = 0;

	foreach(healthCheckCell, healthCheckList)
	{
		HealthCheck *healthCheck = (HealthCheck *) lfirst(healthCheckCell);
		struct pollfd *pollFileDescriptor = &pollFDs[healthCheckIndex];

		healthCheck->readyToPoll = pollFileDescriptor->revents &
								   pollFileDescriptor->events;

		healthCheckIndex++;
	}

	return 0;
}


/*
 * LatchWait sleeps on the process latch until a timeout occurs.
 */
static void
LatchWait(long timeoutMs)
{
	int waitResult = 0;

	/*
	 * Background workers mustn't call usleep() or any direct equivalent:
	 * instead, they may wait on their process latch, which sleeps as
	 * necessary, but is awakened if postmaster dies.  That way the
	 * background process goes away immediately in an emergency.
	 */
#if (PG_VERSION_NUM >= 100000)
	waitResult = WaitLatch(MyLatch,
						   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
						   timeoutMs, WAIT_EVENT_CLIENT_READ);
#else
	waitResult = WaitLatch(MyLatch,
						   WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
						   timeoutMs);
#endif

	ResetLatch(MyLatch);

	/* emergency bailout if postmaster has died */
	if (waitResult & WL_POSTMASTER_DEATH)
	{
		elog(LOG, "pg_auto_failover monitor exiting");

		proc_exit(1);
	}
}


/*
 * ManageHealthCheck proceeds the health check state machine.
 */
static void
ManageHealthCheck(HealthCheck *healthCheck, struct timeval currentTime)
{
	HealthCheckState checkState = healthCheck->state;
	NodeHealth *nodeHealth = healthCheck->node;

	switch (checkState)
	{
		case HEALTH_CHECK_RETRY:
		{
			if (healthCheck->numTries >= HealthCheckMaxRetries + 1)
			{
				if (nodeHealth->healthState != NODE_HEALTH_BAD)
				{
					elog(LOG, "pg_auto_failover monitor marking node %s:%d as unhealthy",
						 nodeHealth->nodeName,
						 nodeHealth->nodePort);
				}

				SetNodeHealthState(healthCheck->node->nodeName,
								   healthCheck->node->nodePort,
								   NODE_HEALTH_BAD);

				healthCheck->state = HEALTH_CHECK_DEAD;
				break;
			}

			if (CompareTimes(&healthCheck->nextEventTime, &currentTime) > 0)
			{
				/* Retry time lies in the future */
				break;
			}

			/* Fall through to re-connect */
		}

		/* fallthrough */
		case HEALTH_CHECK_INITIAL:
		{
			PGconn *connection = NULL;
			ConnStatusType connStatus = CONNECTION_BAD;
			char connInfoString[MAX_CONN_INFO_SIZE];

			snprintf(connInfoString, MAX_CONN_INFO_SIZE, CONN_INFO_TEMPLATE,
					 nodeHealth->nodeName, nodeHealth->nodePort, HealthCheckTimeout);

			connection = PQconnectStart(connInfoString);
			PQsetnonblocking(connection, true);

			connStatus = PQstatus(connection);
			if (connStatus == CONNECTION_BAD)
			{
				struct timeval nextTryTime = { 0, 0 };

				PQfinish(connection);

				nextTryTime = AddTimeMillis(currentTime, HealthCheckRetryDelay);

				healthCheck->nextEventTime = nextTryTime;
				healthCheck->connection = NULL;
				healthCheck->pollingStatus = PGRES_POLLING_FAILED;
				healthCheck->state = HEALTH_CHECK_RETRY;
			}
			else
			{
				struct timeval timeoutTime = { 0, 0 };

				timeoutTime = AddTimeMillis(currentTime, HealthCheckTimeout);

				healthCheck->nextEventTime = timeoutTime;
				healthCheck->connection = connection;
				healthCheck->pollingStatus = PGRES_POLLING_WRITING;
				healthCheck->state = HEALTH_CHECK_CONNECTING;
			}

			healthCheck->numTries++;

			break;
		}

		case HEALTH_CHECK_CONNECTING:
		{
			PGconn *connection = healthCheck->connection;
			PostgresPollingStatusType pollingStatus = PGRES_POLLING_FAILED;
			char *sqlstate = NULL;
			bool receivedSqlstate = false;
			bool cannotConnectNowSqlstate = false;

			if (CompareTimes(&healthCheck->nextEventTime, &currentTime) < 0)
			{
				struct timeval nextTryTime = { 0, 0 };

				PQfinish(connection);

				nextTryTime = AddTimeMillis(currentTime, HealthCheckRetryDelay);

				healthCheck->nextEventTime = nextTryTime;
				healthCheck->connection = NULL;
				healthCheck->pollingStatus = pollingStatus;
				healthCheck->state = HEALTH_CHECK_RETRY;
				break;
			}

			if (!healthCheck->readyToPoll)
			{
				break;
			}

			/* This logic is taken from libpq's internal_ping (fe-connect.c) */
			pollingStatus = PQconnectPoll(connection);
			sqlstate = connection->last_sqlstate;
			receivedSqlstate = (sqlstate != NULL && strlen(sqlstate) == 5);
			cannotConnectNowSqlstate = (receivedSqlstate &&
										strcmp(sqlstate, CANNOT_CONNECT_NOW) == 0);

			if (pollingStatus == PGRES_POLLING_OK ||

			    /* an auth request means pg is running */
				connection->auth_req_received ||

			    /* any error but CANNOT_CONNECT means the db is accepting connections */
				(receivedSqlstate && !cannotConnectNowSqlstate))
			{
				PQfinish(connection);

				if (nodeHealth->healthState != NODE_HEALTH_GOOD)
				{
					elog(LOG, "pg_auto_failover monitor marking node %s:%d as healthy",
						 nodeHealth->nodeName,
						 nodeHealth->nodePort);
				}

				SetNodeHealthState(healthCheck->node->nodeName,
								   healthCheck->node->nodePort,
								   NODE_HEALTH_GOOD);

				healthCheck->connection = NULL;
				healthCheck->numTries = 0;
				healthCheck->state = HEALTH_CHECK_OK;
			}
			else if (pollingStatus == PGRES_POLLING_FAILED)
			{
				struct timeval nextTryTime = { 0, 0 };

				PQfinish(connection);

				nextTryTime = AddTimeMillis(currentTime, HealthCheckRetryDelay);

				healthCheck->nextEventTime = nextTryTime;
				healthCheck->connection = NULL;
				healthCheck->state = HEALTH_CHECK_RETRY;
			}
			else
			{
				/* Health check is still connecting */
			}

			healthCheck->pollingStatus = pollingStatus;

			break;
		}

		case HEALTH_CHECK_DEAD:
		case HEALTH_CHECK_OK:
		default:
		{
			/* Health check is done */
		}
	}
}


/*
 * CompareTime compares two timeval structs.
 *
 * If leftTime < rightTime, return -1
 * If leftTime > rightTime, return 1
 * else, return 0
 */
static int
CompareTimes(struct timeval *leftTime, struct timeval *rightTime)
{
	int compareResult = 0;

	if (leftTime->tv_sec < rightTime->tv_sec)
	{
		compareResult = -1;
	}
	else if (leftTime->tv_sec > rightTime->tv_sec)
	{
		compareResult = 1;
	}
	else if (leftTime->tv_usec < rightTime->tv_usec)
	{
		compareResult = -1;
	}
	else if (leftTime->tv_usec > rightTime->tv_usec)
	{
		compareResult = 1;
	}
	else
	{
		compareResult = 0;
	}

	return compareResult;
}


/*
 * SubtractTimes subtract the ‘struct timeval’ values y from x,
 * returning the result.
 *
 * From:
 * http://www.gnu.org/software/libc/manual/html_node/Elapsed-Time.html
 */
static int
SubtractTimes(struct timeval x, struct timeval y)
{
	int differenceMs = 0;

	/* Perform the carry for the later subtraction by updating y. */
	if (x.tv_usec < y.tv_usec)
	{
		int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
		y.tv_usec -= 1000000 * nsec;
		y.tv_sec += nsec;
	}

	if (x.tv_usec - y.tv_usec > 1000000)
	{
		int nsec = (x.tv_usec - y.tv_usec) / 1000000;
		y.tv_usec += 1000000 * nsec;
		y.tv_sec -= nsec;
	}

	differenceMs += 1000 * (x.tv_sec - y.tv_sec);
	differenceMs += (x.tv_usec - y.tv_usec) / 1000;

	return differenceMs;
}


/*
 * AddTimeMillis adds additionalMs milliseconds to a timeval.
 */
static struct timeval
AddTimeMillis(struct timeval base, uint32 additionalMs)
{
	struct timeval result = { 0, 0 };

	result.tv_sec = base.tv_sec + additionalMs / 1000;
	result.tv_usec = base.tv_usec + (additionalMs % 1000) * 1000;

	return result;
}


/*
 * HealthCheckWorkerShmemSize computes how much shared memory is required.
 */
static size_t
HealthCheckWorkerShmemSize(void)
{
	Size size = 0;
	Size hashSize = 0;

	size = add_size(size, sizeof(HealthCheckHelperDatabase));

	/*
	 * We request enough shared memory to have one hash-table entry for each
	 * worker process. We couldn't start more anyway, so there's little point
	 * in allocating more.
	 */
	hashSize = hash_estimate_size(max_worker_processes,
								  sizeof(HealthCheckHelperDatabase));
	size = add_size(size, hashSize);

	return size;
}


/*
 * HealthCheckWorkerShmemInit initializes the requested shared memory for the
 * maintenance daemon.
 */
static void
HealthCheckWorkerShmemInit(void)
{
	bool alreadyInitialized = false;
	HASHCTL hashInfo;
	int hashFlags = 0;

	LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

	HealthCheckHelperControl =
		(HealthCheckHelperControlData *)
		ShmemInitStruct("pg_auto_failover Health Check Helper Daemon",
						HealthCheckWorkerShmemSize(),
						&alreadyInitialized);

	/*
	 * Might already be initialized on EXEC_BACKEND type platforms that call
	 * shared library initialization functions in every backend.
	 */
	if (!alreadyInitialized)
	{
		HealthCheckHelperControl->trancheId = LWLockNewTrancheId();
		HealthCheckHelperControl->lockTrancheName =
			"pg_auto_failover Health Check Daemon";
		LWLockRegisterTranche(HealthCheckHelperControl->trancheId,
							  HealthCheckHelperControl->lockTrancheName);

		LWLockInitialize(&HealthCheckHelperControl->lock,
						 HealthCheckHelperControl->trancheId);
	}

	memset(&hashInfo, 0, sizeof(hashInfo));
	hashInfo.keysize = sizeof(Oid);
	hashInfo.entrysize = sizeof(HealthCheckHelperDatabase);
	hashInfo.hash = tag_hash;
	hashFlags = (HASH_ELEM | HASH_FUNCTION);

	HealthCheckWorkerDBHash = ShmemInitHash("pg_auto_failover Database Hash",
											max_worker_processes,
											max_worker_processes,
											&hashInfo, hashFlags);

	LWLockRelease(AddinShmemInitLock);

	if (prev_shmem_startup_hook != NULL)
	{
		prev_shmem_startup_hook();
	}
}


/*
 * StopHealthCheckWorker stops the maintenance daemon for the given database
 * and removes it from the Health Check Launcher control hash.
 */
void
StopHealthCheckWorker(Oid databaseId)
{
	bool found = false;
	HealthCheckHelperDatabase *dbData = NULL;
	pid_t workerPid = 0;

	LWLockAcquire(&HealthCheckHelperControl->lock, LW_EXCLUSIVE);

	dbData = (HealthCheckHelperDatabase *)
		hash_search(HealthCheckWorkerDBHash,
					&databaseId, HASH_REMOVE, &found);

	if (found)
	{
		workerPid = dbData->workerPid;
	}

	LWLockRelease(&HealthCheckHelperControl->lock);

	if (workerPid > 0)
	{
		kill(workerPid, SIGTERM);
	}
}
