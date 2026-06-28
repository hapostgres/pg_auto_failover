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

#define PGAF_MAX_NODES       32
#define PGAF_MAX_FORMATIONS  16
#define PGAF_MAX_STEPS      256
#define PGAF_MAX_SEQ        256
#define PGAF_TIMEOUT_DEFAULT 90

/* -----------------------------------------------------------------------
 * Cluster topology (from the cluster { } block)
 *
 * Hierarchy:  cluster → monitor + formations → nodes
 *
 * Syntax:
 *
 *   cluster {
 *       monitor [port 15432]
 *       image   "pg_auto_failover:pg17"   # optional; default: build from src
 *       ssl     self-signed               # optional; default: self-signed
 *       auth    trust                     # optional; default: trust
 *
 *       formation [name] [num-sync N] {
 *           node1
 *           node2
 *           node3  async  candidate-priority 0
 *           coord  coordinator
 *           w1     worker  group 1
 *       }
 *
 *       # Multiple formations (Citus use case)
 *       formation workers num-sync 1 {
 *           w1 worker group 1
 *           w2 worker group 1
 *       }
 *   }
 *
 * When "formation" has no name it defaults to "default".
 * When a formation block is omitted entirely the cluster is invalid.
 * ----------------------------------------------------------------------- */

typedef struct TestNode
{
	char name[64];
	PgInstanceKind kind;         /* standalone / coordinator / worker */
	int  group;                  /* Citus group id; 0 = coordinator */
	int  candidatePriority;      /* 0-100, default 50 */
	bool replicationQuorum;      /* participates in sync quorum */
	bool async;                  /* async standby (replicationQuorum = false) */

	/* create-time options — passed to pg_autoctl create via [options] ini */
	bool noMonitor;              /* --no-monitor: standalone node */
	bool listen;                 /* --listen 0.0.0.0: bind all interfaces */
	bool citusSecondary;         /* --citus-secondary */
	char citusClusterName[64];   /* --citus-cluster-name NAME */
	int  pgPort;                 /* --pg-port N (0 = default 5432) */
	char debianCluster[64];      /* --debian-cluster NAME */
	char ssl[32];                /* per-node ssl override; "" = use cluster */
	char auth[32];               /* per-node auth override; "" = use cluster */
} TestNode;

typedef struct TestFormation
{
	char name[64];               /* formation name; default "default" */
	int  numSync;                /* number-sync-standbys, -1 = unset */
	TestNode nodes[PGAF_MAX_NODES];
	int  nodeCount;
} TestFormation;

typedef struct TestCluster
{
	TestFormation formations[PGAF_MAX_FORMATIONS];
	int           formationCount;

	bool withMonitor;            /* always true for now */
	bool withCitus;
	int  monitorHostPort;        /* host-side port mapped to monitor:5432; 0 = auto */

	/* cluster-level Docker / network options */
	char image[256];             /* Docker image tag; "" = build from source */
	char ssl[32];                /* self-signed | cert | off; default self-signed */
	char auth[32];               /* trust | md5 | scram; default trust */
} TestCluster;

/* -----------------------------------------------------------------------
 * Commands (inside step/setup/teardown blocks)
 * ----------------------------------------------------------------------- */

typedef enum TestCmdKind
{
	CMD_EXEC,            /* exec <svc> <args...>                        */
	CMD_EXEC_FAILS,      /* exec-fails <svc> <args...>                  */
	CMD_WAIT_STATE,      /* wait until <node> state = <s> [timeout Ns] */
	CMD_ASSERT_STATE,    /* assert <node> state = <s>                   */
	CMD_ASSERT_ASSIGNED, /* assert <node> assigned-state = <s>          */
	CMD_SQL,             /* sql <svc> { SQL }                           */
	CMD_EXPECT,          /* expect { text }                             */
	CMD_EXPECT_ERROR,    /* expect error [SQLSTATE]                     */
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
	char        state[64];         /* expected state / SQLSTATE           */
	char        expected[4096];    /* for CMD_EXPECT                      */
	int         timeoutSeconds;    /* for CMD_WAIT_STATE                  */
	bool        allowError;        /* CMD_SQL: don't fail if SQL errors   */
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
