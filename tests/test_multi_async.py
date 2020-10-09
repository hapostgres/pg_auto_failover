import pgautofailover_utils as pgautofailover
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
    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "mkdir", "-p", "/tmp/multi_async/monitor"])
    assert(p.wait() == 0)

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

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")

def test_003_add_standby():
    global node3

    node3 = cluster.create_datanode("/tmp/multi_async/node3")
    node3.create(level='-vv', replicationQuorum=False)
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

    assert node1.set_replication_quorum("false")    # primary
    assert node2.set_replication_quorum("false")    # secondary
    assert node3.set_replication_quorum("false")    # secondary

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

    assert node1.wait_until_state(target_state="secondary")    # secondary
    assert node3.wait_until_state(target_state="secondary")    # secondary
    assert node2.wait_until_state(target_state="primary")      # primary

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
    assert node3.set_replication_quorum("false") # secondary

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
    # pg_autoctl client means we would lsiten to notification and get back
    # to the rest of the code when the promotion is all over with
    #
    # we need to take control way before that, so just trigger the failover
    # and get back to controling our test case.
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
