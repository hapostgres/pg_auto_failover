/*
 * src/bin/pgaftest/test_spec.h
 *   AST types for the .pgaf test specification format.
 *
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the PostgreSQL License.
 */

#ifndef TEST_SPEC_H
#define TEST_SPEC_H

#include <stdbool.h>
#include "pgsetup.h"   /* PgInstanceKind */

#define PGAF_MAX_NODES    32
#define PGAF_MAX_STEPS    256
#define PGAF_MAX_SEQ      256
#define PGAF_TIMEOUT_DEFAULT 90

/* -----------------------------------------------------------------------
 * Cluster topology (from the cluster { } block)
 * ----------------------------------------------------------------------- */

typedef struct TestNode
{
	char name[64];
	PgInstanceKind kind;         /* standalone / coordinator / worker */
	int  group;                  /* Citus group id; 0 = coordinator */
	int  candidatePriority;      /* 0-100, default 50 */
	bool replicationQuorum;      /* participates in sync quorum */
	bool async;                  /* async standby */
} TestNode;

typedef struct TestCluster
{
	TestNode nodes[PGAF_MAX_NODES];
	int      nodeCount;
	bool     withMonitor;        /* always true for now */
	bool     withCitus;
	int      numSync;            /* number-sync-standbys, -1 = unset */

	/* cluster-level Docker / network options */
	char     image[256];         /* Docker image tag; "" = build from source */
	char     ssl[32];            /* self-signed | cert | off; default self-signed */
	char     auth[32];           /* trust | md5 | scram; default trust */
	char     formation[64];      /* formation name; default "default" */
	int      monitorHostPort;    /* host-side port mapped to monitor:5432; 0 = auto */
} TestCluster;

/* -----------------------------------------------------------------------
 * Commands (inside step/setup/teardown blocks)
 * ----------------------------------------------------------------------- */

typedef enum TestCmdKind
{
	CMD_EXEC,            /* exec <svc> <args...>                        */
	CMD_WAIT_STATE,      /* wait until <node> state = <s> [timeout Ns] */
	CMD_ASSERT_STATE,    /* assert <node> state = <s>                   */
	CMD_ASSERT_ASSIGNED, /* assert <node> assigned-state = <s>          */
	CMD_SQL,             /* sql <svc> { SQL }                           */
	CMD_EXPECT,          /* expect { text }                             */
	CMD_NETWORK_OFF,     /* network disconnect <node>                   */
	CMD_NETWORK_ON,      /* network connect <node>                      */
	CMD_SLEEP,           /* sleep Ns                                    */
	CMD_COMPOSE_DOWN,    /* compose down                                */
} TestCmdKind;

typedef struct TestCmd
{
	TestCmdKind kind;
	char        service[64];       /* target service / node name          */
	char        args[4096];        /* exec args or SQL text               */
	char        state[64];         /* expected state string               */
	char        expected[4096];    /* for CMD_EXPECT                      */
	int         timeoutSeconds;    /* for CMD_WAIT_STATE                  */
	struct TestCmd *next;
} TestCmd;

/* -----------------------------------------------------------------------
 * Named step
 * ----------------------------------------------------------------------- */

typedef struct TestStep
{
	char        name[64];
	TestCmd    *commands;          /* linked list                         */
	struct TestStep *next;
} TestStep;

/* -----------------------------------------------------------------------
 * Full test specification
 * ----------------------------------------------------------------------- */

typedef struct TestSpec
{
	char         filename[1024];

	TestCluster  cluster;

	TestStep    *setup;            /* single anonymous step               */
	TestStep    *teardown;         /* single anonymous step               */

	TestStep    *steps;            /* named steps, linked list            */
	int          stepCount;

	char        *sequence[PGAF_MAX_SEQ]; /* ordered step names            */
	int          sequenceLength;
} TestSpec;

/* -----------------------------------------------------------------------
 * Parser entry point (implemented by bison grammar)
 * ----------------------------------------------------------------------- */

extern TestSpec *parse_test_spec(const char *filename);

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

TestStep *spec_find_step(TestSpec *spec, const char *name);
TestCmd  *make_cmd(TestCmdKind kind);
TestStep *make_step(const char *name);

#endif /* TEST_SPEC_H */
