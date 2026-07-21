# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the PostgreSQL License.
#
# Bug 6, root-caused: pytest/monitor (PG16) test_003_002_stop_primary,
# node3 promoted past report_lsn/prepare_promotion despite
# number_sync_standbys=1 (minCandidates=2) leaving only one node able to
# report once both other nodes are dead.
#
# Root cause, found by comparing lock discipline before/after
# d5a159b ("monitor: extract GroupStateContext + pure health predicates",
# origin/main): ProceedGroupState() builds a GroupStateContext once, up
# front (BuildGroupStateContext() -> ctx->groupNodeList, a single
# AutoFailoverNodeGroup() SPI read). BuildCandidateList() -- inside
# ProceedGroupStateForMSFailover() -- iterates that same cached
# ctx->groupNodeList for every skip/missing/candidate decision. But
# ProceedGroupStateFromContext() also calls GetPrimaryOrDemotedNodeInGroup()
# separately -- a second, independent SPI read, done *after* ctx was built
# -- and passes its result (primaryNode) into
# ProceedGroupStateForMSFailover(ctx, primaryNode) alongside the older
# ctx->groupNodeList. NodeIsUnhealthy(primaryNode, ctx) at the outer gate
# sees whatever is freshest as of that second read; BuildCandidateList()'s
# per-node NodeIsUnhealthy() checks, applied to ctx->groupNodeList's own
# (older) copy of the very same node, do not. Before d5a159b,
# ProceedGroupStateForMSFailover() re-fetched the node list itself (the
# "redundant AutoFailoverNodeGroup() call" the commit message says it
# removed) immediately before building candidates, so both reads were
# adjacent and saw the same data; the refactor's single up-front context
# reintroduced a window between them.
#
# Nothing closes that window: SetNodeHealthState() (the real health-check
# worker's write) took no LockFormation()/LockNodeGroup() at all, unlike
# NodeActive(), RemoveNode(), and every other node-mutating entry point.
# A health-check write marking the second primary unhealthy could land in
# exactly that gap -- after ctx->groupNodeList was read, before
# BuildCandidateList() evaluates it -- leaving the stale, still-"healthy
# primary" copy of that node to fall through BuildCandidateList()'s
# "skip old/new primary" branch uncounted, rather than being recognized as
# newly unhealthy and counted towards missingNodesCount.
#
# Fixed by making SetNodeHealthState() take the same
# LockFormation(ShareLock)/LockNodeGroup(ExclusiveLock) every other writer
# takes, serializing health-check writes with in-flight FSM evaluations
# for the same group -- the same remedy already applied twice this session
# (RemoveNode(), NodeActive()) for the same class of defect: a writer that
# doesn't hold the lock a reader assumes is held.
#
# This spec proves it two ways:
#
#   1. testing_lock_node_group()/testing_set_node_health() (new,
#      testing-only, not granted to autoctl_node) let a session hold the
#      exact locks a real writer would need, from plain SQL. A concurrent
#      node_active() call for a different node in the same group now
#      provably blocks behind testing_set_node_health() -- before the fix,
#      a plain UPDATE simulating the same write did not block at all,
#      which is the whole defect in one sentence.
#
#   2. The full test_003_001 -> test_003_002 sequence (crsp3_p killed,
#      crsp3_s1 promoted through the real report_lsn -> ... -> primary
#      path, crsp3_s1 then also killed) with crsp3_s1's health-check write
#      made to race crsp3_s2's routine report via testing_set_node_health(),
#      instead of the raw UPDATE both of this investigation's earlier
#      attempts used (stale_primary_report.sql,
#      concurrent_second_primary_death_report.spec). crsp3_s2 must still
#      end up stuck at report_lsn -- number_sync_standbys=1 requires 2
#      quorum reporters, and crsp3_p is already fully retired, leaving only
#      crsp3_s2 able to ever report.

setup
{
    CREATE EXTENSION IF NOT EXISTS pgautofailover CASCADE;

    SELECT pgautofailover.create_formation('crsp3_test', 'pgsql', 'postgres', true, 1);

    SELECT pgautofailover.register_node('crsp3_test', 'crsp3_p', 5432, 'postgres', 'crsp3_p', 1);
    SELECT pgautofailover.register_node('crsp3_test', 'crsp3_s1', 5432, 'postgres', 'crsp3_s1', 1);
    SELECT pgautofailover.register_node('crsp3_test', 'crsp3_s2', 5432, 'postgres', 'crsp3_s2', 1);

    -- bootstrap: crsp3_p=primary, crsp3_s1=secondary, crsp3_s2=secondary
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'single');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'single', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'wait_standby');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'catchingup', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        0, current_group_role => 'primary', current_lsn => '0/5000');

    -- ── generation 1: crsp3_p (node1) hard-killed. crsp3_s1 (node2) takes
    -- over through the real report_lsn -> ... -> primary sequence.
    --
    -- This SET only reaches the rest of generation 1 below, which all runs
    -- on this same setup connection. It does NOT reach session s2's later
    -- evaluation of crsp3_s1 in generation 2 -- pg_isolation_regress gives
    -- each named session its own separate connection, and SET is session
    -- local. Session s2 sets this again itself (see s2_set_grace) for that
    -- reason.
    SET pgautofailover.startup_grace_period = 1;

    SELECT pgautofailover.testing_set_node_health(
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        health => 0, report_time_ago => interval '60 seconds');

    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'prepare_promotion', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'stop_replication', current_lsn => '0/5000');

    SELECT pgautofailover.testing_set_node_health(
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_p'),
        state_change_time_ago => interval '31 seconds');

    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'stop_replication', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'report_lsn', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
        0, current_group_role => 'secondary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'wait_primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'primary', current_lsn => '0/5000');
    SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
        (SELECT nodeid FROM pgautofailover.node WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        0, current_group_role => 'primary', current_lsn => '0/5000');

    -- generation 1 complete: crsp3_p demoted, crsp3_s1 primary, crsp3_s2
    -- secondary (checked by s3_check_before, so it shows up in the .out
    -- file instead of being silently asserted here).
}

teardown
{
    DELETE FROM pgautofailover.node WHERE formationid = 'crsp3_test';
    DELETE FROM pgautofailover.formation WHERE formationid = 'crsp3_test';
}

session s3
step s3_check_before {
    SELECT nodename, goalstate, reportedstate, health
      FROM pgautofailover.node
     WHERE formationid = 'crsp3_test'
     ORDER BY nodename;
}

# The health-check worker's real write path (now lock-protected), marking
# crsp3_s1 dead exactly as generation 1 marked crsp3_p dead. BEGIN here so
# its LockNodeGroup() stays held open until hc_commit, giving s2_report
# something to provably block behind.
session hc
step hc_begin  { BEGIN; }
step hc_mark_dead {
    SELECT pgautofailover.testing_set_node_health(
        (SELECT nodeid FROM pgautofailover.node
          WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s1'),
        health => 0, report_time_ago => interval '60 seconds');
}
step hc_commit { COMMIT; }

# crsp3_s2's routine report, racing the health-check write above. Before
# the fix this would proceed immediately against SetNodeHealthState's
# uncommitted (and unlocked, so irrelevant) write; after the fix it must
# block until hc_commit, then see a fully-consistent post-health-check
# world -- proving the two writers can no longer interleave mid-decision.
session s2

# node_active()'s IsUnhealthy() check gates on both reportTime staleness
# AND enough real time having passed since the *monitor postgres
# instance's own* startup (PgStartTime), not since crsp3_s1's own state
# change -- a grace window meant to avoid spurious failovers right after
# the monitor itself restarts. The generation-1 SET in setup{} only
# covers that block's own connection; against a freshly started postgres
# (as pg_virtualenv gives each installcheck run) the unmodified 10s
# default on session s2's own connection can still be open when this
# runs, silently masking the race hc_mark_dead/s2_report exist to prove.
# Set it low here too, on s2's own connection, before touching crsp3_s1.
step s2_set_grace { SET pgautofailover.startup_grace_period = 1; }
step s2_report { SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
                     0, current_group_role => 'secondary', current_lsn => '0/5000'); }
step s2_report_again { SELECT assigned_group_state FROM pgautofailover.node_active('crsp3_test',
                     (SELECT nodeid FROM pgautofailover.node
                       WHERE formationid = 'crsp3_test' AND nodename = 'crsp3_s2'),
                     0, current_group_role => 'report_lsn', current_lsn => '0/5000'); }

session s4
step s4_check_after {
    SELECT nodename, goalstate, reportedstate, health
      FROM pgautofailover.node
     WHERE formationid = 'crsp3_test'
     ORDER BY nodename;
}

# s2_report starts while hc still holds LockNodeGroup(ExclusiveLock)
# (uncommitted testing_set_node_health); the isolation tester detects the
# block and only reports s2_report's result once hc_commit releases the
# lock. crsp3_s2 must end up assigned report_lsn and stay there -- crsp3_p
# is fully retired and crsp3_s1 is now dead too, so with
# number_sync_standbys=1 (minCandidates=2) only crsp3_s2 can ever report:
# it can never satisfy quorum alone.
permutation s3_check_before s2_set_grace hc_begin hc_mark_dead s2_report hc_commit s2_report_again s4_check_after
