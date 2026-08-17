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

#define PGAF_MAX_NODES 32
#define PGAF_MAX_FORMATIONS 16
#define PGAF_MAX_NODE_VOLUMES 8
#define PGAF_MAX_STEPS 256
#define PGAF_MAX_SEQ 256
#define PGAF_TIMEOUT_DEFAULT 90
#define PGAF_MAX_ARCHIVERS 8
#define PGAF_MAX_ARCHIVER_FORMATIONS 8

/* -----------------------------------------------------------------------
 * Cluster topology (from the cluster { } block)
 *
 * Hierarchy:  cluster → monitor + formations → nodes
 *                    ↘ archivers (attach to one or more formations by name,
 *                      not nested inside any one of them -- see
 *                      TestArchiverNode's own comment below)
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
 *
 *       # Top-level archiver, sibling to monitor/formation -- see
 *       # TestArchiverNode's own comment for why this isn't nested inside
 *       # a formation_block the way ordinary nodes are. Braces are
 *       # mandatory here (unlike monitor's own bare form) -- see
 *       # archiver_block's own comment in test_spec_parse.y for why.
 *       archiver archiver1 {
 *           formation default
 *           region    dc1
 *       }
 *   }
 *
 * When "formation" has no name it defaults to "default".
 * When a formation block is omitted entirely the cluster is invalid.
 * ----------------------------------------------------------------------- */

typedef struct TestNode
{
	char name[128];
	PgInstanceKind kind;         /* standalone / coordinator / worker */
	int group;                   /* Citus group id; 0 = coordinator */
	int candidatePriority;       /* 0-100, default 50 */
	bool replicationQuorum;      /* participates in sync quorum */
	bool async;                  /* async standby (replicationQuorum = false) */

	/* create-time options — passed to pg_autoctl create via [options] ini */
	bool noMonitor;              /* --no-monitor: standalone node */
	bool createDeferred;         /* node waits before pg_autoctl create */
	bool launchDeferred;         /* node waits for pg_autoctl node start */
	bool suspended;              /* PG_AUTOCTL_SUSPENDED: node-active never
	                              * ticks on its own; only "fsm step <node>"
	                              * advances its FSM, one transition at a
	                              * time (see step_socket.c on the pg_autoctl
	                              * side) */
	bool listen;                 /* --listen 0.0.0.0: bind all interfaces */
	bool citusSecondary;         /* --citus-secondary */
	char citusClusterName[64];   /* --citus-cluster-name NAME */
	int pgPort;                  /* --pg-port N (0 = default 5432) */
	char debianCluster[64];      /* --debian-cluster NAME */
	char ssl[32];                /* per-node ssl override; "" = use cluster */
	char auth[32];               /* per-node auth override; "" = use cluster */
	char replicationPassword[256]; /* replication.password written to node ini */
	char monitorPassword[256];   /* pg_auto_failover.monitor_password written to node ini */
	char region[64];             /* --region NAME; "" = omit (defaults to "default" on monitor) */

	/* Extra Docker named volumes: volume <name> <containerPath> */
	struct
	{
		char name[64];
		char path[256];
	} volumes[PGAF_MAX_NODE_VOLUMES];
	int volumeCount;
} TestNode;

typedef struct TestFormation
{
	char name[128];               /* formation name; default "default" */
	int numSync;                 /* number-sync-standbys, -1 = unset */
	bool disableSecondary;       /* secondary = false in [formation] ini section */
	TestNode nodes[PGAF_MAX_NODES];
	int nodeCount;
} TestFormation;

/* -----------------------------------------------------------------------
 * Top-level archiver nodes (from a cluster-level "archiver <name> { ... }"
 * declaration, sibling to "monitor" and "formation" -- NOT nested inside a
 * formation_block's node_list the way ordinary/coordinator/worker nodes
 * are). This matches the real data model: pgautofailover.archiver has no
 * formationid column at all, and attaches to one or more formations
 * through the separate archiver_formation join table -- an archiver is a
 * process identity that formations attach to, not a member of any one of
 * them.
 *
 * Syntax:
 *
 *   archiver archiver1 {
 *       formation default
 *       region    eu-west         # optional; defaults to "default"
 *       create and launch deferred # optional; see below
 *   }
 *
 * Despite being declared at the top level, an archiver ends up represented
 * internally as an ordinary TestNode (kind = NODE_KIND_ARCHIVER), appended
 * to its own declared formation's own node list -- see parse_test_spec()'s
 * own fold_archivers_into_formations() call, run once right after
 * yyparse() returns. This means compose_gen.c's existing, fully-featured
 * per-node machinery (writing a real pg_autoctl_node.ini, "pg_autoctl node
 * run <ini>" as the container's own command, healthcheck/depends_on
 * ordering, create/launch-deferred handling) already used for an archiver
 * nested directly inside a formation_block -- the older, still-supported
 * spelling -- just works for these too, completely unmodified. Only the
 * *declaration* needs to be top-level, to match the real data model
 * (pgautofailover.archiver has no formationid column at all, it attaches
 * to formations through the separate archiver_formation join table, so an
 * archiver isn't a member of any one formation the way an ordinary node
 * genuinely is) -- once parsed, there is no other difference left.
 *
 * Only ever attaches to the FIRST formation listed: pg_autoctl create
 * archiver's own ini-driven bootstrap (nodespec.c) has no notion of more
 * than one --formation at create time. An archiver that needs to cover
 * more than one formation from the very start should still declare just
 * that first one here, then attach the rest once it's running (see
 * archiver_multi_formation.pgaf for the pattern: a direct `sql monitor {
 * SELECT pgautofailover.archiver_add_formation(...) }` step) --
 * fold_archivers_into_formations() exits with a clear error rather than
 * silently dropping any formation past the first.
 *
 * "create and launch deferred" (or either half alone) behaves exactly as
 * it does for an ordinary node: the container still runs "pg_autoctl node
 * run <ini>", but the ini's own [launch] section makes that command poll
 * and wait rather than actually registering -- `exec <name> pg_autoctl
 * node start` un-defers it explicitly, same as any other deferred node
 * (see citus_basic_operation.pgaf's own test_011 for why a Citus
 * formation's archiver needs this: it must not attempt to register before
 * every worker group already exists, and nothing here waits for that on
 * its own).
 * ----------------------------------------------------------------------- */
typedef struct TestArchiverNode
{
	char name[128];
	char region[64];             /* --region NAME; "" = omit (defaults to "default") */
	char formations[PGAF_MAX_ARCHIVER_FORMATIONS][128];
	int formationCount;
	bool createDeferred;         /* node waits before pg_autoctl create */
	bool launchDeferred;         /* node waits for pg_autoctl node start */
} TestArchiverNode;

typedef struct TestCluster
{
	TestFormation formations[PGAF_MAX_FORMATIONS];
	int formationCount;

	TestArchiverNode archivers[PGAF_MAX_ARCHIVERS];
	int archiverCount;

	bool withMonitor;            /* true when "monitor" keyword appears in cluster{} */
	bool withCitus;
	bool bindSource;             /* bind-source: mount repo root → /usr/src/pg_auto_failover */
	bool legacyStartup;          /* legacy-startup: use pg_autoctl create <kind> --run (v2.2 style) */

	/* cluster-level Docker / network options */
	char image[256];             /* Docker image tag; "" = build from source */
	char ssl[32];                /* self-signed | cert | off; default self-signed */
	char auth[32];               /* trust | md5 | scram; default trust */
	char monitorPassword[256];   /* password for autoctl_node; embedded in monitor URI */
	int monitorHostPort;         /* random free host port mapped to monitor:5432 */
	char extensionVersion[64];   /* PG_AUTOCTL_EXTENSION_VERSION env var for monitor */

	/*
	 * Optional second monitor, used for replace-monitor tests.
	 * When set, compose_gen emits a second monitor service with the given name
	 * and (if initiallyStoped) stops it after compose up so that the test can
	 * start it at the right moment.
	 */
	char secondMonitorName[64];  /* "" when no second monitor */
	bool secondMonitorStopped;   /* true when declared with "launch deferred" */
	int secondMonitorHostPort;   /* random free host port for second monitor */

	char monitorDebianCluster[64]; /* debian-cluster name for the monitor, "" otherwise */
	char monitorImageTarget[64];   /* Dockerfile build target for monitor; "" = "run" */
} TestCluster;

/* -----------------------------------------------------------------------
 * Commands (inside step/setup/teardown blocks)
 * ----------------------------------------------------------------------- */

/* Maximum number of states in a multi-state wait */
#define PGAF_MAX_WAIT_STATES 8

/* Maximum number of nodes in a promote list */
#define PGAF_MAX_PROMOTE_NODES 16

/* Maximum number of groups in a formation wait */
#define PGAF_MAX_WAIT_GROUPS 16

typedef enum TestCmdKind
{
	CMD_EXEC,            /* exec <svc> <args...>                        */
	CMD_EXEC_FAILS,      /* exec-fails <svc> <args...>                  */
	CMD_RUN,             /* run <svc> <args...> — compose run --rm      */
	CMD_WAIT_STATE,      /* wait until <node> state = <s> [timeout Ns] */
	CMD_WAIT_STATES,     /* wait until s1, s2 [in group N,...] [timeout Ns] */
	CMD_ASSERT_STATE,              /* assert <node> state = <s>                   */
	CMD_ASSERT_ASSIGNED,           /* assert <node> assigned-state = <s>          */
	CMD_PROMOTE,         /* promote node1 [, node2, ...]                */
	CMD_SQL,             /* sql <svc> { SQL }                           */
	CMD_EXPECT,          /* expect { text }                             */
	CMD_EXPECT_ERROR,    /* expect error [SQLSTATE]                     */
	CMD_NETWORK_OFF,     /* network disconnect <node>                   */
	CMD_NETWORK_ON,      /* network connect <node>                      */
	CMD_SLEEP,           /* sleep Ns                                    */
	CMD_COMPOSE_DOWN,    /* compose down                                */
	CMD_COMPOSE_START,   /* compose start <svc>                         */
	CMD_COMPOSE_STOP,    /* compose stop <svc>                          */
	CMD_COMPOSE_KILL,    /* compose kill <svc>  (SIGKILL, no grace)     */
	CMD_WAIT_STOPPED,    /* wait until <svc> stopped [timeout Ns]       */
	CMD_WAIT_MULTI,      /* wait until n1 state is s1 and n2 state is s2 */
	CMD_PG_AUTOCTL,      /* pg_autoctl <args> — runs in pgaftest container */
	CMD_STOP_POSTGRES,   /* stop postgres <node>  — pg_autoctl manual pgctl off */
	CMD_START_POSTGRES,  /* start postgres <node> — pg_autoctl manual pgctl on  */
	CMD_FSM_STEP,        /* fsm step <node> — pg_autoctl manual fsm step;
	                      * only meaningful for a node declared "suspended"
	                      * (see TestNode.suspended) */
	CMD_STAYS_WHILE,     /* assert <node> stays <state> while { cmds }  */
	CMD_SET_MONITOR,     /* set monitor <svc>  — switch active monitor service  */
	CMD_LOGS_CHECK,      /* logs <svc> [not] <pattern> — grep container logs    */
	CMD_COMPOSE_INJECT,  /* compose inject <image> <src> <svc>:<dst>            */
	CMD_FAILOVER,        /* perform failover [in formation F] [group G]
	                      * — calls pgautofailover.perform_failover() directly
	                      * on the monitor; service = formation, waitGroups[0] = group */
	CMD_NODEINI_SET,     /* nodeini set <node> <key> <value> — edits the
	                      * node's host-side pg_autoctl_node.ini [settings]
	                      * entry directly, exercising the supervisor's
	                      * file-watch live-apply path (the ini is read-only
	                      * inside the node's own container, so this can't go
	                      * through exec/compose). service = node,
	                      * state = key, args = value. */
	CMD_NODEINI_GET,     /* nodeini get <node> <key> <value> — reads the
	                      * node's host-side .ini [settings] entry directly
	                      * and asserts it equals <value>. service = node,
	                      * state = key, args = expected value. */
	CMD_WAIT_LSN,        /* wait until <node> replays <source> [timeout Ns] —
	                      * captures <source>'s current (or, if it's itself a
	                      * standby, last-replayed) WAL LSN at the moment this
	                      * command runs, then polls <node> until its own
	                      * last-replayed LSN has caught up to that captured
	                      * value. service = node to poll, state = source
	                      * node to capture the LSN from. */
	CMD_WAIT_SQL,        /* wait until sql <svc> { SQL } is { value } [timeout Ns]
	                      * — polls an arbitrary scalar SQL expression until
	                      * its (substring-matched, same semantics as
	                      * CMD_EXPECT) result contains <value>, or times
	                      * out. The building block "wait until wal segment
	                      * ... archived", "wait until archiver state is
	                      * ...", and "wait until basebackup ... is ..." are
	                      * all sugar for at parse time -- reach for this
	                      * directly only when none of those fit.
	                      * service = target service (e.g. "monitor"),
	                      * args = SQL text, expected = value to match. */
} TestCmdKind;

typedef struct TestCmd
{
	TestCmdKind kind;
	char service[64];              /* target service / node name          */
	char args[4096];               /* exec args or SQL text               */
	char state[64];                /* expected state / SQLSTATE           */
	char expected[4096];           /* for CMD_EXPECT                      */
	int timeoutSeconds;            /* for CMD_WAIT_STATE / CMD_WAIT_STATES */
	bool allowError;               /* CMD_SQL: don't fail if SQL errors   */

	/* CMD_WAIT_STATES: list of states that must ALL be present */
	char waitStates[PGAF_MAX_WAIT_STATES][64];
	int waitStateCount;

	/* CMD_WAIT_STATES: group filter; -1 = all groups */
	int waitGroups[PGAF_MAX_WAIT_GROUPS];
	int waitGroupCount;            /* 0 means no group filter (all groups) */

	/* CMD_WAIT_MULTI: per-node conditions (parallel arrays) */
	char waitNodes[PGAF_MAX_WAIT_STATES][64];

	/* waitStates[] re-used for the per-node state; waitStateCount is the count */

	/* CMD_PROMOTE: list of nodes to make primary in their respective groups */
	char promoteNodes[PGAF_MAX_PROMOTE_NODES][64];
	int promoteCount;

	/*
	 * CMD_WAIT_STATE / CMD_WAIT_MULTI:
	 * Optional list of intermediate states the node must pass through before
	 * reaching the target state.  Checked via LISTEN/NOTIFY on the monitor.
	 */
	char passThroughStates[PGAF_MAX_WAIT_STATES][64];
	int passThroughCount;

	/* CMD_STAYS_WHILE: nested command list (the "while { }" body) */
	struct TestCmd *body;

	/* CMD_LOGS_CHECK */
	bool logsNegate;           /* true → assert pattern NOT found */

	struct TestCmd *next;
} TestCmd;

/* -----------------------------------------------------------------------
 * Named step
 * ----------------------------------------------------------------------- */

typedef struct TestStep
{
	char name[128];
	TestCmd *commands;             /* linked list                         */
	struct TestStep *next;
} TestStep;

/* -----------------------------------------------------------------------
 * Full test specification
 * ----------------------------------------------------------------------- */

typedef struct TestSpec
{
	char filename[1024];

	TestCluster cluster;

	TestStep *setup;               /* single anonymous step               */
	TestStep *teardown;            /* single anonymous step               */

	TestStep *steps;               /* named steps, linked list            */
	int stepCount;

	char *sequence[PGAF_MAX_SEQ];        /* ordered step names            */
	int sequenceLength;
} TestSpec;

/* -----------------------------------------------------------------------
 * Parser entry point (implemented by bison grammar)
 * ----------------------------------------------------------------------- */

extern TestSpec * parse_test_spec(const char *filename);

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

TestStep * spec_find_step(TestSpec *spec, const char *name);
TestCmd * make_cmd(TestCmdKind kind);
TestStep * make_step(const char *name);

#endif /* TEST_SPEC_H */
