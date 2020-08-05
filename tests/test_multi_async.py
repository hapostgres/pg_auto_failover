import pgautofailover_utils as pgautofailover
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
    node3.create()
    node3.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to be set to 1 now
    assert node1.get_number_sync_standbys() == 1

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

def test_005_set_priorities():
    # to make the tests consistent, we assign higher
    # priorty to node1
    assert node1.set_candidate_priority(90)      # current primary
    assert node2.set_candidate_priority(100)     # next primary
    assert node3.set_candidate_priority(90)      # secondary

    # when we set candidate priority we go to join_primary then primary
    print()
    assert node1.wait_until_state(target_state="primary")

def test_006_write_into_primary():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2), (3), (4)")
    node1.run_sql_query("CHECKPOINT")

    results = node1.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]

def test_007_async_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()

    assert node2.wait_until_state(target_state="wait_primary") # primary
    assert node1.wait_until_state(target_state="secondary")    # secondary
    assert node3.wait_until_state(target_state="secondary")    # secondary
    assert node2.wait_until_state(target_state="primary")      # primary

def test_008_read_from_new_primary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]
