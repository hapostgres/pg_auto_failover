# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Bug C: citus_nonha_operation.pgaf's test_008_remove_old_primaries
# intermittently (about 15% of local Docker reproductions this session,
# see debug_citus_worker_switchover.pgaf-adjacent investigation and
# drop_node.sql's header) leaves the surviving primary of a two-node group
# stuck at "wait_primary" instead of "single" after the standby is
# dropped, and "pg_autoctl drop node" times out waiting for the standby's
# row to disappear.
#
# drop_node.sql already proved that RemoveNode()'s OWN sequencing is
# correct: SetNodeGoalState(standby, DROPPED) is visible to its own inline
# ProceedGroupState(primary) call in the same transaction, which assigns
# "single" directly. concurrent_remove_node.spec then showed that
# GetAutoFailoverNodeById() runs before LockFormation() is acquired,
# causing a second concurrent remove_node() call on the SAME node to work
# from stale pre-lock data once it unblocks.
#
# This spec asks the same question about the PRIMARY's own path: does the
# primary's own node_active() call -- exactly what its real keeper sends
# on every ~1s poll, entirely independent of the drop -- see fresh,
# post-drop state if it happens to be blocked behind a concurrent
# remove_node() and only resumes after that transaction commits? NodeActive()
# (node_active_protocol.c) also calls GetAutoFailoverNodeById() before its
# own LockFormation()/LockNodeGroup() calls, which is the same category of
# pre-lock read as concurrent_remove_node.spec exercised on the drop side.
#
# Result: this specific mechanism is NOT the cause of the "wait_primary"
# symptom -- it's ruled out by this test. The primary's own node_active()
# call correctly returns "single" even after blocking behind the drop and
# resuming post-commit, because ProceedGroupState()'s nodesCount (via
# AutoFailoverNodeGroup(), a fresh SPI query) is computed AFTER the lock is
# acquired, not from the stale pre-lock GetAutoFailoverNodeById() read: the
# staleness at line ~420 only affects the calling node's own previously-known
# fields, not the group-membership count the "single" decision is actually
# based on.
#
# Bug C's actual trigger turned out to be the same class of bug, on the
# OTHER node: concurrent_remove_standby_and_standby_report.spec shows the
# STANDBY's own concurrent node_active() call -- not the primary's --
# racing its own removal the same way, and there the staleness does
# matter: the standby's own goalState/reportedState fields (stale, not
# the fresh nodesCount) are what ProceedGroupState()'s "already dropped,
# do nothing" checks compare against, so they never fire for the standby
# reporting on itself, and it falls through to the nodesCount == 1 branch
# which reassigns its OWN goal to SINGLE -- exactly the original CI log
# line for the dropped node, "New state for this node ...: single ->
# single", instead of ever reaching "dropped -> dropped". Fixed in
# NodeActive() alongside this spec's sibling.

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('crsp_test', 'pgsql', 'postgres', true, 0);

    SELECT pgautofailover.register_node('crsp_test', 'crsp_p', 5432, 'postgres', 'crsp_p', 1);
    SELECT pgautofailover.register_node('crsp_test', 'crsp_s', 5432, 'postgres', 'crsp_s', 1);

    -- bootstrap to primary + secondary (same sequence as drop_node.sql)
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_s'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp_test' AND nodename = 'crsp_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'crsp_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'crsp_test';
}

session s1
step s1_begin  { BEGIN; }
step s1_remove { SELECT pgautofailover.remove_node(
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp_test' AND nodename = 'crsp_s')); }
step s1_commit { COMMIT; }

# s2 plays the role of the primary's own keeper, polling node_active() and
# re-reporting the role it last knew about ("primary"), entirely unaware
# that a drop is in flight -- exactly what a real keeper does once a
# second before and after the drop, since it has no way to know one is
# happening concurrently.
session s2
step s2_report { SELECT assigned_group_state FROM pgautofailover.node_active('crsp_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp_test' AND nodename = 'crsp_p'),
                     0, current_group_role => 'primary', current_lsn => '0/5000'); }

session s3
step s3_check  {
    SELECT nodename, goalstate, reportedstate
      FROM pgautofailover.node
     WHERE formationid = 'crsp_test'
     ORDER BY nodename;
}

# s2_report starts while s1 still holds LockFormation(ExclusiveLock)
# (uncommitted remove_node); the isolation tester detects the block and
# only reports s2_report's result once s1_commit releases the lock.
permutation s1_begin s1_remove s2_report s1_commit s3_check
