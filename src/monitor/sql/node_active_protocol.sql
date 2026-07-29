-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for the monitor's node_active() protocol.
--
-- Covers two concrete regressions:
--
--   #1062 — Primary has health=BAD (health-check worker cannot reach it) but
--            IS calling node_active with pgIsRunning=true.  NodeIsHealthy must
--            return true via the fresh-report override so no spurious failover
--            is triggered.
--
--   #1032 — After a failover the old primary catches up while the health
--            checker still marks it BAD.  The fixed NodeIsHealthy() allows
--            catchingup→secondary to proceed.  Without the fix the transition
--            was permanently blocked (NodeIsHealthy returned false for any
--            node with health=BAD regardless of the live report).
--
-- Additional regression: NODE_HEALTH_UNKNOWN (-1) is the initial DB value for
-- newly registered nodes (no health-check worker has run yet).  NodeIsHealthy
-- must treat UNKNOWN as "trust the keeper's own pgIsRunning report."  This
-- manifests during formation bootstrap: the catchingup→secondary transition
-- requires NodeIsHealthy(joining-node) to be true.
--
-- Also exercises the stale in-memory struct fix in NodeActive(): after
-- ReportAutoFailoverNodeState writes pgIsRunning to the DB, the same value
-- must be synced back into the pgAutoFailoverNode struct before
-- ProceedGroupState runs, otherwise pgIsRunning appears stale to
-- NodeIsHealthy when the keeper changes from pgIsRunning=false to true.

\x on

-- ── formation and node registration ─────────────────────────────────────────

SELECT pgautofailover.create_formation('fsm_test', 'pgsql', 'postgres', true, 1);

-- sysidentifier=1: a non-zero value satisfies the same_system_identifier
-- constraint without a separate set_node_system_identifier() call.
SELECT *
  FROM pgautofailover.register_node('fsm_test', 'node1', 5432,
                                    'postgres', 'node1', 1);

SELECT nodeid AS n1 FROM pgautofailover.node
 WHERE formationid = 'fsm_test' AND nodename = 'node1' \gset

SELECT *
  FROM pgautofailover.register_node('fsm_test', 'node2', 5432,
                                    'postgres', 'node2', 1);

SELECT nodeid AS n2 FROM pgautofailover.node
 WHERE formationid = 'fsm_test' AND nodename = 'node2' \gset

-- ── formation bootstrap ──────────────────────────────────────────────────────
--
-- IsCurrentState(node, S) requires both goalState=S and reportedState=S, so
-- every intermediate state must be explicitly confirmed before the next guard
-- fires.  The sequence below mirrors what two real keepers would produce.

-- node1: single (confirm)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'single');

-- node2: wait_standby (confirm)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'wait_standby');

-- node1: single → wait_primary (secondary has joined in WAIT_STANDBY)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

-- node1: wait_primary (confirm)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- node2: wait_standby → catchingup (primary is confirmed in WAIT_PRIMARY)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'wait_standby');

-- node2: catchingup → secondary
-- NODE_HEALTH_UNKNOWN (-1) regression: health is -1 (default for new nodes;
-- the health-check worker has not run).  NodeIsHealthy must trust the
-- keeper's pgIsRunning=true instead of returning false.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

-- node2: secondary (confirm)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- node1: wait_primary → primary (secondary quorum satisfied)
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- ── test_001: steady state ───────────────────────────────────────────────────

SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- ── test_002: issue #1062 ─────────────────────────────────────────────────────
--
-- Health checker marks primary BAD while the primary is still calling
-- node_active with pgIsRunning=true.  NodeIsHealthy must return true via the
-- fresh-report override:
--   health=BAD, healthchecktime < reportTime, reportTime within 1s of now.
-- No spurious failover must be triggered.

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now() - interval '1 second'
 WHERE nodename = 'node1';

-- Primary reports pgIsRunning=true; fresh report overrides the BAD health.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'primary',
                                  current_pg_is_running => true,
                                  current_lsn => '0/5000');

-- Secondary calls in; NodeIsUnhealthy(primary) is false — no promotion.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'secondary',
                                  current_pg_is_running => true,
                                  current_lsn => '0/5000');

-- restore health before the full-failure test
UPDATE pgautofailover.node
   SET health = 1,
       healthchecktime = now()
 WHERE nodename = 'node1';

-- ── test_003: two-node failover ───────────────────────────────────────────────

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now() - interval '1 second'
 WHERE nodename = 'node1';

-- Primary's own heartbeat with pgIsRunning=false.  In the two-node case
-- ProceedGroupStateForPrimaryNode has no self-demotion rule; the failover
-- trigger fires from the standby's heartbeat.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'primary',
                                  current_pg_is_running => false,
                                  current_lsn => '0/5000');

-- Secondary calls in: NodeIsUnhealthy(primary) is true → prepare_promotion.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'secondary',
                                  current_pg_is_running => true,
                                  current_lsn => '0/5000');

-- node2 reports prepare_promotion → assigned stop_replication.
-- node1 gets goalState=demote_timeout.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'prepare_promotion',
                                  current_pg_is_running => true,
                                  current_lsn => '0/5000');

-- node1 reports demote_timeout; satisfies IsCurrentState(node1, DEMOTE_TIMEOUT)
-- so that the next node2 heartbeat can advance.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'demote_timeout',
                                  current_pg_is_running => false,
                                  current_lsn => '0/5000');

-- node2 reports stop_replication → assigned wait_primary.
-- node1 gets goalState=demoted.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'stop_replication',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- node2 in wait_primary; no secondary in quorum yet.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'wait_primary',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- ── test_004: issue #1032 ─────────────────────────────────────────────────────
--
-- Old primary (node1) resurfaces.  Health checker is still marking it BAD
-- (recovery in progress).  The fixed NodeIsHealthy() must allow the
-- catchingup→secondary transition.
--
-- The stale in-memory struct fix is also exercised here: node1's previous
-- node_active call reported pgIsRunning=false (demoted).  The DB therefore
-- has reportedpgisrunning=false.  When node1 reports catchingup with
-- pgIsRunning=true, ReportAutoFailoverNodeState writes true to the DB but the
-- in-memory struct still holds false.  Without the fix, ProceedGroupState
-- sees pgIsRunning=false and NodeIsHealthy returns false, permanently blocking
-- the transition.

UPDATE pgautofailover.node
   SET health = 1,
       healthchecktime = now()
 WHERE nodename = 'node1';

-- node1 resurfaces reporting 'primary' while its goalState is 'demoted'.
-- IsCurrentState(node1, DEMOTED) is false (reportedState mismatch), so no
-- DEMOTED rule fires; the monitor returns the current goalState = demoted.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'primary',
                                  current_pg_is_running => true,
                                  current_tli => 1,
                                  current_lsn => '0/4F00');

-- node1 reports demoted → catchingup assigned.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'demoted',
                                  current_pg_is_running => false,
                                  current_tli => 1,
                                  current_lsn => '0/4F00');

-- Health checker marks node1 BAD again (recovery still in progress).
UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now() - interval '1 second'
 WHERE nodename = 'node1';

-- node1 calls node_active with pgIsRunning=true.
-- NodeIsHealthy(node1) returns true via the fresh-report override.
-- The stale-struct fix ensures the pgIsRunning=true from this call's report
-- is visible to ProceedGroupState within the same call.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'catchingup',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- node1 confirms secondary.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'secondary',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- node2 now has a healthy secondary in the quorum → promoted to primary.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'wait_primary',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- ── test_005: start_maintenance on primary with health=UNKNOWN secondary ──────
--
-- Regression for the CountHealthyCandidates / IsHealthy inconsistency.
--
-- NodeIsHealthy() treats NODE_HEALTH_UNKNOWN as "trust pgIsRunning", so
-- catchingup→secondary can fire before the health-check worker runs.  The
-- old IsHealthy() returned false for UNKNOWN, so start_maintenance() counted
-- 0 healthy candidates even when the secondary was fully reachable.
--
-- After fixing IsHealthy() to mirror NodeIsHealthy() for UNKNOWN health,
-- start_maintenance must succeed and assign prepare_maintenance /
-- prepare_promotion.

-- Confirm node2 as primary (last test_004 call left it in wait_primary goal).
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n2, 0,
                                  current_group_role => 'primary',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- Confirm node1 as secondary.
SELECT *
  FROM pgautofailover.node_active('fsm_test', :n1, 0,
                                  current_group_role => 'secondary',
                                  current_pg_is_running => true,
                                  current_tli => 2,
                                  current_lsn => '0/5000');

-- Force node1 health back to UNKNOWN to simulate a node that reached
-- SECONDARY before the health-check worker ran (the window our FSM fix
-- opened up).  Without the IsHealthy() fix this causes start_maintenance
-- to fail with "0 candidate nodes available".
UPDATE pgautofailover.node
   SET health = -1
 WHERE nodename = 'node1';

-- start_maintenance on the primary (node2): must succeed despite
-- node1 health=UNKNOWN, because IsHealthy() now trusts pgIsRunning=true
-- when no health check has run yet.
SELECT pgautofailover.start_maintenance(:n2);

-- Verify: node2 → prepare_maintenance, node1 → prepare_promotion.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'fsm_test'
 ORDER BY nodename;

-- ── test_006: killed-primary failover (health=BAD, pgIsRunning still true) ────
--
-- The container-killed scenario: the primary stops reporting node_active (so
-- pgIsRunning stays TRUE in the DB from its last heartbeat), the health-check
-- worker eventually marks it BAD, and after unhealthyTimeoutMs the monitor
-- should fire secondary → prepare_promotion even though the primary never
-- self-reported pgIsRunning=false.
--
-- Distinct from test_003 (self-reported-down path via pgIsRunning=false).
-- Here we exercise the time+health-check-based unhealthy path.
--
-- Uses a fresh formation to start from a clean state.

SELECT pgautofailover.create_formation('killed_test', 'pgsql', 'postgres', true, 0);

SELECT *
  FROM pgautofailover.register_node('killed_test', 'ka', 5432,
                                    'postgres', 'ka', 1);

SELECT nodeid AS ka FROM pgautofailover.node
 WHERE formationid = 'killed_test' AND nodename = 'ka' \gset

SELECT *
  FROM pgautofailover.register_node('killed_test', 'kb', 5432,
                                    'postgres', 'kb', 1);

SELECT nodeid AS kb FROM pgautofailover.node
 WHERE formationid = 'killed_test' AND nodename = 'kb' \gset

-- Bootstrap to primary + secondary.
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :ka, 0, current_group_role => 'single');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :kb, 0, current_group_role => 'wait_standby');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :ka, 0, current_group_role => 'single', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :ka, 0, current_group_role => 'wait_primary', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :kb, 0, current_group_role => 'wait_standby');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :kb, 0, current_group_role => 'catchingup', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :kb, 0, current_group_role => 'secondary', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :ka, 0, current_group_role => 'wait_primary', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :ka, 0, current_group_role => 'primary', current_lsn => '0/3000');
SELECT assigned_group_state FROM pgautofailover.node_active('killed_test', :kb, 0, current_group_role => 'secondary', current_lsn => '0/3000');

-- Lower startup_grace_period to 1ms so the time-based unhealthy path fires
-- immediately in the test environment (in production the server has been up
-- far longer than the 10s default).
SET pgautofailover.startup_grace_period = 1;

-- Simulate killed primary (ka): health checker ran and marked it BAD,
-- but ka's last node_active set pgIsRunning=true and that value is still in
-- the DB — it never self-reported pgIsRunning=false.
-- reporttime is set 60s in the past to exceed UnhealthyTimeoutMs (20s default).
-- healthchecktime is recent so the health-check-ran-after-startup condition holds.
UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds'
 WHERE formationid = 'killed_test' AND nodename = 'ka';

-- kb (secondary) calls node_active: NodeIsUnhealthy(ka, ctx) must fire via
-- the time-based path (reportTime > unhealthyTimeoutMs AND health=BAD),
-- triggering secondary → prepare_promotion even though ka never called
-- node_active with pgIsRunning=false.
SELECT *
  FROM pgautofailover.node_active('killed_test', :kb, 0,
                                  current_group_role => 'secondary',
                                  current_pg_is_running => true,
                                  current_lsn => '0/3000');

RESET pgautofailover.startup_grace_period;

-- ── test_007: set_node_region ────────────────────────────────────────────────
--
-- region is purely informational: setting it must not touch any FSM state,
-- and must be rejected for an unknown node or an empty value.

SELECT region FROM pgautofailover.node WHERE nodename = 'node1';

SELECT pgautofailover.set_node_region('fsm_test', 'node1', 'dc2');

SELECT region FROM pgautofailover.node WHERE nodename = 'node1';

-- unknown node: error
SELECT pgautofailover.set_node_region('fsm_test', 'unknown_node', 'dc2');

-- empty region: error
SELECT pgautofailover.set_node_region('fsm_test', 'node1', '');

-- ── test_008: report_postgres_version ────────────────────────────────────────
--
-- Postgres/Citus version info is a plain self-report: not part of the FSM,
-- never queried by node_active, and (unlike set_node_region) doesn't error
-- on an unknown node -- a keeper reporting on its own already-known nodeid
-- is a harmless no-op if that row no longer exists, same as
-- ReportAutoFailoverNodeState()'s own report path.

-- never reported yet: all four columns are NULL
SELECT pg_versionnum, pg_version, pg_versionstring, citus_version
  FROM pgautofailover.node WHERE nodeid = :n1;

SELECT pgautofailover.report_postgres_version(
    :n1, 170003, '17.3', 'PostgreSQL 17.3 on x86_64-linux', '12.1');

SELECT pg_versionnum, pg_version, pg_versionstring, citus_version
  FROM pgautofailover.node WHERE nodeid = :n1;

-- citus_version omitted (defaults to NULL): a later report legitimately
-- clears a previously-reported citus_version, e.g. Citus was removed
SELECT pgautofailover.report_postgres_version(
    :n1, 170003, '17.3', 'PostgreSQL 17.3 on x86_64-linux');

SELECT pg_versionnum, pg_version, pg_versionstring, citus_version
  FROM pgautofailover.node WHERE nodeid = :n1;

-- null node_id: error
SELECT pgautofailover.report_postgres_version(NULL, 170003);

-- unknown node_id: silent no-op, not an error
SELECT pgautofailover.report_postgres_version(-1, 170003);

-- event summary: which MonitorFSM[] rule (if any) produced each of this
-- test's own state-change events, for each of the two formations used
-- above. Exercises pgautofailover.last_events() against a real scenario --
-- its own SELECT list didn't match pgautofailover.event's column set for a
-- long time, breaking it outright, and nothing in this suite ever called
-- it to notice (see monitor.sql's own minimal-repro coverage). Filtering
-- by formationid isolates each summary from the other, and from every
-- other test in this schedule sharing the same event table -- safe
-- regardless of where in this file (or the whole schedule) it runs.
-- eventid/eventtime omitted: eventid is a database-wide sequence shared by
-- every test in this schedule (see regress_schedule's own comment) and
-- eventtime is a live timestamp -- neither is a stable value to pin here.
SELECT reportedstate, goalstate, rule_pos, rule_section, description
  FROM pgautofailover.last_events('fsm_test', count => 100);

SELECT reportedstate, goalstate, rule_pos, rule_section, description
  FROM pgautofailover.last_events('killed_test', count => 100);
