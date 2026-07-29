-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for pgautofailover.remove_node() dropping a secondary
-- from a two-node group, leaving the primary alone.
--
-- Background (see citus_nonha_operation.pgaf / test_008_remove_old_primaries):
-- occasionally, after a real pg_autoctl deployment drops the last standby of
-- a two-node group, the primary is observed stuck at "wait_primary" instead
-- of moving straight to "single", and "pg_autoctl drop node" times out after
-- 60s waiting for the standby's row to disappear. This file checks the
-- monitor-side half of that sequence directly via SQL, bypassing the actual
-- keeper processes, to determine whether the monitor's own bookkeeping
-- (AutoFailoverNodeGroup's "goalstate <> 'dropped'" filter, RemoveNode()'s
-- inline ProceedGroupState(primaryNode) call, and the two-step
-- goalstate=dropped / reportedstate=dropped removal protocol) accounts for
-- the observed symptom, or whether it must originate elsewhere (e.g. the
-- keeper's own FSM not honoring a pending "dropped" goal, which this file
-- cannot exercise since it never runs the real pg_autoctl keeper).

\x on

-- ── formation and node registration ─────────────────────────────────────────

SELECT pgautofailover.create_formation('dn_test', 'pgsql', 'postgres', true, 0);

SELECT *
  FROM pgautofailover.register_node('dn_test', 'dn_p', 5432,
                                    'postgres', 'dn_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'dn_test' AND nodename = 'dn_p' \gset

SELECT *
  FROM pgautofailover.register_node('dn_test', 'dn_s', 5432,
                                    'postgres', 'dn_s', 1);

SELECT nodeid AS ns FROM pgautofailover.node
 WHERE formationid = 'dn_test' AND nodename = 'dn_s' \gset

-- ── bootstrap: drive the FSM to primary + secondary ─────────────────────────
--
-- Mirrors guard_data_loss.sql's bootstrap sequence (register -> single ->
-- wait_primary -> [standby: wait_standby -> catchingup -> secondary] ->
-- primary), including its "confirm" round-trips: the monitor only advances a
-- node past wait_primary/secondary once it sees that same reported role
-- confirmed on a second call.

-- p: single (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :np, 0,
                                  current_group_role => 'single');

-- s: wait_standby (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'wait_standby');

-- p: single → wait_primary
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

-- p: wait_primary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- s: wait_standby → catchingup
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'wait_standby');

-- s: catchingup → secondary
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

-- s: secondary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: wait_primary → primary (now that s is secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

-- p: primary (confirm)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- s: secondary (confirm after primary confirmed)
SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- Verify bootstrap: p=primary, s=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'dn_test'
 ORDER BY nodename;

-- ── test 1: remove_node(s) sets s to "dropped" and moves p straight to
--            "single" in the SAME call, without an intermediate
--            "wait_primary" -────────────────────────────────────────────────
--
-- RemoveNode() sets s's goal to DROPPED and then calls ProceedGroupState(p)
-- inline, in the same transaction. AutoFailoverNodeGroup() filters out rows
-- with goalstate = 'dropped', so at the point ProceedGroupState(p) runs the
-- group should already look like a 1-node group to it, and p should be
-- assigned "single" directly -- not "wait_primary" (the path taken when a
-- standby is merely unhealthy/missing, not being deliberately removed).

SELECT pgautofailover.remove_node(:ns);

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'dn_test'
 ORDER BY nodename;

-- ── test 2: a stale/racing node_active() report from the about-to-be-dropped
--            node does not resurrect it or corrupt the primary's state ──────
--
-- Simulate a keeper report that raced the drop and still claims a normal
-- replication role (as if the "drop" goal hadn't been observed yet). The
-- early-return in ProceedGroupState() for goalState = DROPPED means no new
-- goal should be assigned to s, but reportedstate is updated unconditionally
-- as part of node_active()'s bookkeeping -- this is the exact shape of the
-- "New state for this node ... : single -> single" symptom seen in CI, where
-- the about-to-be-dropped node reports something other than "dropped" and
-- the row is left stranded until it eventually complies.

SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'dn_test'
 ORDER BY nodename;

-- remove_node() called again while s still hasn't complied must be a safe
-- no-op (idempotent): s's goal is already 'dropped', RemoveNode() returns
-- early without re-running ProceedGroupState(p) or re-adjusting
-- number_sync_standbys a second time.
SELECT pgautofailover.remove_node(:ns);

SELECT formationid, number_sync_standbys
  FROM pgautofailover.formation
 WHERE formationid = 'dn_test';

-- ── test 3: once s reports "dropped", its row is actually removed and p is
--            left alone, unaffected ───────────────────────────────────────

SELECT assigned_group_state
  FROM pgautofailover.node_active('dn_test', :ns, 0,
                                  current_group_role => 'dropped');

-- s's row is gone; only p remains, still "single".
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'dn_test'
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
  FROM pgautofailover.last_events('dn_test', count => 100);
