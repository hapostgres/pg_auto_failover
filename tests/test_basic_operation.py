import pgautofailover_utils as pgautofailover
from nose.tools import *

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
    monitor = cluster.create_monitor("/tmp/basic/monitor")
    monitor.run()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/basic/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_stop_postgres():
    node1.stop_postgres()
    assert node1.wait_until_pg_is_running()

def test_003_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_004_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/basic/node2")
    node2.create()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

def test_005_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]

@raises(Exception)
def test_006_writes_to_node2_fail():
    node2.run_sql_query("INSERT INTO t1 VALUES (3)")

def test_007_maintenance():
    print()
    print("Enabling maintenance on node2")
    assert node1.wait_until_state(target_state="primary")
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")
    node2.stop_postgres()
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")

    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_008_fail_primary():
    print()
    print("Injecting failure of node1")
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")

def test_009_writes_to_node2_succeed():
    node2.run_sql_query("INSERT INTO t1 VALUES (4)")
    results = node2.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,), (4,)]

def test_010_start_node1_again():
    node1.run()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")

def test_011_read_from_new_secondary():
    results = node1.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,), (4,)]

@raises(Exception)
def test_012_writes_to_node1_fail():
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")

def test_013_fail_secondary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")

def test_014_drop_secondary():
    node1.run()
    assert node1.wait_until_state(target_state="secondary")
    node1.drop()
    assert not node1.pg_is_running()
    assert node2.wait_until_state(target_state="single")

    # replication slot list should be empty now
    assert node2.has_needed_replication_slots()

def test_015_add_new_secondary():
    global node3
    node3 = cluster.create_datanode("/tmp/basic/node3")
    node3.create()
    node3.run()
    assert node3.wait_until_state(target_state="secondary", other_node=node2)
    assert node2.wait_until_state(target_state="primary", other_node=node3)

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

# In previous versions of pg_auto_failover we removed the replication slot
# on the secondary after failover. Now, we instead maintain the replication
# slot's replay_lsn thanks for the monitor tracking of the nodes' LSN
# positions.
#
# So rather than checking that we want to zero replication slots after
# replication, we check that we still have a replication slot for the other
# node.
#
def test_016_multiple_manual_failover_verify_replication_slots():
    print()

    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node2.wait_until_state(target_state="secondary", other_node=node3)
    assert node3.wait_until_state(target_state="primary", other_node=node2)

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node2.wait_until_state(target_state="primary", other_node=node3)
    assert node3.wait_until_state(target_state="secondary", other_node=node2)

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

def test_017_drop_primary():
    node2.drop()
    assert not node2.pg_is_running()
    assert node3.wait_until_state(target_state="single")

def test_018_stop_postgres_monitor():
    original_state = node3.get_state()
    monitor.stop_postgres()
    monitor.wait_until_pg_is_running()
    assert node3.wait_until_state(target_state=original_state)
