-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression test for a guard_data_loss edge case: a primary that is
-- hard-killed (SIGKILL / stop_pg_autoctl, as tests/pgautofailover_utils.py
-- DataNode.fail() does) gets zero opportunity to report its own demotion.
-- Its reportedstate stays frozen at "primary" forever; only its goalstate
-- advances (draining -> demote_timeout -> demoted), driven purely by the
-- monitor's own timeout, never confirmed by the dead node itself.
--
-- This was investigated as a suspected bypass of BuildCandidateList()'s
-- missingNodesCount bookkeeping: its "skip this node, it's a primary (old
-- or new)" branch only excludes a node from the skip when its reportedState
-- (not goalState) is DRAINING/DEMOTED, so a node frozen at reportedState =
-- PRIMARY with goalState = DEMOTED matches the skip and is dropped from
-- missingNodesCount entirely -- neither a candidate nor "missing".
--
-- This test manufactures exactly that scenario for TWO consecutive dead
-- primaries (mirroring test_multi_alternate_primary_failures.py's
-- test_003_002_stop_primary, which kills node1 then node2 back-to-back)
-- and confirms the sole surviving standby (spr_s2) still correctly gets
-- stuck at report_lsn rather than being promoted: the separate
-- quorumCandidateCount < minCandidates guard (minCandidates =
-- number_sync_standbys + 1 = 2 here) catches what the missingNodesCount
-- skip-branch misses, so the end-to-end quorum-safety behavior is correct
-- even though missingNodesCount's bookkeeping is arguably imprecise for
-- this case. The PG16 CI flake this was investigating turned out to be
-- CI-runner timing (see test_003_002_stop_primary's 120s timeout bump),
-- not a promotion-safety bug -- this test is kept as permanent regression
-- coverage for the hard-killed-primary quorum edge case.

\x on

-- ── formation and node registration ─────────────────────────────────────────
-- Same 3-node/number_sync_standbys=1 shape as guard_data_loss.sql.

SELECT pgautofailover.create_formation('spr_test', 'pgsql', 'postgres', true, 1);

SELECT *
  FROM pgautofailover.register_node('spr_test', 'spr_p', 5432,
                                    'postgres', 'spr_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'spr_test' AND nodename = 'spr_p' \gset

SELECT *
  FROM pgautofailover.register_node('spr_test', 'spr_s1', 5432,
                                    'postgres', 'spr_s1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'spr_test' AND nodename = 'spr_s1' \gset

SELECT *
  FROM pgautofailover.register_node('spr_test', 'spr_s2', 5432,
                                    'postgres', 'spr_s2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'spr_test' AND nodename = 'spr_s2' \gset

-- ── bootstrap: drive the FSM to primary + secondary + secondary ─────────────
-- Identical sequence to guard_data_loss.sql's bootstrap.

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'single');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns2, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: primary (refresh to pick up second secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- Verify bootstrap: p=primary, s1=secondary, s2=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'spr_test'
 ORDER BY nodename;

-- ── generation 1: p is hard-killed, never reports its own demotion ──────────
--
-- Simulate node1.fail(): p's process is gone the instant it's killed, so
-- its reportedstate is frozen at "primary" forever -- only goalstate ever
-- advances, driven purely by the monitor's own demote-timeout logic. We
-- manufacture the end state of that timeout sequence directly instead of
-- waiting out startup_grace_period/drain timeouts.

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds',
       reportedpgisrunning = false,
       goalstate = 'demoted'
       -- reportedstate deliberately left at 'primary': p never reported
       -- draining, demote_timeout, or demoted -- it was dead before any
       -- of those could be confirmed.
 WHERE formationid = 'spr_test' AND nodename = 'spr_p';

-- s1 takes over as primary (mirrors node2 becoming primary in
-- test_003_001, and s1.reportedstate/goalstate both become primary/primary
-- for real, via a genuine node_active() report -- unlike p above).
SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns1, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate, health
  FROM pgautofailover.node
 WHERE formationid = 'spr_test'
 ORDER BY nodename;

-- ── generation 2: s1 is ALSO hard-killed the same way ───────────────────────
--
-- Simulate node2.fail(): same failure mode as p above -- reportedstate
-- frozen at "primary", only goalstate ever reaches "demoted".

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds',
       reportedpgisrunning = false,
       goalstate = 'demoted'
 WHERE formationid = 'spr_test' AND nodename = 'spr_s1';

-- s2 is the only node left alive. number_sync_standbys=1 means minCandidates
-- = 2: with BOTH p and s1 correctly counted as missing quorum reporters,
-- s2 alone must NOT be enough to proceed -- it should be assigned
-- report_lsn and get stuck there, exactly like test_003_002_stop_primary
-- expects. If p/s1 are instead silently skipped by the primary-skip
-- branch, s2 will be promoted (prepare_promotion or further) despite
-- being the only node that has ever reported an LSN.

SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate, health
  FROM pgautofailover.node
 WHERE formationid = 'spr_test'
 ORDER BY nodename;

-- s2 must still be stuck after further node_active() calls report the
-- same state again (mirrors test_003_002's second assertion after a 5s
-- sleep: node3 must not have progressed to wait_primary in the interim).
SELECT assigned_group_state
  FROM pgautofailover.node_active('spr_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'spr_test'
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
  FROM pgautofailover.last_events('spr_test', count => 100);
