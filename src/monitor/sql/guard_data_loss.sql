-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for pgautofailover.guard_data_loss GUC.
--
-- Verifies that:
--   1. With guard_data_loss=true (default), a 3-node failover stuck in the
--      report_lsn phase (one quorum node has not yet reported) does NOT
--      proceed -- perform_failover returns without selecting a candidate.
--
--   2. With guard_data_loss=false the same scenario proceeds: the surviving
--      report_lsn node is selected as the failover candidate even though
--      the other quorum node never reported its LSN.
--

\x on

-- ── formation setup ──────────────────────────────────────────────────────────

SELECT pgautofailover.create_formation('gdl_test', 'pgsql', 'postgres', true, 1);

-- Register three nodes (number_sync_standbys=1, so two quorum standbys needed
-- to safely commit).
SELECT *
  FROM pgautofailover.register_node('gdl_test', 'p', 5432,
                                    'postgres', 'p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'gdl_test' AND nodename = 'p' \gset

SELECT *
  FROM pgautofailover.register_node('gdl_test', 's1', 5432,
                                    'postgres', 's1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'gdl_test' AND nodename = 's1' \gset

SELECT *
  FROM pgautofailover.register_node('gdl_test', 's2', 5432,
                                    'postgres', 's2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'gdl_test' AND nodename = 's2' \gset

-- ── bootstrap ────────────────────────────────────────────────────────────────
--
-- Drive the FSM from init through to primary + secondary + secondary.

-- p: single (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'single');

-- s1: wait_standby (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

-- p: single → wait_primary
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

-- p: wait_primary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- s1: wait_standby → catchingup
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

-- s1: catchingup → secondary
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns1, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

-- s1: secondary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: wait_primary → primary (now that s1 is secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- p: primary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- s1: secondary (confirm after primary confirmed)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- s2: wait_standby (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns2, 0,
                                  current_group_role => 'wait_standby');

-- s2: catchingup
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

-- s2: secondary
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: primary (refresh to pick up new secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('gdl_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- Verify final formation state: p=primary, s1=secondary, s2=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'gdl_test'
 ORDER BY nodename;

-- ── simulate stuck failover ──────────────────────────────────────────────────
--
-- Kill the primary (p) and one quorum standby (s2) simultaneously.
-- We simulate this by:
--   1. Marking p and s2 as unhealthy (health=BAD, old reporttime).
--   2. Manually placing s1 in report_lsn/report_lsn (it already called
--      node_active and transitioned itself), while s2 is still in
--      secondary/secondary with goalState=report_lsn assigned (missing).
--
-- This mirrors what happens in production: the monitor assigns report_lsn
-- to all standbys, s1 reports immediately, but s2 is dead and never will.

SET pgautofailover.startup_grace_period = 1;

-- Mark p as dead (health=BAD, stale report).
UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds'
 WHERE formationid = 'gdl_test' AND nodename = 'p';

-- Mark s2 as dead as well (health=BAD, stale report, pgIsRunning=false).
UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds',
       reportedpgisrunning = false
 WHERE formationid = 'gdl_test' AND nodename = 's2';

-- Demote p: set it to draining/draining so the FSM sees no primary.
UPDATE pgautofailover.node
   SET goalstate = 'draining', reportedstate = 'draining'
 WHERE formationid = 'gdl_test' AND nodename = 'p';

-- Put s1 in report_lsn/report_lsn (it already ran node_active and confirmed).
UPDATE pgautofailover.node
   SET goalstate = 'report_lsn', reportedstate = 'report_lsn'
 WHERE formationid = 'gdl_test' AND nodename = 's1';

-- Put s2 in secondary/secondary with goalState=report_lsn assigned but NOT
-- yet confirmed (missingNodesCount will be > 0 for s2).
UPDATE pgautofailover.node
   SET goalstate = 'report_lsn', reportedstate = 'secondary'
 WHERE formationid = 'gdl_test' AND nodename = 's2';

-- Verify the manufactured stuck state.
SELECT nodename, goalstate, reportedstate, health
  FROM pgautofailover.node
 WHERE formationid = 'gdl_test'
 ORDER BY nodename;

-- ── test 1: guard_data_loss = true (default) blocks ─────────────────────────
--
-- perform_failover should find no primary, locate s1 in report_lsn, drive
-- ProceedGroupState for s1, which calls ProceedGroupStateForMSFailover, which
-- hits the guard and returns false without selecting a candidate.
-- The node states should remain unchanged (no candidate selected).

-- Default is true; make it explicit.
SET pgautofailover.guard_data_loss TO true;

SELECT pgautofailover.perform_failover('gdl_test', 0);

-- States must be unchanged: no candidate was promoted.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'gdl_test'
 ORDER BY nodename;

-- ── test 2: guard_data_loss = false allows progress ─────────────────────────
--
-- With guard_data_loss disabled the failover proceeds despite s2 being missing.
-- s1 (the only node in report_lsn) should be selected as the candidate and
-- transition toward prepare_promotion / stop_replication.

SET pgautofailover.guard_data_loss TO false;

SELECT pgautofailover.perform_failover('gdl_test', 0);

-- s1 should now have a goal state of prepare_promotion or stop_replication,
-- indicating the failover candidate was selected.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'gdl_test'
 ORDER BY nodename;

RESET pgautofailover.guard_data_loss;
RESET pgautofailover.startup_grace_period;

-- event summary: which MonitorFSM[] rule (if any) produced each of this
-- test's own state-change events. Exercises pgautofailover.last_events()
-- against a real scenario -- its own SELECT list didn't match
-- pgautofailover.event's column set for a long time, breaking it outright,
-- and nothing in this suite ever called it to notice (see monitor.sql's
-- own minimal-repro coverage). eventid/eventtime omitted: eventid is a
-- database-wide sequence shared by every test in this schedule (see
-- regress_schedule's own comment) and eventtime is a live timestamp --
-- neither is a stable value to pin in this file's own expected output.
SELECT reportedstate, goalstate, rule_pos, rule_section, description
  FROM pgautofailover.last_events('gdl_test', count => 100);
