# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Bug 6 investigation: pytest/monitor (PG16) test_003_002_stop_primary,
# "node3 failed to reach prepare_promotion or report_lsn after 120
# seconds" -- except the CI log this was chasing actually shows node3
# proceeding all the way to wait_primary, well past what
# number_sync_standbys=1 (minCandidates=2) should ever allow with only
# one node (node3) left able to report.
#
# Two prior attempts at this (both against a single serial session, no
# real concurrency):
#
#   - stale_primary_report.sql manufactured the frozen-primary end state
#     directly via UPDATE (goalstate='demoted', reportedstate frozen at
#     'primary'). It correctly stayed blocked -- disproving the original
#     "missingNodesCount skip-branch" hypothesis as the whole story.
#
#   - A second, more faithful attempt drove every transition through real
#     node_active() calls (crsp_p killed via backdated reporttime/health,
#     crsp_s1 promoted via the real report_lsn -> prepare_promotion ->
#     stop_replication -> wait_primary -> primary sequence, crsp_s1 then
#     killed the same way). This *also* stayed correctly blocked at
#     report_lsn once crsp_s2 confirmed report_lsn -- ruling out "raw
#     UPDATE bypassed some side effect of the real FSM path" as the
#     explanation too.
#
# What neither prior attempt touched: SetNodeHealthState() (the health
# check background worker's write path) runs in its own SPI transaction
# with no LockFormation()/LockNodeGroup() call at all -- unlike
# NodeActive(), remove_node(), and every other node-mutating entry point.
# It is completely unsynchronized with the FSM decision-making. This spec
# tests the case that opens: a dying primary's OWN keeper sending one
# last self-report ("I'm still primary") concurrently with the surviving
# standby's report, racing against each other through LockNodeGroup
# rather than against the (unlockable, unblockable) health-check writer.
#
# crsp_p is already fully retired (goalstate=reportedstate=demoted) from
# crsp_test's first "primary death", exactly mirroring node1's state by
# the time test_003_002 runs in the real test. crsp_s1 is then marked
# unhealthy (health=0, stale reporttime) -- but its own keeper still
# manages one more node_active() call reporting "primary", unaware it is
# already considered dead, concurrently with crsp_s2's routine
# "secondary" report.

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('crsp2_test', 'pgsql', 'postgres', true, 1);

    SELECT pgautofailover.register_node('crsp2_test', 'crsp2_p', 5432, 'postgres', 'crsp2_p', 1);
    SELECT pgautofailover.register_node('crsp2_test', 'crsp2_s1', 5432, 'postgres', 'crsp2_s1', 1);
    SELECT pgautofailover.register_node('crsp2_test', 'crsp2_s2', 5432, 'postgres', 'crsp2_s2', 1);

    -- bootstrap: crsp2_p=primary, crsp2_s1=secondary, crsp2_s2=secondary
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');

    -- ── generation 1: crsp2_p (node1) is hard-killed, crsp2_s1 (node2)
    -- takes over via the real report_lsn -> ... -> primary sequence ──

    SET pgautofailover.startup_grace_period = 1;

    UPDATE pgautofailover.node
       SET health = 0, healthchecktime = now(), reporttime = now() - interval '60 seconds'
     WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p';

    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'prepare_promotion', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'stop_replication', current_lsn => '0/5000');

    UPDATE pgautofailover.node
       SET statechangetime = now() - interval '31 seconds'
     WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_p';

    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'stop_replication', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
        0, current_group_role => 'primary', current_lsn => '0/5000');

    -- Confirm generation 1 landed exactly like test_003_001: crsp2_p
    -- fully retired (demoted/demoted), crsp2_s1 the real primary,
    -- crsp2_s2 the sole remaining secondary.
    -- (checked by s3_check_before below, not here, so it shows up in the
    -- .out file rather than being silently asserted in setup)

    -- ── generation 2: mark crsp2_s1 unhealthy, exactly like the health
    -- check worker would (SetNodeHealthState -- no lock taken). Its own
    -- keeper does not know yet and will send one more self-report.
    UPDATE pgautofailover.node
       SET health = 0, healthchecktime = now(), reporttime = now() - interval '60 seconds'
     WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1';
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'crsp2_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'crsp2_test';
}

session s3
step s3_check_before {
    SELECT nodename, goalstate, reportedstate, health
      FROM pgautofailover.node
     WHERE formationid = 'crsp2_test'
     ORDER BY nodename;
}

# crsp2_s1's own keeper, unaware it has just been marked unhealthy,
# sending one more self-report of the role it last knew ("primary").
# BEGIN here so its node_active() call's LockNodeGroup() is held open
# until s1_commit, giving s2_report something to block behind.
session s1
step s1_begin  { BEGIN; }
step s1_report { SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s1'),
                     0, current_group_role => 'primary', current_lsn => '0/5000'); }
step s1_commit { COMMIT; }

# crsp2_s2's routine report, concurrent with crsp2_s1's own last gasp.
# With only crsp2_s2 able to ever reach report_lsn (crsp2_p is fully
# retired, crsp2_s1 is being marked dead), number_sync_standbys=1
# (minCandidates=2) should keep this formation permanently stuck.
session s2
step s2_report { SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
                     0, current_group_role => 'secondary', current_lsn => '0/5000'); }
step s2_report_again { SELECT assigned_group_state FROM pgautofailover.node_active('crsp2_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp2_test' AND nodename = 'crsp2_s2'),
                     0, current_group_role => 'report_lsn', current_lsn => '0/5000'); }

session s4
step s4_check_after {
    SELECT nodename, goalstate, reportedstate, health
      FROM pgautofailover.node
     WHERE formationid = 'crsp2_test'
     ORDER BY nodename;
}

# s2_report starts while s1 still holds LockNodeGroup(ExclusiveLock)
# (uncommitted self-report); the isolation tester detects the block and
# only reports s2_report's result once s1_commit releases the lock.
permutation s3_check_before s1_begin s1_report s2_report s1_commit s2_report_again s4_check_after
