-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression test for issue #774: a monitor rule can assign a node a goal
-- state its own KeeperFSM[] (src/bin/pg_autoctl/fsm.c) has no transition
-- path to reach from that node's current state, fataling the keeper
-- forever ("does not know how to reach state ... from ...").
--
-- Two independent rules in group_state_machine.c interact badly:
--
--  Rule 1 (ProceedGroupStateForPrimaryNode, "no healthy standby in the
--  quorum" block): reassigns the primary to wait_primary whenever
--  secondaryQuorumNodesCount == 0. That count drops to zero the instant a
--  failover candidate leaves SECONDARY (e.g. converges to
--  prepare_promotion) -- even though the candidate *is* the failover in
--  progress, not a lost standby. Without a guard, the primary can be
--  reassigned wait_primary right as its own candidate is converging.
--
--  Rule 2 ("prepare_promotion -> stop_replication" block): assigns the
--  primary demote_timeout as soon as the candidate reports
--  prepare_promotion, unconditionally. demote_timeout has a real
--  KeeperFSM[] edge from primary/join_primary/apply_settings/draining --
--  but not from wait_primary. If Rule 1 already reassigned the primary to
--  wait_primary, Rule 2 hands out an unreachable demote_timeout, and the
--  keeper fatals on every retry.
--
-- Fixed by: Rule 1 now skips its reassignment when IsFailoverInProgress()
-- is true (the candidate already being in prepare_promotion counts);
-- Rule 2 now only assigns demote_timeout when the primary's reported
-- state is actually one KeeperFSM[] has an edge to, deferring otherwise.
--
-- Two scenarios below, each isolating one of the two guards. Neither
-- needs the isolation tester (pg_isolation_regress): both are pure
-- sequencing issues inside ProceedGroupStateFromContext, reproducible
-- deterministically with a straight-line sequence of node_active() calls
-- and (for the states that are otherwise hard to reach synchronously) a
-- direct UPDATE to manufacture the precondition -- the same technique
-- stale_primary_report.sql already uses for a similarly hard-to-reach
-- scenario (a primary that never gets to report its own demotion).

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

-- ═══════════════════════════════════════════════════════════════════════
-- Scenario B (Rule 2 / Fix 2): demote_timeout must never be assigned to a
-- primary sitting at wait_primary -- there is no KeeperFSM[] edge from
-- wait_primary to demote_timeout, so that assignment fatals the keeper
-- forever.
-- ═══════════════════════════════════════════════════════════════════════

SELECT pgautofailover.create_formation('fclmb_test', 'pgsql', 'postgres', true, 0);

SELECT *
  FROM pgautofailover.register_node('fclmb_test', 'fclmb_p', 5432,
                                    'postgres', 'fclmb_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'fclmb_test' AND nodename = 'fclmb_p' \gset

SELECT *
  FROM pgautofailover.register_node('fclmb_test', 'fclmb_s', 5432,
                                    'postgres', 'fclmb_s', 1);

SELECT nodeid AS ns FROM pgautofailover.node
 WHERE formationid = 'fclmb_test' AND nodename = 'fclmb_s' \gset

-- bootstrap: p = primary, s = secondary
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :np, 0,
                                  current_group_role => 'single');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :ns, 0,
                                  current_group_role => 'wait_standby');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :ns, 0,
                                  current_group_role => 'wait_standby');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :ns, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');
SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT nodename, reportedstate, goalstate
  FROM pgautofailover.node
 WHERE formationid = 'fclmb_test'
 ORDER BY nodename;

-- Manufacture the state Rule 1 would have produced without Fix 1 above --
-- the primary fully converged at wait_primary, whatever the exact prior
-- trigger. This isolates Rule 2's behaviour from Rule 1's.
UPDATE pgautofailover.node
   SET reportedstate = 'wait_primary',
       goalstate = 'wait_primary'
 WHERE formationid = 'fclmb_test' AND nodename = 'fclmb_p';

-- The candidate is already fully converged at prepare_promotion, same as in
-- Scenario A -- IsCurrentState() requires goalstate == reportedstate == the
-- state being tested, so a node_active() call alone would only move
-- reportedstate and leave goalstate at 'secondary' from the bootstrap above,
-- never matching Rule 2's IsCurrentState(activeNode, PREPARE_PROMOTION)
-- check. Manufacture full convergence directly, then re-affirm via
-- node_active() to trigger the dispatch.
UPDATE pgautofailover.node
   SET reportedstate = 'prepare_promotion',
       goalstate = 'prepare_promotion'
 WHERE formationid = 'fclmb_test' AND nodename = 'fclmb_s';

SELECT assigned_group_state
  FROM pgautofailover.node_active('fclmb_test', :ns, 0,
                                  current_group_role => 'prepare_promotion',
                                  current_lsn => '0/5000');

-- ASSERT: fclmb_p's goalstate is still 'wait_primary', not 'demote_timeout'.
-- Pre-fix: Rule 2 assigns demote_timeout unconditionally here, producing
-- reportedstate=wait_primary / goalstate=demote_timeout -- no KeeperFSM[]
-- edge between them, so the keeper would fatal on every retry.
SELECT nodename, reportedstate, goalstate
  FROM pgautofailover.node
 WHERE formationid = 'fclmb_test'
 ORDER BY nodename;
