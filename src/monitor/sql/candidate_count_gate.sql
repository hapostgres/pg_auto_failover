-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- Regression test for ProceedGroupStateForMSFailover's candidateCount == 0
-- gate (reporting_node.ms_failover.promotion_outcome.candidate_count_gate in
-- MonitorFSM[]): the window where the primary has gone unhealthy but NOT A
-- SINGLE standby has yet reported reaching report_lsn.
--
-- Unlike the other two counting gates -- missingNodesCount (guard_data_loss.sql)
-- and quorumCandidateCount (stale_primary_report.sql), both named explicitly
-- in those files' own header comments -- no existing test exercises this one
-- by name. It has no dedicated log message either (the original hand-written
-- code silently `return`s false here, and the declarative row that now
-- matches this same condition carries no extraAction, matching that exactly)
-- so there is no pgautofailover.event row to check for it; the only
-- observable effect is what does NOT happen: neither standby should reach
-- prepare_promotion/fast_forward this round.
--
-- guard_data_loss is set to false: with the default (true), the
-- missingNodesCount > 0 gate above this one in ProceedGroupStateForMSFailover
-- would itself decline and return before ever reaching this gate, since both
-- standbys are still SECONDARY/CATCHINGUP (each counted as missing, per
-- BuildCandidateList's own fan-out branch) at the moment this test polls
-- them.
--
-- startup_grace_period is also lowered to 1, same as guard_data_loss.sql/
-- fast_forward.sql/stale_primary_report.sql: NodeIsUnhealthy() only honors a
-- BAD health reading once at least this many seconds have passed since the
-- monitor process itself started (PgStartTime), to avoid spurious failovers
-- right after the monitor restarts. The default (10s) is longer than this
-- whole schedule takes to reach this test file when run automated, which
-- would silently make p never register as unhealthy and this test's own
-- ProceedGroupStateForMSFailover call never even fire.

\x on

-- ── formation and node registration ─────────────────────────────────────────

SELECT pgautofailover.create_formation('ccg_test', 'pgsql', 'postgres', true, 1);

SELECT *
  FROM pgautofailover.register_node('ccg_test', 'ccg_p', 5432,
                                    'postgres', 'ccg_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'ccg_test' AND nodename = 'ccg_p' \gset

SELECT *
  FROM pgautofailover.register_node('ccg_test', 'ccg_s1', 5432,
                                    'postgres', 'ccg_s1', 1);

SELECT nodeid AS ns1 FROM pgautofailover.node
 WHERE formationid = 'ccg_test' AND nodename = 'ccg_s1' \gset

SELECT *
  FROM pgautofailover.register_node('ccg_test', 'ccg_s2', 5432,
                                    'postgres', 'ccg_s2', 1);

SELECT nodeid AS ns2 FROM pgautofailover.node
 WHERE formationid = 'ccg_test' AND nodename = 'ccg_s2' \gset

-- ── bootstrap: drive the FSM to primary + secondary + secondary ─────────────
-- Same sequence as guard_data_loss.sql's own bootstrap.

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'single');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns2, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns2, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns2, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- p: primary (refresh to pick up second secondary)
SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

-- Verify bootstrap: p=primary, s1=secondary, s2=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'ccg_test'
 ORDER BY nodename;

-- ── manufacture: primary unhealthy, NEITHER standby has reported yet ────────
--
-- p goes unhealthy and is demoted to draining/draining (same manufactured
-- shape as guard_data_loss.sql/stale_primary_report.sql). s1/s2 are left
-- exactly as bootstrap left them -- secondary/secondary, neither has been
-- assigned report_lsn yet -- so candidateCount is 0 for both of them: no
-- standby has reached report_lsn, the exact window this gate covers.

SET pgautofailover.startup_grace_period = 1;
SET pgautofailover.guard_data_loss TO false;

UPDATE pgautofailover.node
   SET health = 0,
       healthchecktime = now(),
       reporttime = now() - interval '60 seconds'
 WHERE formationid = 'ccg_test' AND nodename = 'ccg_p';

UPDATE pgautofailover.node
   SET goalstate = 'draining', reportedstate = 'draining'
 WHERE formationid = 'ccg_test' AND nodename = 'ccg_p';

-- Verify the manufactured state before the test call.
SELECT nodename, goalstate, reportedstate, health
  FROM pgautofailover.node
 WHERE formationid = 'ccg_test'
 ORDER BY nodename;

-- ── test: poll exactly one secondary, candidateCount == 0 for both ──────────
--
-- s1 reports secondary/0-5000 again (nothing new from its own point of
-- view). This drives ProceedGroupState(s1) -> ActionRunMultiStandbyFailover
-- Cascade -> ProceedGroupStateForMSFailover, which runs BuildCandidateList
-- over the WHOLE group (not just s1): both s1 and s2 are still SECONDARY/
-- CATCHINGUP, so BOTH get fanned out to report_lsn in this same call (the
-- fan-out rows, already covered elsewhere) -- but candidateCount is 0 (no
-- node's reportedState is report_lsn yet), so the candidate_count_gate row
-- matches and the function returns false: no candidate is selected.

SELECT assigned_group_state
  FROM pgautofailover.node_active('ccg_test', :ns1, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- s1 and s2 must both have been fanned out to report_lsn (goalstate), but
-- NEITHER may have reached prepare_promotion/fast_forward: the
-- candidate_count_gate declined before any candidate could be selected.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'ccg_test'
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
  FROM pgautofailover.last_events('ccg_test', count => 100);
