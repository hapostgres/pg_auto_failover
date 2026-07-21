import tests.pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_
import signal
import time

import os.path

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor(
        "/tmp/multi_alternate_primary_failures/monitor"
    )
    monitor.run()
    monitor.wait_until_pg_is_running()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode(
        "/tmp/multi_alternate_primary_failures/node1"
    )
    node1.create()
    node1.run()
    print()
    assert node1.wait_until_state(target_state="single")


def test_002_001_add_two_standbys():
    global node2

    node2 = cluster.create_datanode(
        "/tmp/multi_alternate_primary_failures/node2"
    )
    node2.create()
    node2.run()

    node2.wait_until_pg_is_running()

    print()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # with one standby, we have number_sync_standbys set to 0 still
    assert node1.get_number_sync_standbys() == 0


def test_002_002_add_two_standbys():
    global node3

    node3 = cluster.create_datanode(
        "/tmp/multi_alternate_primary_failures/node3"
    )
    node3.create()
    node3.run()

    node3.wait_until_pg_is_running()

    print()
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # with two standbys, we have number_sync_standbys set to 1
    assert node1.get_number_sync_standbys() == 1


#
# In this test series, we have
#
#   node1         node2        node3
#   primary       secondary    secondary
#   <down>
#   demoted       primary      secondary
#                 <down>
#   demoted       draining     report_lsn
#                 <up>
#   demoted       primary      secondary
#   <up>
#   secondary     primary      secondary
#
def test_003_001_stop_primary():
    # verify that node1 is primary and stop it
    assert node1.get_state().assigned == "primary"
    node1.fail()

    # wait for node2 to become the new primary
    print()
    assert node1.wait_until_assigned_state(target_state="demoted")
    assert node2.wait_until_state(target_state="primary")


def test_003_002_stop_primary():
    # verify that node2 is primary and stop it
    assert node2.get_state().assigned == "primary"
    # SIGINT, not the default SIGTERM: this is the one scenario in this file
    # where node3 is the *sole* surviving standby (node1 is already dead from
    # test_003_001), so it depends on node2 never being able to report
    # anything again. A plain fail() (SIGTERM) triggers pg_autoctl's graceful
    # shutdown reporting (keeper_node_active_shutdown_loop(), #1146): the
    # node-active service keeps calling the full node_active() protocol every
    # second for up to 30s while PostgreSQL's own shutdown checkpoint
    # completes, racing against that checkpoint's duration. If node2 manages
    # to confirm "draining" (both goal and reported state) before it's
    # actually gone, BuildCandidateList()'s "skip old/new primary unless
    # draining/demoted" exemption stops excluding it, letting it contribute a
    # second quorum report alongside node3 -- satisfying
    # number_sync_standbys=1's minCandidates=2 and promoting node3 right away
    # instead of leaving it correctly stuck at report_lsn. This is exactly
    # the PG14 CI flake: non-deterministic depending on how long the
    # checkpoint happens to take on that runner. SIGINT skips the shutdown
    # loop entirely (asked_to_stop_fast exits immediately), matching what
    # fail() is meant to simulate -- an abrupt crash, not a graceful stop --
    # and making node2 permanently unable to report past whatever state it
    # was last confirmed in.
    node2.fail(sig=signal.SIGINT)

    # node3 can't be promoted when it's the only one reporting its LSN
    print()
    # Reaching this point requires health-check detection of node2's death
    # (node_considered_unhealthy_timeout, 20s default) plus a full
    # primary_demote_timeout (30s default) -- and test_003_001 immediately
    # before this one already drove node1 through the identical sequence, so
    # a loaded CI runner can push this past the default 90s STATE_CHANGE_TIMEOUT
    # even though the FSM itself is behaving correctly. Use the same 120s
    # margin as the equivalent waits in test_005_001/test_005_002 below.
    assert node2.wait_until_assigned_state(
        target_state="draining", or_state="demote_timeout", timeout=120
    )
    # With node2 hard-killed, it can never confirm a second LSN report, so
    # node3 should stay deterministically stuck at report_lsn (quorumCandidateCount
    # stays 1, short of number_sync_standbys=1's minCandidates=2). or_state
    # is kept as a defensive margin, not because prepare_promotion is
    # expected here.
    assert node3.wait_until_assigned_state(
        target_state="report_lsn", or_state="prepare_promotion", timeout=120
    )

    # check that node3 stays blocked and doesn't go to wait_primary
    node3.sleep(5)
    assert node3.wait_until_assigned_state(
        target_state="report_lsn", or_state="prepare_promotion", timeout=120
    )


def test_003_003_bringup_last_failed_primary():
    # Restart node2
    node2.run()

    # Now node 2 should become primary
    print()
    assert node2.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")


def test_003_004_bringup_first_failed_primary():
    # Restart node1
    node1.run()
    node3.wait_until_pg_is_running()

    # Now node 1 should become secondary
    print()
    assert node1.wait_until_state(target_state="secondary")
    assert node2.get_state().assigned == "primary"
    assert node3.get_state().assigned == "secondary"

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()


#
# In this test series , we have
#
#   node1         node2        node3
#   secondary     primary      secondary
#                 <down>
#   primary       demoted      secondary
#   <down>
#   draining      demoted      report_lsn
#                 <up>
#   draining      secondary    primary
#   <up>
#   secondary     secondary    primary
#
def test_005_001_fail_primary_again():
    # verify that node2 is primary and stop it
    assert node2.get_state().assigned == "primary"
    node2.fail()

    print()
    assert node2.wait_until_assigned_state(
        target_state="demote_timeout", timeout=120
    )
    assert node2.wait_until_assigned_state(target_state="demoted", timeout=120)
    assert node1.wait_until_assigned_state(target_state="primary", timeout=120)
    assert node1.wait_until_state(target_state="primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary", timeout=120)


def test_005_002_fail_primary_again():
    # Start node2 first so that it can participate in the LSN quorum.
    # node2 was killed in test_005_001 and never restarted; it is still
    # registered as a sync standby (replication_quorum=true). With
    # number_sync_standbys=1 the monitor needs 2 LSN reports to elect a new
    # primary (pgautofailover.guard_data_loss defaults to true as of #1142),
    # and without node2 back online only node3 would report, stalling the
    # failover indefinitely -- the monitor logs "2 nodes are required in the
    # quorum to satisfy number_sync_standbys=1" forever instead of electing
    # node3.
    node2.run()

    # verify that node1 is primary and stop it
    assert node1.get_state().assigned == "primary"
    node1.fail()

    print()
    # report_lsn is transient (milliseconds); the monitor may assign it and
    # immediately advance to prepare_promotion before the poll sees it.
    # Wait for the stable end-state instead.
    assert node3.wait_until_state(target_state="primary", timeout=300)


def test_005_003_bring_up_first_failed_primary():
    # node2 was already started in test_005_002; wait for it to join as
    # secondary. 'demoted' is a transient intermediate state that lasts
    # under a second and may have already come and gone by the time we get
    # here (it races unreliably on shared CI runners) -- wait for the
    # stable end state instead.
    print()
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")


def test_005_004_bring_up_last_failed_primary():
    # Restart node1
    node1.run()
    node1.wait_until_pg_is_running()

    # Now node 3 should become secondary
    print()
    assert node1.wait_until_state(target_state="secondary")
    assert node3.get_state().assigned == "primary"
    assert node2.get_state().assigned == "secondary"


#
# In this test series , we have
#
#   node1         node2        node3
#   secondary     secondary    primary
#                              <down>
#   primary       secondary    demoted
#   <down>
#                              <up>
#   demoted       primary      secondary
#   <up>
#   secondary     primary      secondary
#
def test_006_001_fail_primary():
    assert node3.get_state().assigned == "primary"
    node3.fail()

    print()
    assert node3.wait_until_assigned_state(
        target_state="demote_timeout", timeout=120
    )
    assert node3.wait_until_assigned_state(target_state="demoted", timeout=120)
    assert node1.wait_until_assigned_state(target_state="primary", timeout=120)
    assert node1.wait_until_state(target_state="primary", timeout=120)
    assert node2.wait_until_state(target_state="secondary", timeout=120)


def test_006_002_fail_new_primary():
    assert node1.get_state().assigned == "primary"
    node1.fail()
    node3.run()

    print()
    assert node2.wait_until_state(target_state="primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary", timeout=120)


def test_006_003_bringup_last_failed_primary():
    node1.run()

    print()
    assert node1.wait_until_state(target_state="secondary", timeout=120)
    assert node2.wait_until_state(target_state="primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary", timeout=120)
