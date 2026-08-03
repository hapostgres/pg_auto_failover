-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression test for issue #774: a monitor rule can assign a node a goal
-- state its own KeeperFSM[] (src/bin/pg_autoctl/fsm.c) has no transition
-- path to reach from that node's current state.
--
-- Root cause: two independent rules in group_state_machine.c could race.
-- Rule 1 (ProceedGroupStateForPrimaryNode, "no healthy standby in the
-- quorum" block) reassigns the primary to wait_primary whenever
-- secondaryQuorumNodesCount == 0. That count drops to zero the instant a
-- failover candidate leaves SECONDARY (e.g. converges to prepare_promotion)
-- -- even though the candidate *is* the failover in progress, not a lost
-- standby. Without a guard, the primary could be reassigned wait_primary
-- right as its own candidate is converging, landing it on wait_primary
-- right when a later rule still expects to find it in draining and assigns
-- demote_timeout -- an assignment with no KeeperFSM[] edge from
-- wait_primary, which the keeper cannot execute.
--
-- Fixed by: Rule 1 now skips its reassignment when IsFailoverInProgress()
-- is true (the candidate already being in prepare_promotion counts). This
-- prevents the race from happening in the first place, matching the root
-- cause described by the original issue reporter.
--
-- Note: if a live primary is nonetheless ever handed an unreachable goal
-- state by some other path, the keeper is expected to loudly refuse (fatal)
-- rather than silently proceed -- that is a deliberate invariant, not a bug
-- to be softened. The monitor's own drain-timeout mechanism
-- (NodeIsDrainTimeExpired, purely time-based) already reconciles a primary
-- stuck unresponsive at wait_primary by reassigning it to demoted (a state
-- genuinely reachable from wait_primary), independently of this fix.
--
-- This scenario isolates Rule 1's guard. It doesn't need the isolation
-- tester (pg_isolation_regress): it's a pure sequencing issue inside
-- ProceedGroupStateFromContext, reproducible deterministically with a
-- straight-line sequence of node_active() calls and a direct UPDATE to
-- manufacture the otherwise hard-to-reach precondition -- the same
-- technique stale_primary_report.sql already uses for a similarly
-- hard-to-reach scenario (a primary that never gets to report its own
-- demotion).

\x on

-- ═══════════════════════════════════════════════════════════════════════
-- Scenario A (Rule 1 / Fix 1): a candidate already mid-promotion must not
-- be treated as "the standby is just gone" by the primary's own routine
-- report.
-- ═══════════════════════════════════════════════════════════════════════

SELECT pgautofailover.create_formation('fclma_test', 'pgsql', 'postgres', true, 0);

SELECT *
  FROM pgautofailover.register_node('fclma_test', 'fclma_p', 5432,
                                    'postgres', 'fclma_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'fclma_test' AND nodename = 'fclma_p' \gset

SELECT *
  FROM pgautofailover.register_node('fclma_test', 'fclma_s', 5432,
                                    'postgres', 'fclma_s', 1);

SELECT nodeid AS ns FROM pgautofailover.node
 WHERE formationid = 'fclma_test' AND nodename = 'fclma_s' \gset

-- bootstrap: p = primary, s = secondary
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'single');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :ns, 0,
                                  current_group_role => 'wait_standby');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :ns, 0,
                                  current_group_role => 'wait_standby');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :ns, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT nodename, reportedstate, goalstate
  FROM pgautofailover.node
 WHERE formationid = 'fclma_test'
 ORDER BY nodename;

-- The candidate is already fully converged at prepare_promotion (a
-- promotion is genuinely in progress -- this is what IsFailoverInProgress()
-- must recognize). This can be reached for real via perform_promotion() or
-- a multi-standby perform_failover(); manufactured directly here to isolate
-- Rule 1 from everything else.
UPDATE pgautofailover.node
   SET reportedstate = 'prepare_promotion',
       goalstate = 'prepare_promotion'
 WHERE formationid = 'fclma_test' AND nodename = 'fclma_s';

-- The primary's own next routine report must NOT be reassigned
-- wait_primary just because its one standby is no longer "secondary" --
-- that standby is the active failover candidate, not a lost node.
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclma_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- ASSERT: fclma_p's goalstate is still 'primary'.
-- Pre-fix: Rule 1 reassigns it to 'wait_primary' here.
SELECT nodename, reportedstate, goalstate
  FROM pgautofailover.node
 WHERE formationid = 'fclma_test'
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
  FROM pgautofailover.last_events('fclma_test', count => 100);
