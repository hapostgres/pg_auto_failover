import tests.pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_
import time
import subprocess

import os.path

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None
node4 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor

    # test creating the monitor in an existing empty directory
    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "mkdir",
            "-p",
            "/tmp/multi_async/monitor",
        ]
    )
    assert p.wait() == 0

    monitor = cluster.create_monitor("/tmp/multi_async/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/multi_async/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")


def test_002_add_standby():
    global node2

    node2 = cluster.create_datanode("/tmp/multi_async/node2")
    node2.create()
    node2.run()

    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")


def test_003_add_standby():
    global node3

    node3 = cluster.create_datanode("/tmp/multi_async/node3")
    node3.create(level="-vv", replicationQuorum=False)
    node3.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to still be zero now
    eq_(node1.get_number_sync_standbys(), 0)

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")


def test_004_set_async():
    # now we set the whole formation to async
    node1.set_number_sync_standbys(0)
    eq_(node1.get_number_sync_standbys(), 0)

    print()
    assert node1.wait_until_state(target_state="primary")

    assert node1.set_replication_quorum("false")  # primary
    assert node2.set_replication_quorum("false")  # secondary
    assert node3.set_replication_quorum("false")  # secondary


def test_005_write_into_primary():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2), (3), (4)")
    node1.run_sql_query("CHECKPOINT")

    results = node1.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]


def test_006_async_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    node2.perform_promotion()

    assert node1.wait_until_state(target_state="secondary")  # secondary
    assert node3.wait_until_state(target_state="secondary")  # secondary
    assert node2.wait_until_state(target_state="primary")  # primary


def test_007_read_from_new_primary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]


#
# The next tests prepare a test-case where at promotion time an async
# standby is first driven to SECONDARY, and then other sync standby nodes in
# REPORT_LSN move forward. We had a bug where the REPORT_LSN nodes would be
# stuck with the primary node being in the WAIT_PRIMARY/PRIMARY state.
#
def test_008_set_sync_async():
    print()

    assert node1.set_replication_quorum("true")  # secondary
    assert node2.set_replication_quorum("true")  # primary
    assert node3.set_replication_quorum("false")  # secondary

    assert node3.set_candidate_priority(0)

    assert node2.wait_until_state(target_state="primary")


def test_009_add_sync_standby():
    global node4

    node4 = cluster.create_datanode("/tmp/multi_async/node4")
    node4.create()
    node4.run()

    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")
    assert node4.wait_until_state(target_state="secondary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()
    assert node4.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to be incremented, we
    # now have two standby nodes that participate in the replication quorum
    # (node1 and node4)
    eq_(node2.get_number_sync_standbys(), 1)

    # make sure we reached primary on node1 before next tests
    assert node2.wait_until_state(target_state="primary")


def test_010_promote_node1():
    print()
    print("Calling pgautofailover.perform_promotion(node1) on the monitor")

    # we don't use node1.perform_promotion() here because using the
    # pg_autoctl client means we would listen to notification and get back
    # to the rest of the code when the promotion is all over with
    #
    # we need to take control way before that, so just trigger the failover
    # and get back to controlling our test case.
    q = "select pgautofailover.perform_promotion('default', 'node_1')"
    monitor.run_sql_query(q)


def test_011_ifdown_node4_at_reportlsn():
    print()
    assert node4.wait_until_state(target_state="report_lsn")
    node4.ifdown()

    assert node3.wait_until_state(target_state="secondary")


def test_012_ifup_node4():
    node4.ifup()

    print()
    assert node3.wait_until_state(target_state="secondary")
    assert node4.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")


def test_013_drop_node4():
    node4.destroy()

    print()
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="secondary")

    # we have only one standby participating in the quorum now
    eq_(node1.get_number_sync_standbys(), 0)


#
# A series of test where we fail primary and candidate secondary node and
# all is left is a secondary that is not a candidate for failover.
#
# In the first series 014_0xx the demoted primary comes back first and needs
# to fetch missing LSNs from node3 because it might have missed some
# transactions.
#
#   node1         node2        node3
#   primary       secondary    secondary
#   <down>
#   demoted       wait_primary secondary
#                 <down>
#   demoted       demoted      report_lsn
#   <up>
#   wait_primary  demoted      secondary   (fast_forward → primary)
#                 <up>
#   primary       secondary    secondary
#
def test_014_001_fail_node1():
    node1.fail()

    # first we have a 30s timeout for the monitor to decide that node1 is
    # down; then we have another 30s timeout at stop_replication waiting for
    # demote_timeout, so let's give it 120s there and step in the middle first
    assert node2.wait_until_state(target_state="stop_replication", timeout=120)
    assert node2.wait_until_state(target_state="wait_primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary")

    node2.check_synchronous_standby_names(ssn="")


def test_014_002_stop_new_primary_node2():
    node2.fail()

    print()
    assert node3.wait_until_state(target_state="report_lsn")


def test_014_003_restart_node1():
    node1.run()

    # node1 used to be primary, now demoted, and meanwhile node2 was primary
    # node1 is assigned report_lsn and then is selected (only node with
    # candidate priority > 0) ; and thus needs to go through fast_forward
    assert node1.wait_until_assigned_state(target_state="fast_forward")
    assert node1.wait_until_state(target_state="stop_replication")
    assert node1.wait_until_state(target_state="wait_primary")
    assert node3.wait_until_state(target_state="secondary")


def test_014_004_restart_node2():
    node2.run()

    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


#
# Test 15 is like test 14, though we inverse the restarting of the failed
# nodes, first the new primary and then very old primary.
#
#   node1        node2        node3
#   primary      secondary    secondary
#   <down>
#   demoted      wait_primary secondary
#                <down>
#   demoted      demoted      report_lsn
#                <up>
#   demoted      wait_primary secondary
#   <up>
#   secondary    primary      secondary
#
def test_015_001_fail_primary_node1():
    node1.fail()

    # first we have a 30s timeout for the monitor to decide that node1 is
    # down; then we have another 30s timeout at stop_replication waiting for
    # demote_timout, so let's give it 120s there
    assert node2.wait_until_state(target_state="wait_primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary")

    node2.check_synchronous_standby_names(ssn="")


def test_015_002_fail_new_primary_node2():
    node2.fail()

    print()
    assert node3.wait_until_state(target_state="report_lsn")


def test_015_003_restart_node2():
    node2.run()

    # restart the previous primary, it re-joins as a (wannabe) primary
    # because the only secondary has candidatePriority = 0, it's wait_primary
    assert node2.wait_until_state(target_state="wait_primary")
    assert node3.wait_until_state(target_state="secondary")

    time.sleep(5)
    assert not node2.get_state().assigned == "primary"


def test_015_004_restart_node1():
    node1.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")


#
# When after loosing both secondary nodes, the one that's back online has
# candidate priority set to zero, then we should remain in wait_primary
# state.
#
def test_016_001_fail_node3():
    node3.fail()
    assert node3.wait_until_assigned_state(target_state="catchingup")


def test_016_002_fail_node1():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")


def test_016_003_restart_node3():
    node3.run()

    assert node3.wait_until_assigned_state(target_state="secondary")
    assert node2.wait_until_state(target_state="wait_primary")

    time.sleep(5)
    assert not node2.get_state().assigned == "primary"


def test_016_004_restart_node1():
    node1.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")


#
# When a node with candidate-priority zero (here, node3) fails while the
# primary node (here, node2) is already in wait_primary, the non-candidate
# node (here, node3) should still be assigned catchingup.
#
def test_017_001_fail_node1():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")


def test_017_002_fail_node3():
    node3.fail()
    assert node3.wait_until_assigned_state(target_state="catchingup")


def test_017_003_restart_nodes():
    node3.run()
    node1.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")
