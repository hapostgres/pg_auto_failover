# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Bug C, continued -- and found. concurrent_remove_standby_and_primary_report.spec
# ruled out the PRIMARY's own concurrent node_active() call as the trigger
# for the "wait_primary" symptom. This spec asks the same question about
# the node actually BEING dropped: does ITS OWN concurrent node_active()
# call -- its real keeper's regular ~1s poll, re-reporting its last known
# role, unaware a drop is in flight -- see the fresh "goalState = DROPPED"
# once it blocks behind remove_node() and resumes after that transaction
# commits?
#
# It did not, and this is the actual root cause of Bug C. NodeActive()
# (like remove_node_by_nodeid(), see concurrent_remove_node.spec) used to
# read the node via GetAutoFailoverNodeById() before acquiring its locks.
# s2's call read crss_s's row (still goalState = 'secondary') before
# blocking behind s1's LockFormation(ExclusiveLock); once s1 committed
# (crss_s.goalState now 'dropped') and s2 resumed, it kept using that
# stale, pre-lock struct for everything downstream, including the call
# into ProceedGroupState(). The "already dropped, do nothing" checks
# there compare against that same stale goalState/reportedState and
# never fired. Execution fell through to ordinary FSM logic, where
# AutoFailoverNodeGroup()'s nodesCount -- a fresh SPI query, unaffected by
# the staleness -- correctly saw a one-node group (crss_s's own row
# already excluded by its just-committed goalState = 'dropped'), and the
# nodesCount == 1 case reassigned crss_s's OWN goal to SINGLE: the
# standby's own concurrent report undid the drop it had just been given.
# This exactly reproduces the original CI log line for the node being
# dropped -- "New state for this node ...: single -> single" -- instead
# of ever reaching "dropped -> dropped", which is why "pg_autoctl drop
# node" timed out waiting for a row that could never disappear.
#
# Fixed: NodeActive() now re-reads the node after acquiring
# LockFormation()/LockNodeGroup(), the same pattern used to fix
# remove_node(). This spec now documents and guards that fixed behavior --
# crss_s stays "dropped".

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('crss_test', 'pgsql', 'postgres', true, 0);

    SELECT pgautofailover.register_node('crss_test', 'crss_p', 5432, 'postgres', 'crss_p', 1);
    SELECT pgautofailover.register_node('crss_test', 'crss_s', 5432, 'postgres', 'crss_s', 1);

    -- bootstrap to primary + secondary (same sequence as drop_node.sql)
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'crss_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'crss_test';
}

session s1
step s1_begin  { BEGIN; }
step s1_remove { SELECT pgautofailover.remove_node(
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crss_test' AND nodename = 'crss_s')); }
step s1_commit { COMMIT; }

# s2 plays the role of the standby's OWN keeper, re-reporting the role it
# last knew about ("secondary") entirely unaware that it is being dropped
# -- exactly what a real keeper does on its next ~1s poll after the drop
# command is issued but before it has processed the new goal state.
session s2
step s2_report { SELECT assigned_group_state FROM pgautofailover.node_active('crss_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crss_test' AND nodename = 'crss_s'),
                     0, current_group_role => 'secondary', current_lsn => '0/5000'); }

session s3
step s3_check  {
    SELECT nodename, goalstate, reportedstate
      FROM pgautofailover.node
     WHERE formationid = 'crss_test'
     ORDER BY nodename;
}

# s2_report starts while s1 still holds LockFormation(ExclusiveLock)
# (uncommitted remove_node); the isolation tester detects the block and
# only reports s2_report's result once s1_commit releases the lock.
permutation s1_begin s1_remove s2_report s1_commit s3_check
