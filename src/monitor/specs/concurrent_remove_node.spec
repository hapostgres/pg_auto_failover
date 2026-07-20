# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Classic concurrency test #1: two sessions call pgautofailover.remove_node()
# on the same standby at the same time. remove_node() is meant to be
# idempotent (see drop_node.sql's sequential idempotency check via
# "goalState == DROPPED -> return true early"), but that only proves it's
# safe to call twice in a row on ONE connection, where the second call sees
# the first call's committed effects. Genuine concurrency is different, and
# this test demonstrates a real bug: remove_node_by_nodeid() calls
# GetAutoFailoverNodeById() to read the target node BEFORE calling
# RemoveNode(), which is where LockFormation(ExclusiveLock) is acquired.
# s2's call reads the standby's row (still goalState = 'secondary', not yet
# 'dropped') before blocking on s1's lock, so once s1 commits and s2's call
# resumes, it is working from a stale snapshot that predates s1's changes:
# the idempotency check ("goalState == DROPPED -> return early") never
# fires, and s2 re-runs the entire removal a second time. The standby side
# is a harmless no-op re-write, but the primary side is not: RemoveNode()'s
# fallback "if ProceedGroupState(primary) didn't change its goal, force
# APPLY_SETTINGS" fires on this second, redundant call (the primary is
# already correctly "single" by then, so its goal genuinely doesn't change
# on the re-run) and pushes a primary that had already settled at "single"
# into "apply_settings" for no reason. See expected/concurrent_remove_node.out
# for the captured, currently-uncorrected behavior; a fix would move the
# GetAutoFailoverNodeById() lookup to happen after LockFormation is
# acquired (i.e. inside RemoveNode(), given a nodeId instead of a resolved
# AutoFailoverNode*), so a blocked second caller re-reads fresh,
# post-commit state once unblocked instead of reusing what it read before
# blocking.

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('crn_test', 'pgsql', 'postgres', true, 0);

    SELECT pgautofailover.register_node('crn_test', 'crn_p', 5432, 'postgres', 'crn_p', 1);
    SELECT pgautofailover.register_node('crn_test', 'crn_s', 5432, 'postgres', 'crn_s', 1);

    -- bootstrap to primary + secondary (same sequence as drop_node.sql)
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_s'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_s'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crn_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crn_test' AND nodename = 'crn_s'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'crn_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'crn_test';
}

session s1
step s1_begin  { BEGIN; }
step s1_remove { SELECT pgautofailover.remove_node(
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crn_test' AND nodename = 'crn_s')); }
step s1_commit { COMMIT; }

session s2
step s2_remove { SELECT pgautofailover.remove_node(
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crn_test' AND nodename = 'crn_s')); }

session s3
step s3_check  {
    SELECT nodename, goalstate, reportedstate
      FROM pgautofailover.node
     WHERE formationid = 'crn_test'
     ORDER BY nodename;

    SELECT number_sync_standbys
      FROM pgautofailover.formation
     WHERE formationid = 'crn_test';
}

# s1 holds the formation lock across its remove_node() call (uncommitted);
# s2's remove_node() on the same node must block behind it, not run
# interleaved, and must be a safe no-op once it does proceed.
permutation s1_begin s1_remove s2_remove s1_commit s3_check
