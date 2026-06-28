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

/* Runtime context for a running test */
typedef struct TestRunner
{
	TestSpec   *spec;
	char        projectName[64];   /* docker compose project name     */
	char        composeFile[1024]; /* path to generated compose YAML  */
	char        workDir[1024];     /* temp dir for this run           */
	int         monitorPort;       /* host port mapped to monitor:5432 */
	char        monitorConnStr[256];
	char        contextDir[1024];  /* absolute path used as docker build context */

	/* TAP counters */
	int         tapTotal;
	int         tapPass;
	int         tapFail;

	/* last SQL result (for CMD_EXPECT / CMD_EXPECT_ERROR) */
	char        lastSqlOutput[4096];
	bool        lastSqlFailed;     /* true when last sql raised an error  */
	char        lastSqlState[8];   /* SQLSTATE from last failed sql       */
	char        lastSqlService[64];/* service name of last sql command    */

	bool        composeUp;         /* compose stack is running        */
} TestRunner;

/*
 * CI mode: run the full spec (setup → sequence → teardown).
 * Emits TAP to stdout.  Returns true if all steps passed.
 */
bool runner_run(TestSpec *spec, const char *workDir);

/*
 * Interactive mode: bring up compose, run setup{}, then stop.
 * The caller gets the running cluster to explore interactively.
 * If withTmux is true, launch a tmux session instead of returning.
 */
bool runner_setup(TestSpec *spec, const char *workDir, bool withTmux);

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

#endif /* TEST_RUNNER_H */
