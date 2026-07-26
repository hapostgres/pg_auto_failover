# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Proves the LockNodeGroupAndFetch()/LockNodeGroupAndFetchByName() fix for
# set_node_replication_quorum(): its write path calls
# ReportAutoFailoverNodeReplicationSetting(nodeId, nodeHost, nodePort,
# currentNode->candidatePriority, currentNode->replicationQuorum) -- writing
# BOTH settings back together, using whatever candidatePriority the
# in-memory currentNode struct happens to hold.
#
# Before the migration, currentNode was read via GetAutoFailoverNodeByName()
# *before* LockFormation()/LockNodeGroup() were acquired. If a concurrent
# set_node_candidate_priority() call for the SAME node committed a new
# candidatePriority while this call was blocked waiting for the lock, the
# struct read before the block was stale by exactly that transaction --
# and the write above would silently reassert the pre-lock candidatePriority,
# clobbering the concurrent change. A genuine lost update, not just a
# theoretical staleness window: the reverted value is what ends up
# persisted, with no error or warning to say so.
#
# With the fix, LockNodeGroupAndFetchByName() re-reads the node after the
# lock is acquired, so candidatePriority reflects the concurrent commit
# by the time it's written back.

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('ccpq_test', 'pgsql', 'postgres', true, 1);

    SELECT pgautofailover.register_node('ccpq_test', 'ccpq_p', 5432, 'postgres', 'ccpq_p', 1);
    SELECT pgautofailover.register_node('ccpq_test', 'ccpq_s1', 5432, 'postgres', 'ccpq_s1', 1);
    SELECT pgautofailover.register_node('ccpq_test', 'ccpq_s2', 5432, 'postgres', 'ccpq_s2', 1);

    -- bootstrap: ccpq_p=primary, ccpq_s1=secondary, ccpq_s2=secondary
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s1'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s2'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s2'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_s2'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('ccpq_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'ccpq_test' AND nodename = 'ccpq_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'ccpq_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'ccpq_test';
}

session s3
step s3_check_before {
    SELECT nodename, candidatepriority, replicationquorum
      FROM pgautofailover.node
     WHERE formationid = 'ccpq_test'
     ORDER BY nodename;
}

# Sets ccpq_s2's candidate priority to 0. BEGIN here so LockNodeGroup()
# stays held open until cp_commit, giving rq_set something to provably
# block behind.
session cp
step cp_begin  { BEGIN; }
step cp_set    { SELECT pgautofailover.set_node_candidate_priority('ccpq_test', 'ccpq_s2', 0); }
step cp_commit { COMMIT; }

# Re-asserts ccpq_s2's replication quorum as true (its existing value),
# racing cp's candidate priority change on the SAME node. Setting it to
# the same value it already has -- rather than flipping it to false --
# keeps this scoped to exactly the vulnerable write path
# (ReportAutoFailoverNodeReplicationSetting writing back
# currentNode->candidatePriority alongside the new replicationQuorum)
# without also tripping FormationNumSyncStandbyIsValid()'s unrelated
# "enough standbys left" check, which only runs when disabling quorum
# participation. Before the fix, this would read ccpq_s2's pre-lock
# candidatePriority (100, before cp's commit) and write it straight back
# once unblocked -- silently reverting cp's change.
session rq
step rq_set { SELECT pgautofailover.set_node_replication_quorum('ccpq_test', 'ccpq_s2', true); }

session s4
step s4_check_after {
    SELECT nodename, candidatepriority, replicationquorum
      FROM pgautofailover.node
     WHERE formationid = 'ccpq_test'
     ORDER BY nodename;
}

# rq_set starts while cp still holds LockNodeGroup(ExclusiveLock)
# (uncommitted candidate-priority change); the isolation tester detects
# the block and only reports rq_set's result once cp_commit releases the
# lock. ccpq_s2 must end up with candidatepriority=0 (cp's change): rq_set
# re-asserting replicationquorum=true must not revert it back to 100.
permutation s3_check_before cp_begin cp_set rq_set cp_commit s4_check_after
