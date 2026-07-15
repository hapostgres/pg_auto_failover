/*
 * src/bin/pgaftest/test_runner.h
 *   Test execution engine for .pgaf specs.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdbool.h>
#include "test_spec.h"
#include "pgsql.h"

/* Runtime context for a running test */
typedef struct TestRunner
{
	TestSpec *spec;
	char projectName[64];          /* docker compose project name     */
	char composeFile[1024];        /* path to generated compose YAML  */
	char composeBase[1152];        /* "docker compose -p name [-f file]" */
	char workDir[1024];            /* temp dir for this run           */
	char contextDir[1024];         /* absolute path used as docker build context */
	char specFile[1024];           /* absolute path to the .pgaf spec file */
	char specDir[1024];            /* dirname(specFile): directory of the spec */
	char hostSpecDir[1024];        /* host-side specDir for compose bind-mount */

	/* TAP counters and buffered output */
	int tapTotal;
	int tapPass;
	int tapFail;
	char tapBuffer[65536];
	int tapBufferLen;

	/* last SQL result (for CMD_EXPECT / CMD_EXPECT_ERROR) */
	char lastSqlOutput[4096];
	bool lastSqlFailed;            /* true when last sql raised an error  */
	char lastSqlState[8];          /* SQLSTATE from last failed sql       */
	char lastSqlService[64];       /* service name of last sql command    */

	bool composeUp;                /* compose stack is running        */
	bool interactive;              /* --tmux: pgaftest service sleeps instead of running */

	/*
	 * Direct libpq connection to the monitor's exposed postgres port.
	 * Used for LISTEN "state" notifications so wait loops are event-driven
	 * instead of polling via docker exec.
	 */
	PGSQL notifyConn;
	bool notifyConnected;

	/*
	 * Which monitor service the runner currently targets for LISTEN/NOTIFY
	 * and monitor_get_node_state() queries.  Defaults to "monitor"; changed
	 * by the "set monitor <svc>" DSL command in a replace-monitor test.
	 */
	char activeMonitorService[64];

	/* Per-step timing/result for the post-run summary */
	struct
	{
		char name[256];
		bool passed;
		long elapsed_ms;
	} stepResults[PGAF_MAX_SEQ];
	int stepResultCount;
} TestRunner;

/*
 * CI mode: run the full spec (setup → sequence → teardown).
 * Emits TAP to stdout.  Returns true if all steps passed.
 */

/*
 * noCleanup: when true, skip `compose down` after the run so the stack stays
 * up for post-mortem inspection.  Use `pgaftest down <spec.pgaf>` to clean up.
 */
bool runner_run(TestSpec *spec, const char *workDir, bool noCleanup);

/*
 * Interactive mode: bring up compose, run setup{}, then stop.
 * The caller gets the running cluster to explore interactively.
 * If withTmux is true, launch a tmux session instead of returning.
 */
bool runner_setup(TestSpec *spec, const char *workDir, bool withTmux);

/*
 * Run only the setup{} block against an already-running compose stack.
 * Used internally by `pgaftest _setup_` (the tmux bottom-pane helper).
 */
bool runner_run_setup_only(TestSpec *spec, const char *workDir);

/*
 * Run a single named step against an already-running compose stack.
 * Used by `pgaftest step <name>`.
 */
bool runner_step(TestSpec *spec, const char *workDir, const char *stepName);

/*
 * Tear down the compose stack (run teardown{} then compose down).
 * Used by `pgaftest down`.
 */
bool runner_down(TestSpec *spec, const char *workDir);

/*
 * Print the generated docker-compose.yml without starting anything.
 */
bool runner_show(TestSpec *spec);

/* Interactive sub-commands (DSL mirror, for use inside pgaftest container) */
bool runner_wait(TestSpec *spec, const char *workDir,
				 const char *nodeName, const char *targetState,
				 int timeoutSecs);
bool runner_sql(TestSpec *spec, const char *workDir,
				const char *service, const char *query);
bool runner_network(TestSpec *spec, const char *workDir,
					const char *nodeName, bool connect);
bool runner_assert(TestSpec *spec, const char *workDir,
				   const char *nodeName, const char *targetState);

/*
 * Prepare an output directory with docker-compose.yml, *.ini files, and a
 * Makefile.  Prints the `docker compose up` command to stdout.
 * outDir is created if it does not exist.  Pass NULL to derive from spec name.
 */
bool runner_prepare(TestSpec *spec, const char *outDir);

#endif /* TEST_RUNNER_H */
