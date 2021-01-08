/*
 * src/bin/pg_autoctl/demoapp.c
 *	 Demo application for pg_auto_failover
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 *
 */
#include <inttypes.h>
#include <limits.h>
#include <sys/select.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "cli_do_demoapp.h"
#include "defaults.h"
#include "demoapp.h"
#include "env_utils.h"
#include "log.h"
#include "monitor.h"
#include "pgsql.h"
#include "signals.h"
#include "string_utils.h"

#include "runprogram.h"

static void demoapp_start_client(const char *pguri,
								 int clientId,
								 DemoAppOptions *demoAppOptions);

static bool demoapp_wait_for_clients(pid_t clientsPidArray[],
									 int startedClientsCount);

static void demoapp_terminate_clients(pid_t clientsPidArray[],
									  int startedClientsCount);

static void demoapp_process_perform_switchover(DemoAppOptions *demoAppOptions);

/*
 * demoapp_grab_formation_uri connects to the monitor and grabs the formation
 * URI to use in the demo application.
 */
bool
demoapp_grab_formation_uri(DemoAppOptions *options, char *pguri, size_t size,
						   bool *mayRetry)
{
	Monitor monitor = { 0 };

	SSLOptions ssl = { 0 };
	SSLMode sslMode = SSL_MODE_PREFER;
	char *sslModeStr = pgsetup_sslmode_to_string(sslMode);

	ssl.sslMode = sslMode;
	strlcpy(ssl.sslModeStr, sslModeStr, SSL_MODE_STRLEN);

	*mayRetry = false;

	if (!monitor_init(&monitor, options->monitor_pguri))
	{
		/* errors have already been logged */
		return false;
	}

	/* allow lots of retries to connect to the monitor at startup */
	pgsql_set_monitor_interactive_retry_policy(&(monitor.pgsql.retryPolicy));

	if (!monitor_formation_uri(&monitor, options->formation, &ssl, pguri, size))
	{
		int groupsCount = 0;

		if (!monitor_count_groups(&monitor, options->formation, &groupsCount))
		{
			/* errors have already been logged */
			return false;
		}

		if (groupsCount >= 0)
		{
			*mayRetry = true;
		}

		log_level(*mayRetry ? LOG_ERROR : LOG_FATAL,
				  "Failed to grab the Postgres URI "
				  "to connect to formation \"%s\", see above for details",
				  options->formation);

		pgsql_finish(&(monitor.pgsql));

		return false;
	}

	pgsql_finish(&(monitor.pgsql));

	return true;
}


/*
 * demoapp_set_retry_policy sets a retry policy that is suitable for a demo
 * client application.
 */
void
demoapp_set_retry_policy(PGSQL *pgsql)
{
	int cap = 200;         /* sleep up to 200s between attempts */
	int sleepTime = 500;   /* first retry happens after 500ms */

	(void) pgsql_set_retry_policy(&(pgsql->retryPolicy),
								  60, /* maxT */
								  -1, /* unbounded maxR */
								  cap,
								  sleepTime);
}


/*
 * demoapp_prepare_schema prepares the demo application schema on the target
 * database instance.
 */
bool
demoapp_prepare_schema(const char *pguri)
{
	PGSQL pgsql = { 0 };

	const char *ddls[] = {
		"drop schema if exists demo cascade",
		"create schema demo",
		"create table demo.tracking(ts timestamptz default now(), "
		"client integer, loop integer, retries integer, us bigint, recovery bool)",
		NULL
	};

	/* use the retry policy for a REMOTE node */
	pgsql_init(&pgsql, (char *) pguri, PGSQL_CONN_MONITOR);
	demoapp_set_retry_policy(&pgsql);

	for (int i = 0; ddls[i] != NULL; i++)
	{
		const char *command = ddls[i];

		log_info("Preparing demo schema: %s", command);

		if (!pgsql_execute(&pgsql, command))
		{
			return false;
		}
	}

	return true;
}


/*
 * demoapp_run runs clientsCount sub-processes for given duration (in seconds),
 * each sub-process implements a very simple INSERT INTO in a loop.
 */
bool
demoapp_run(const char *pguri, DemoAppOptions *demoAppOptions)
{
	int clientsCount = demoAppOptions->clientsCount;
	int startedClientsCount = 0;
	pid_t clientsPidArray[MAX_CLIENTS_COUNT + 1] = { 0 };

	IntString semIdString = intToString(log_semaphore.semId);

	log_info("Starting %d concurrent clients as sub-processes", clientsCount);


	/* Flush stdio channels just before fork, to avoid double-output problems */
	fflush(stdout);
	fflush(stderr);

	/* we want to use the same logs semaphore in the sub-processes */
	setenv(PG_AUTOCTL_LOG_SEMAPHORE, semIdString.strValue, 1);

	for (int index = 0; index <= clientsCount; index++)
	{
		pid_t fpid = fork();

		switch (fpid)
		{
			case -1:
			{
				log_error("Failed to fork client %d", index);

				(void) demoapp_terminate_clients(clientsPidArray,
												 startedClientsCount);

				return false;
			}

			case 0:
			{
				/* initialize the semaphore used for locking log output */
				if (!semaphore_init(&log_semaphore))
				{
					exit(EXIT_CODE_INTERNAL_ERROR);
				}

				/* set our logging facility to use our semaphore as a lock */
				(void) log_set_udata(&log_semaphore);
				(void) log_set_lock(&semaphore_log_lock_function);

				if (index == 0)
				{
					(void) demoapp_process_perform_switchover(demoAppOptions);
				}
				else
				{
					(void) demoapp_start_client(pguri, index, demoAppOptions);
				}


				(void) semaphore_finish(&log_semaphore);
				exit(EXIT_CODE_QUIT);
			}

			default:
			{
				/* fork succeeded, in parent */
				clientsPidArray[index] = fpid;
				++startedClientsCount;
			}
		}
	}

	/* all clients have started, now wait until they are done */
	return demoapp_wait_for_clients(clientsPidArray, startedClientsCount);
}


/*
 * demoapp_wait_for_clients waits until all the subprocess are finished.
 */
static bool
demoapp_wait_for_clients(pid_t clientsPidArray[], int startedClientsCount)
{
	int subProcessCount = startedClientsCount;
	bool allReturnCodeAreZero = true;

	while (subProcessCount > 0)
	{
		pid_t pid;
		int status;

		/* ignore errors */
		pid = waitpid(-1, &status, WNOHANG);

		switch (pid)
		{
			case -1:
			{
				if (errno == ECHILD)
				{
					/* no more childrens */
					return subProcessCount == 0;
				}

				pg_usleep(100 * 1000); /* 100 ms */
				break;
			}

			case 0:
			{
				/*
				 * We're using WNOHANG, 0 means there are no stopped or
				 * exited children, it's all good. It's the expected case
				 * when everything is running smoothly, so enjoy and sleep
				 * for awhile.
				 */
				pg_usleep(100 * 1000); /* 100 ms */
				break;
			}

			default:
			{
				/*
				 * One of the az vm create sub-commands has finished, find
				 * which and if it went all okay.
				 */
				int returnCode = WEXITSTATUS(status);

				/* find which client is done now */
				for (int index = 0; index < startedClientsCount; index++)
				{
					if (clientsPidArray[index] == pid)
					{
						if (returnCode != 0)
						{
							log_error("Client %d (pid %d) exited with code %d",
									  index, pid, returnCode);
							allReturnCodeAreZero = false;
						}
					}
				}

				--subProcessCount;
				break;
			}
		}
	}

	return allReturnCodeAreZero;
}


/*
 * demoapp_terminate_clients sends a SIGQUIT signal to known-running client
 * processes, and then wait until the processes are finished.
 */
static void
demoapp_terminate_clients(pid_t clientsPidArray[], int startedClientsCount)
{
	for (int index = 0; index < startedClientsCount; index++)
	{
		int pid = clientsPidArray[index];

		if (kill(pid, SIGQUIT) != 0)
		{
			log_error("Failed to send SIGQUIT to client %d pid %d: %m",
					  index, pid);
		}
	}
}


/*
 * demoapp_perform_switchover performs a switchover while the demo application
 * is running, once in a while
 */
static void
demoapp_process_perform_switchover(DemoAppOptions *demoAppOptions)
{
	Monitor monitor = { 0 };
	char *channels[] = { "state", NULL };

	char *formation = demoAppOptions->formation;
	int groupId = demoAppOptions->groupId;

	bool durationElapsed = false;
	uint64_t startTime = time(NULL);

	if (!monitor_init(&monitor, demoAppOptions->monitor_pguri))
	{
		/* errors have already been logged */
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	pgsql_set_monitor_interactive_retry_policy(&(monitor.pgsql.retryPolicy));

	if (demoAppOptions->duration <= 15)
	{
		log_error("Use a --duration of at least 15s for a failover to happen");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	log_info("Failover client is started, will failover in 10s "
			 "and every 20s after that");

	while (!durationElapsed)
	{
		uint64_t now = time(NULL);

		if ((now - startTime) > demoAppOptions->duration)
		{
			durationElapsed = true;
			break;
		}

		/* once every 20s period we do a failover at the 10s mark */
		if ((((int) (now - startTime)) % 20) != 10)
		{
			pg_usleep(500);
			continue;
		}

		log_info("pg_autoctl perform failover");

		/* start listening to the state changes before we perform_failover */
		if (!pgsql_listen(&(monitor.pgsql), channels))
		{
			log_error("Failed to listen to state changes from the monitor");
			pgsql_finish(&(monitor.pgsql));
			continue;
		}

		if (!monitor_perform_failover(&monitor, formation, groupId))
		{
			log_fatal("Failed to perform failover/switchover, "
					  "see above for details");

			/* skip this round entirely and continue */
			sleep(1);
			continue;
		}

		/* process state changes notification until we have a new primary */
		if (!monitor_wait_until_some_node_reported_state(&monitor,
														 formation,
														 groupId,
														 NODE_KIND_UNKNOWN,
														 PRIMARY_STATE))
		{
			log_error("Failed to wait until a new primary has been notified");
			continue;
		}
	}
}


/*
 * http://c-faq.com/lib/randrange.html
 */
#define random_between(M, N) \
	((M) + pg_lrand48() / (RAND_MAX / ((N) -(M) +1) + 1))

/*
 * demo_start_client starts a sub-process that implements our demo application:
 * the subprocess connects to Postgres and INSERT INTO our demo tracking table
 * some latency information.
 */
static void
demoapp_start_client(const char *pguri, int clientId,
					 DemoAppOptions *demoAppOptions)
{
	uint64_t startTime = time(NULL);
	bool durationElapsed = false;
	bool firstLoop = true;

	uint64_t previousLogLineTime = 0;

	int directs = 0;
	int retries = 0;
	int failovers = 0;
	int maxConnectionTimeNoRetry = 0;
	int maxConnectionTimeWithRetries = 0;

	for (int index = 0; !durationElapsed; index++)
	{
		PGSQL pgsql = { 0 };
		bool is_in_recovery = false;

		uint64_t now = time(NULL);

		if (firstLoop)
		{
			firstLoop = false;
		}
		else
		{
			int sleepTimeMs = random_between(10, 200);
			pg_usleep(sleepTimeMs * 1000);
		}

		if ((now - startTime) > demoAppOptions->duration)
		{
			durationElapsed = true;
			break;
		}

		/* use the retry policy for a REMOTE node */
		pgsql_init(&pgsql, (char *) pguri, PGSQL_CONN_MONITOR);
		demoapp_set_retry_policy(&pgsql);

		if (!pgsql_is_in_recovery(&pgsql, &is_in_recovery))
		{
			/* errors have already been logged */
			continue;
		}

		instr_time duration = pgsql.retryPolicy.connectTime;
		INSTR_TIME_SUBTRACT(duration, pgsql.retryPolicy.startTime);

		if (pgsql.retryPolicy.attempts == 0)
		{
			++directs;

			/* we could connect without retries, everything is fine */
			if (maxConnectionTimeNoRetry == 0 ||
				INSTR_TIME_GET_MILLISEC(duration) > maxConnectionTimeNoRetry)
			{
				maxConnectionTimeNoRetry = INSTR_TIME_GET_MILLISEC(duration);
			}

			/* log every 2s max, to avoid filling in the logs */
			if (previousLogLineTime == 0 ||
				(now - previousLogLineTime) >= 10)
			{
				log_info("Client %d connected %d times in less than %d ms",
						 clientId, directs, maxConnectionTimeNoRetry);

				previousLogLineTime = now;
			}
		}
		else
		{
			/* we had to retry connecting, a failover is in progress */
			++failovers;
			retries += pgsql.retryPolicy.attempts;

			if (maxConnectionTimeWithRetries == 0 ||
				INSTR_TIME_GET_MILLISEC(duration) > maxConnectionTimeWithRetries)
			{
				maxConnectionTimeWithRetries = INSTR_TIME_GET_MILLISEC(duration);
			}

			log_info("Client %d attempted to connect during a failover, "
					 "and had to attempt %d times which took %5.3f ms with "
					 "the current retry policy",
					 clientId,
					 pgsql.retryPolicy.attempts,
					 INSTR_TIME_GET_MILLISEC(duration));
		}

		char *sql =
			"insert into demo.tracking(client, loop, retries, us, recovery) "
			"values($1, $2, $3, $4, $5)";

		const Oid paramTypes[5] = { INT4OID, INT4OID, INT8OID, INT8OID, BOOLOID };
		const char *paramValues[5] = { 0 };

		paramValues[0] = intToString(clientId).strValue;
		paramValues[1] = intToString(index).strValue;
		paramValues[2] = intToString(pgsql.retryPolicy.attempts).strValue;
		paramValues[3] = intToString(INSTR_TIME_GET_MICROSEC(duration)).strValue;
		paramValues[4] = is_in_recovery ? "true" : "false";

		if (!pgsql_execute_with_params(&pgsql, sql, 5, paramTypes, paramValues,
									   NULL, NULL))
		{
			/* errors have already been logged */
		}

		/* the idea is to reconnect every time */
		pgsql_finish(&pgsql);
	}

	log_info("Client %d connected on first attempt %d times "
			 "with a maximum connection time of %d ms",
			 clientId, directs, maxConnectionTimeNoRetry);

	log_info("Client %d attempted to connect during a failover %d times "
			 "with a maximum connection time of %d ms and a total number "
			 "of %d retries with current retry policy",
			 clientId, failovers, maxConnectionTimeWithRetries, retries);
}


#define P95 "percentile_cont(0.95) within group (order by us::float8) / 1000.0"
#define P99 "percentile_cont(0.99) within group (order by us::float8) / 1000.0"

/*
 * demoapp_print_summary prints a summar of what happened during the run.
 */
void
demoapp_print_summary(const char *pguri, DemoAppOptions *demoAppOptions)
{
	char cat[MAXPGPATH] = { 0 };
	char psql[MAXPGPATH] = { 0 };

	const char *sql =

		/* *INDENT-OFF* */
		"select "
		"case when client is not null then format('Client %s', client) "
		"else ('All Clients Combined') end as \"Client\", "
		"count(*) as \"Connections\", "
		"sum(retries) as \"Retries\", "
		"round(min(us)/1000.0, 3) as \"Min Connect Time (ms)\", "
		"round(max(us)/1000.0, 3) as max, "
		"round((" P95 ")::numeric, 3) as p95, "
					  "round((" P99 ")::numeric, 3) as p99 "
		"from demo.tracking "
		"group by rollup(client) "
		"order by client nulls last;";
		/* *INDENT-ON* */

		char *args[16];
	int argsIndex = 0;

	/* we shell-out to psql so that we don't have to compute headers */
	if (!search_path_first("psql", psql, LOG_ERROR))
	{
		log_fatal("Failed to find program psql in PATH");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* we use /bin/cat as our PAGER */
	if (!search_path_first("cat", cat, LOG_ERROR))
	{
		log_fatal("Failed to find program cat in PATH");
		exit(EXIT_CODE_INTERNAL_ERROR);
	}

	/* set our PAGER to be just cat */
	setenv("PAGER", cat, 1);

	log_info("Summary for the demo app running with %d clients for %ds",
			 demoAppOptions->clientsCount, demoAppOptions->duration);

	args[argsIndex++] = psql;
	args[argsIndex++] = "--no-psqlrc";
	args[argsIndex++] = "-d";
	args[argsIndex++] = (char *) pguri;
	args[argsIndex++] = "-c";
	args[argsIndex++] = (char *) sql;
	args[argsIndex++] = NULL;

	/* we do not want to call setsid() when running this program. */
	Program program = initialize_program(args, false);

	program.capture = false;    /* don't capture output */
	program.tty = true;         /* allow sharing the parent's tty */

	(void) execute_subprogram(&program);

	free_program(&program);
}
