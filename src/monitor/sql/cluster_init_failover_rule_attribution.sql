-- Copyright (c) Microsoft Corporation. All rights reserved.
-- Licensed under the PostgreSQL License.
--
-- End-to-end demonstration of the rule_pos/rule_section attribution
-- mechanism (notifications.c's CurrentMonitorFSMRulePos/RuleSection,
-- InsertEvent()): registers a two-node formation, drives it through the
-- heartbeat-only bootstrap to primary + secondary, triggers a manual
-- perform_failover(), and then joins pgautofailover.event against
-- pgautofailover.fsm on rule_pos = pos to show, for every state
-- transition the monitor produced, exactly which MonitorFSM[] row was
-- selected and executed -- both for the ordinary heartbeat-driven
-- bootstrap rows (api_triggered = false in this join, since rule_pos is
-- only set for rows actually reached through the declarative dispatch
-- table) and for the operator-triggered perform_failover call itself.

\x on

-- ── formation and node registration ─────────────────────────────────────────

SELECT pgautofailover.create_formation('cifra_test', 'pgsql', 'postgres', true, 0);

SELECT *
  FROM pgautofailover.register_node('cifra_test', 'cifra_p', 5432,
                                    'postgres', 'cifra_p', 1);

SELECT nodeid AS np FROM pgautofailover.node
 WHERE formationid = 'cifra_test' AND nodename = 'cifra_p' \gset

SELECT *
  FROM pgautofailover.register_node('cifra_test', 'cifra_s', 5432,
                                    'postgres', 'cifra_s', 1);

SELECT nodeid AS ns FROM pgautofailover.node
 WHERE formationid = 'cifra_test' AND nodename = 'cifra_s' \gset

-- ── bootstrap: drive the FSM to primary + secondary ─────────────────────────
--
-- Mirrors drop_node.sql's bootstrap sequence (register -> single ->
-- wait_primary -> [standby: wait_standby -> catchingup -> secondary] ->
-- primary), including its "confirm" round-trips.

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :np, 0,
                                  current_group_role => 'single');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :ns, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :np, 0,
                                  current_group_role => 'single',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :ns, 0,
                                  current_group_role => 'wait_standby');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :ns, 0,
                                  current_group_role => 'catchingup',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :np, 0,
                                  current_group_role => 'wait_primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :np, 0,
                                  current_group_role => 'primary',
                                  current_lsn => '0/5000');

SELECT assigned_group_state
  FROM pgautofailover.node_active('cifra_test', :ns, 0,
                                  current_group_role => 'secondary',
                                  current_lsn => '0/5000');

-- Verify bootstrap: p=primary, s=secondary.
SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'cifra_test'
 ORDER BY nodename;

-- ── manual failover ──────────────────────────────────────────────────────
--
-- Two-node group: dispatches through MonitorFSM[]'s API_TRIGGERED section
-- (pos 105, "manual failover, 2-node group, primary+standby both converged
-- -> standby prepare_promotion, primary draining"), attributing both
-- resulting event rows to that one rule.

SELECT pgautofailover.perform_failover('cifra_test', 0);

SELECT nodename, goalstate, reportedstate
  FROM pgautofailover.node
 WHERE formationid = 'cifra_test'
 ORDER BY nodename;

-- ── which rule fired for which event? ───────────────────────────────────────
--
-- Every event row this formation produced, joined against the FSM table on
-- rule_pos = pos: rule_pos/rule_section are NULL for the ordinary
-- heartbeat-driven bootstrap transitions above whenever the matched row
-- happens to be identified only by array position in earlier sessions'
-- tests -- here every one of them was reached through the same declarative
-- MonitorFSM[] dispatch table, so each carries its own attribution too. The
-- final two rows (both attributed to pos 105) are the perform_failover()
-- call's own dual assignment (standby -> prepare_promotion, primary ->
-- draining), selected and executed from the API_TRIGGERED section.

SELECT e.eventid, e.nodename, e.reportedstate, e.goalstate,
       e.rule_pos, e.rule_section, f.comment AS rule_comment
  FROM pgautofailover.event e
  LEFT JOIN pgautofailover.fsm f ON f.pos = e.rule_pos
 WHERE e.formationid = 'cifra_test'
 ORDER BY e.eventid;
