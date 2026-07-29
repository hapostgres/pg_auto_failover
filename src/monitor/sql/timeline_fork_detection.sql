-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression tests for timeline-fork detection: node_timeline_history,
-- report_timeline_history(), accept_timeline(), resolve_accepted_timeline(),
-- node_timeline_status(), and the election's ancestry filter
-- (FilterNodesByTimelineAncestry(), called from ProceedGroupStateForMSFailover).

\x on

-- ── Part A: table + function unit tests ─────────────────────────────────────
--
-- Three nodes are enough to exercise report_timeline_history(),
-- node_timeline_status(), accept_timeline(), and resolve_accepted_timeline()
-- directly, without driving the full FSM: reportedtli/reportedlsn are set by
-- hand, the same way other regress tests here manufacture node state
-- (see guard_data_loss.sql).

SELECT pgautofailover.create_formation('tlf_unit', 'pgsql', 'postgres', true, 1);

SELECT *
  FROM pgautofailover.register_node('tlf_unit', 'tlfu-p', 5432,
                                    'postgres', 'p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'tlf_unit' AND nodename = 'p' \gset

SELECT *
  FROM pgautofailover.register_node('tlf_unit', 'tlfu-s1', 5432,
                                    'postgres', 's1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'tlf_unit' AND nodename = 's1' \gset

SELECT *
  FROM pgautofailover.register_node('tlf_unit', 'tlfu-s2', 5432,
                                    'postgres', 's2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'tlf_unit' AND nodename = 's2' \gset

-- p stayed on the original timeline (tli=1). s1 was promoted onto tli=2 at
-- some point in the past. s2 was promoted onto tli=3 -- a sibling branch of
-- s1's tli=2, both children of tli=1 (a genuine fork: two nodes each think
-- they are the continuation).

SELECT pgautofailover.report_timeline_history(:np,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"}]'::jsonb);

SELECT pgautofailover.report_timeline_history(:ns1,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"},
    {"tli":2,"parenttli":1,"switchpoint":"0/6000"}]'::jsonb);

SELECT pgautofailover.report_timeline_history(:ns2,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"},
    {"tli":3,"parenttli":1,"switchpoint":"0/6000"}]'::jsonb);

-- report_timeline_history() must be idempotent: re-reporting the exact same
-- facts (or a subset of them) must not create duplicate or conflicting rows.
SELECT pgautofailover.report_timeline_history(:ns1,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"}]'::jsonb);

SELECT nodeid, tli, parenttli, switchpoint_lsn
  FROM pgautofailover.node_timeline_history
 ORDER BY nodeid, tli;

UPDATE pgautofailover.node SET reportedtli = 1, reportedlsn = '0/5500'
 WHERE nodeid = :np;
UPDATE pgautofailover.node SET reportedtli = 2, reportedlsn = '0/6500'
 WHERE nodeid = :ns1;
UPDATE pgautofailover.node SET reportedtli = 3, reportedlsn = '0/7500'
 WHERE nodeid = :ns2;

-- No operator pin yet: reference_tli defaults to the highest reportedtli in
-- the group (3, from s2). s1 (tli=2) is a sibling of s2's branch, not an
-- ancestor of it -- on_accepted_lineage must be false. p (tli=1) and s2
-- (tli=3, the reference itself) must both be true.
SELECT node_name, tli, reference_tli, on_accepted_lineage
  FROM pgautofailover.node_timeline_status('tlf_unit', 0)
 ORDER BY node_name;

-- accept_timeline() must refuse to pin a timeline nobody in the group has
-- ever reported.
SELECT pgautofailover.accept_timeline('tlf_unit', 0, 99, 'operator typo');

-- Pin tli=2 (s1's branch) as ground truth instead of the auto-detected tli=3.
SELECT pgautofailover.accept_timeline('tlf_unit', 0, 2, 'operator override');

-- The pin flips the reference: now s1 (tli=2) is on_accepted_lineage, and s2
-- (tli=3, the sibling the operator rejected) is not. p (tli=1) is still an
-- ancestor of tli=2, so it stays true.
SELECT node_name, tli, reference_tli, on_accepted_lineage
  FROM pgautofailover.node_timeline_status('tlf_unit', 0)
 ORDER BY node_name;

SELECT resolve_accepted_timeline
  FROM pgautofailover.resolve_accepted_timeline('tlf_unit', 0);

SELECT formationid, groupid, accepted_tli, decided_by, resolved_at IS NOT NULL AS resolved
  FROM pgautofailover.accepted_timeline
 WHERE formationid = 'tlf_unit' AND groupid = 0;

-- Once resolved, the pin no longer applies: reference_tli reverts to the
-- auto-detected max (3), same as before the pin was ever set.
SELECT node_name, tli, reference_tli, on_accepted_lineage
  FROM pgautofailover.node_timeline_status('tlf_unit', 0)
 ORDER BY node_name;


-- ── Part B: election-level test ──────────────────────────────────────────────
--
-- Drive a real 4-node group (primary + 3 standbys) through the FSM, then
-- kill the primary and simulate two standbys that have each, at some earlier
-- point, been promoted onto their own diverging branch -- a genuine fork.
-- The operator pins one branch as ground truth *before* calling
-- perform_failover(); the election must pick the candidate on the pinned
-- lineage and exclude the other, even though the excluded one has a more
-- advanced (higher) timeline number.

SELECT pgautofailover.create_formation('tlf_election', 'pgsql', 'postgres', true, 1);

SELECT *
  FROM pgautofailover.register_node('tlf_election', 'tlfe-p', 5432,
                                    'postgres', 'p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'tlf_election' AND nodename = 'p' \gset

SELECT *
  FROM pgautofailover.register_node('tlf_election', 'tlfe-s1', 5432,
                                    'postgres', 's1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'tlf_election' AND nodename = 's1' \gset

SELECT *
  FROM pgautofailover.register_node('tlf_election', 'tlfe-s2', 5432,
                                    'postgres', 's2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'tlf_election' AND nodename = 's2' \gset

SELECT *
  FROM pgautofailover.register_node('tlf_election', 'tlfe-s3', 5432,
                                    'postgres', 's3', 1);

SELECT nodeid AS ns3 FROM pgautofailover.node
 WHERE formationid = 'tlf_election' AND nodename = 's3' \gset

-- ── bootstrap: p primary, s1/s2/s3 secondary ────────────────────────────────

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'single');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns1, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns2, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns3, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns3, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :ns3, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('tlf_election', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- Verify final formation state: p=primary, s1/s2/s3=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'tlf_election'
 ORDER BY nodename;

-- ── publish two diverging branches ──────────────────────────────────────────
--
-- s2 was, at some earlier point, promoted onto tli=2. s3 was promoted onto
-- tli=3, a sibling branch, both children of tli=1. s1 stayed on the original
-- tli=1.

SELECT pgautofailover.report_timeline_history(:ns2,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"},
    {"tli":2,"parenttli":1,"switchpoint":"0/6000"}]'::jsonb);

SELECT pgautofailover.report_timeline_history(:ns3,
  '[{"tli":1,"parenttli":0,"switchpoint":"0/0"},
    {"tli":3,"parenttli":1,"switchpoint":"0/6000"}]'::jsonb);

UPDATE pgautofailover.node SET reportedtli = 2, reportedlsn = '0/6500'
 WHERE nodeid = :ns2;
UPDATE pgautofailover.node SET reportedtli = 3, reportedlsn = '0/7500'
 WHERE nodeid = :ns3;

-- ── operator pins s2's branch (tli=2) as ground truth ───────────────────────
--
-- Left to auto-detection this group would pick tli=3 (the highest reported
-- tli, s3's branch). The operator instead pins tli=2: the election must
-- honor that and pick s2, excluding s3 even though s3's tli number is
-- higher.

SELECT pgautofailover.accept_timeline('tlf_election', 0, 2, 'operator override');

-- ── simulate primary death, standbys already at report_lsn ─────────────────

SET pgautofailover.startup_grace_period = 1;

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds'
 WHERE formationid = 'tlf_election' AND nodename = 'p';

UPDATE pgautofailover.node
   SET goalstate = 'draining', reportedstate = 'draining'
 WHERE formationid = 'tlf_election' AND nodename = 'p';

UPDATE pgautofailover.node
   SET goalstate = 'report_lsn', reportedstate = 'report_lsn'
 WHERE formationid = 'tlf_election' AND nodename IN ('s1', 's2', 's3');

SELECT nodename, goalstate, reportedstate, reportedtli, reportedlsn
  FROM pgautofailover.node
 WHERE formationid = 'tlf_election'
 ORDER BY nodename;

-- The dead primary itself always counts as a "missing" quorum node (it
-- can never report_lsn), which trips guard_data_loss regardless of the
-- ancestry filter under test here; disable it, same as guard_data_loss.sql
-- does for its own "proceed despite a missing report" scenario.
SET pgautofailover.guard_data_loss TO false;

SELECT pgautofailover.perform_failover('tlf_election', 0);

-- s2 (the pinned lineage, tli=2) must be the promoted candidate. s3 (tli=3,
-- the higher-numbered sibling branch) must be excluded and left untouched
-- in report_lsn. s1 (tli=1, an ancestor of the pinned tli=2) remains a
-- comparable standby but was less advanced than s2, so it is not selected
-- either.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'tlf_election'
 ORDER BY nodename;

-- PromoteSelectedNode() must auto-resolve the operator's pin once a primary
-- has been promoted on the accepted lineage.
SELECT formationid, groupid, accepted_tli, resolved_at IS NOT NULL AS resolved
  FROM pgautofailover.accepted_timeline
 WHERE formationid = 'tlf_election' AND groupid = 0;

RESET pgautofailover.guard_data_loss;
RESET pgautofailover.startup_grace_period;

-- event summary: which MonitorFSM[] rule (if any) produced each of this
-- test's own state-change events, for each of the two formations used
-- above. Exercises pgautofailover.last_events() against a real scenario --
-- its own SELECT list didn't match pgautofailover.event's column set for a
-- long time, breaking it outright, and nothing in this suite ever called
-- it to notice (see monitor.sql's own minimal-repro coverage). eventid/
-- eventtime omitted: eventid is a database-wide sequence shared by every
-- test in this schedule (see regress_schedule's own comment) and
-- eventtime is a live timestamp -- neither is a stable value to pin here.
SELECT reportedstate, goalstate, rule_pos, rule_section, description
  FROM pgautofailover.last_events('tlf_unit', count => 100);

SELECT reportedstate, goalstate, rule_pos, rule_section, description
  FROM pgautofailover.last_events('tlf_election', count => 100);
