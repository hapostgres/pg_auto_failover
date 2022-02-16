import tests.pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_
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
    node2.fail()

    # node3 can't be promoted when it's the only one reporting its LSN
    print()
    assert node2.wait_until_assigned_state(target_state="draining")
    assert node3.wait_until_state(target_state="report_lsn")

    # check that node3 stays at report_lsn and doesn't go to wait_primary
    node3.sleep(5)
    assert node3.wait_until_state(target_state="report_lsn")


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
    # verify that node1 is primary and stop it
    assert node1.get_state().assigned == "primary"
    node1.fail()

    print()
    assert node1.wait_until_assigned_state(target_state="draining")
    assert node3.wait_until_assigned_state(target_state="report_lsn")


def test_005_003_bring_up_first_failed_primary():
    # Restart node2
    node2.run()

    print()
    assert node2.wait_until_state(target_state="demoted")

    # Now node 2 should become secondary
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
