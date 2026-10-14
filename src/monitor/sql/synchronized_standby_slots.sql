-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression test for pgautofailover.synchronized_standby_slots(), the
-- Postgres 17+ CDC-continuity companion to synchronous_standby_names: it
-- lists the physical replication slot names of the group's current
-- failover candidates (AutoFailoverNode.candidatePriority > 0) that are
-- also currently reporting themselves as secondary (reportedState), so
-- that a failover-enabled logical replication slot only advances once
-- every node that might get promoted right now has replayed that far --
-- excluding candidates that are lagging or otherwise not caught up.
--
-- Same bootstrap sequence as candidate_count_gate.sql: drive the FSM to
-- primary + secondary + secondary through pgautofailover.node_active().
--
-- Unlike candidate_count_gate.sql, register_node()'s result is deliberately
-- NOT printed with "SELECT *": assigned_node_id is a raw sequence value
-- that depends on how many nodes every earlier test in regress_schedule
-- has already registered, which would make this file's expected output
-- fragile to the schedule's exact composition. Only assigned_group_state,
-- which is what this test actually cares about, is printed; node ids are
-- captured silently via \gset for use in node_active() below.

\x on

-- ── formation and node registration ─────────────────────────────────────────

SELECT pgautofailover.create_formation('sss_test', 'pgsql', 'postgres', true, 1);

SELECT assigned_group_state
  FROM pgautofailover.register_node('sss_test', 'sss_p', 5432,
                                    'postgres', 'sss_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'sss_test' AND nodename = 'sss_p' \gset

SELECT assigned_group_state
  FROM pgautofailover.register_node('sss_test', 'sss_s1', 5432,
                                    'postgres', 'sss_s1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'sss_test' AND nodename = 'sss_s1' \gset

SELECT assigned_group_state
  FROM pgautofailover.register_node('sss_test', 'sss_s2', 5432,
                                    'postgres', 'sss_s2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'sss_test' AND nodename = 'sss_s2' \gset

-- ── bootstrap: drive the FSM to primary + secondary + secondary ─────────────

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'single');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns1, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns2, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: primary (refresh to pick up second secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- Verify bootstrap: p=primary, s1=secondary, s2=secondary.
SELECT nodename, goalstate, reportedstate, candidatepriority
  FROM pgautofailover.node
 WHERE formationid = 'sss_test'
 ORDER BY nodename;

-- ── both standbys are candidates: both slot names, in candidatePriority order

SELECT pgautofailover.synchronized_standby_slots('sss_test', 0);

-- ── s2 self-reports lagging (catchingup): it drops out despite still being
--    a failover candidate -- this is the lag opt-out: holding the logical
--    slot's progress hostage to a standby that isn't caught up right now
--    would defeat the point of CDC continuity. Note goalstate for s2 was
--    already catchingup before this (an extra standby beyond what
--    number_sync_standbys needs keeps that goal for quorum bookkeeping);
--    what flips here is reportedstate, which is what we actually filter on.

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'sss_test' AND nodename = 'sss_s2';

SELECT pgautofailover.synchronized_standby_slots('sss_test', 0);

-- ── s2 catches back up and reports secondary again: it's back ─────────────

SELECT assigned_group_state
  FROM pgautofailover.node_active('sss_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT pgautofailover.synchronized_standby_slots('sss_test', 0);

-- ── s2 opts out of the failover quorum: only s1's slot remains ─────────────

SELECT pgautofailover.set_node_candidate_priority('sss_test', 'sss_s2', 0);

SELECT pgautofailover.synchronized_standby_slots('sss_test', 0);

-- ── no candidates left at all: empty string disables the feature ───────────

SELECT pgautofailover.set_node_candidate_priority('sss_test', 'sss_s1', 0);

SELECT pgautofailover.synchronized_standby_slots('sss_test', 0);

-- ── a single-node group has nothing to synchronize against ─────────────────

SELECT pgautofailover.create_formation('sss_single', 'pgsql', 'postgres', true, 1);

SELECT assigned_group_state
  FROM pgautofailover.register_node('sss_single', 'sss_only', 5432,
                                    'postgres', 'sss_only', 1);

SELECT pgautofailover.synchronized_standby_slots('sss_single', 0);

-- ── an empty group errors out, same as synchronous_standby_names ──────────

SELECT pgautofailover.synchronized_standby_slots('sss_test', 42);
