-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression coverage for the LockNodeGroupAndFetch()/
-- LockNodeGroupAndFetchByName() migration.
--
-- perform_promotion, start_maintenance, stop_maintenance,
-- set_node_candidate_priority, set_node_replication_quorum, and
-- update_node_metadata each used to read a node before taking
-- LockFormation()/LockNodeGroup(), then make a decision after locking
-- using mutable fields (reportedState, goalState, candidatePriority,
-- nodeCluster, ...) of that same pre-lock struct -- the same defect shape
-- already fixed in RemoveNode() and NodeActive() (Bug C) and
-- SetNodeHealthState() (Bug 6), just not yet proven to be hit by a
-- concrete reproducer for these six.
--
-- All six were migrated to LockNodeGroupAndFetch()/
-- LockNodeGroupAndFetchByName(), which lock first and return a fresh,
-- post-lock read -- this file only has none of these functions in the
-- existing REGRESS suite before this commit (grep confirms only
-- start_maintenance had any coverage, in node_active_protocol.sql, for an
-- unrelated NodeIsHealthy regression), so this exercises the happy path
-- for each of the six against a fresh formation, proving the migration
-- didn't change observable behavior.

\x on

-- ── formation and node registration ─────────────────────────────────────────
-- 3 nodes so set_node_candidate_priority's "at least 2 non-zero candidate
-- priority nodes" bookkeeping has something real to count.

SELECT pgautofailover.create_formation('lafm_test', 'pgsql', 'postgres', true, 1);

SELECT pgautofailover.register_node('lafm_test', 'lafm_p', 5432, 'postgres', 'lafm_p', 1);
SELECT pgautofailover.register_node('lafm_test', 'lafm_s1', 5432, 'postgres', 'lafm_s1', 1);
SELECT pgautofailover.register_node('lafm_test', 'lafm_s2', 5432, 'postgres', 'lafm_s2', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'lafm_test' AND nodename = 'lafm_p' \gset
SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'lafm_test' AND nodename = 'lafm_s1' \gset
SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'lafm_test' AND nodename = 'lafm_s2' \gset

-- ── bootstrap: drive the FSM to primary + secondary + secondary ─────────────

SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'single');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'wait_standby');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'single', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'wait_primary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'wait_standby');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'catchingup', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'secondary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'wait_primary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'primary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'secondary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns2, 0,
    current_group_role => 'wait_standby');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns2, 0,
    current_group_role => 'catchingup', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns2, 0,
    current_group_role => 'secondary', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'primary', current_lsn => '0/5000');

-- lafm_p: confirm apply_settings, then re-confirm primary so it's fully
-- converged (goalstate == reportedstate == primary) before any of the
-- functions below check its state as a precondition.
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'apply_settings', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'primary', current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate, candidatepriority, replicationquorum
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test'
 ORDER BY nodename;

-- ── perform_promotion: rejecting an already-primary target ──────────────────
--
-- Exercises the post-lock IsCurrentState(currentNode, PRIMARY) check that
-- used to run against the pre-lock struct.

SELECT pgautofailover.perform_promotion('lafm_test', 'lafm_p');

-- ── set_node_candidate_priority: the "≥2 non-zero" NOTICE uses fresh counts ──
--
-- Exercises the post-lock currentNode->candidatePriority /
-- nodesGroupList-derived count that used to run against the pre-lock
-- struct and an independently-fetched (but still pre-decision) list.

-- lafm_s2 to zero: 2 non-zero candidates remain (lafm_p, lafm_s1) -- no NOTICE.
SELECT pgautofailover.set_node_candidate_priority('lafm_test', 'lafm_s2', 0);

-- lafm_s1 to zero next: only 1 non-zero candidate would remain (lafm_p) --
-- must NOTICE "no failover candidate" using the fresh (not stale) count.
SELECT pgautofailover.set_node_candidate_priority('lafm_test', 'lafm_s1', 0);

SELECT nodename, candidatepriority
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test'
 ORDER BY nodename;

-- restore non-zero priorities for the remaining tests
SELECT pgautofailover.set_node_candidate_priority('lafm_test', 'lafm_s1', 100);
SELECT pgautofailover.set_node_candidate_priority('lafm_test', 'lafm_s2', 100);

-- ── set_node_replication_quorum ──────────────────────────────────────────────
--
-- Exercises the post-lock currentNode->replicationQuorum write and the
-- FormationNumSyncStandbyIsValid() check that follows it.

SELECT pgautofailover.set_node_replication_quorum('lafm_test', 'lafm_s2', false);

SELECT nodename, replicationquorum
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test'
 ORDER BY nodename;

SELECT pgautofailover.set_node_replication_quorum('lafm_test', 'lafm_s2', true);

-- ── update_node_metadata ──────────────────────────────────────────────────────
--
-- Exercises the post-lock currentNode->nodeName/nodeHost/nodePort fallback
-- defaults that used to be read from the pre-lock struct.

SELECT pgautofailover.update_node_metadata(:ns2, 'lafm_s2_renamed', 'lafm_s2', 5432);

SELECT nodename, nodehost, nodeport
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test' AND nodeid = :ns2;

-- ── start_maintenance / stop_maintenance on a secondary ──────────────────────
--
-- start_maintenance already has coverage elsewhere (node_active_protocol.sql,
-- a different regression); stop_maintenance had none. Exercises the
-- post-lock IsCurrentState(MAINTENANCE/PREPARE_MAINTENANCE) checks in both.
--
-- The candidate-priority/replication-quorum changes above updated
-- synchronous_standby_names, so lafm_p needs to re-settle
-- (goalstate == reportedstate == primary) before start_maintenance's own
-- "is the primary in a stable state" precondition will pass.
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'apply_settings', current_lsn => '0/5000');
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :np, 0,
    current_group_role => 'primary', current_lsn => '0/5000');

SELECT pgautofailover.start_maintenance(:ns1);

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test'
 ORDER BY nodename;

-- lafm_s1's keeper confirms maintenance
SELECT assigned_group_state FROM pgautofailover.node_active('lafm_test', :ns1, 0,
    current_group_role => 'maintenance', current_lsn => '0/5000');

SELECT pgautofailover.stop_maintenance(:ns1);

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'lafm_test'
 ORDER BY nodename;

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
  FROM pgautofailover.last_events('lafm_test', count => 100);
