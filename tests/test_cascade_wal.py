import pgautofailover_utils as pgautofailover
from nose.tools import *
import time

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
    monitor = cluster.create_monitor("/tmp/cascade/monitor")
    monitor.run()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/cascade/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_add_two_standbys():
    global node2
    global node3

    print()

    node2 = cluster.create_datanode("/tmp/cascade/node2")
    node2.create()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    node3 = cluster.create_datanode("/tmp/cascade/node3")
    node3.create()
    node3.run()
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

def test_003_set_candidate_priority():
    assert node1.get_candidate_priority() == 100

    assert node3.set_candidate_priority(0)
    assert node3.get_candidate_priority() == 0

def test_004_ifdown_node2():
    node2.ifdown()

def test_005_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query(
        "INSERT INTO t1 SELECT x FROM generate_series(1, 100000) as gs(x)")
    node1.run_sql_query("CHECKPOINT")

    lsn = node1.run_sql_query("select pg_current_wal_lsn()")[0][0]
    print("%s " % lsn, end="", flush=True)

def test_006_failover():
    print()
    print("Injecting failure of node1")
    node1.fail()

    # have node2 re-join the network and hopefully reconnect etc
    print("Reconnecting node2 (ifconfig up)")
    node2.ifup()

    # now we should be able to continue with the failover, and fetch missing
    # WAL bits from node3
    node2.wait_until_pg_is_running()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    assert node2.wait_until_state(target_state="wait_primary", timeout=120)
    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

def test_007_read_from_new_primary():
    results = node2.run_sql_query("SELECT count(*) FROM t1")
    assert results == [(100000,)]

def test_008_start_node1_again():
    node1.run()
    assert node1.wait_until_state(target_state="secondary")

    assert node2.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()
