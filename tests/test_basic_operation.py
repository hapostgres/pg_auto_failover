import pgautofailover_utils as pgautofailover
from nose.tools import *

cluster = None
node1 = None
node2 = None
node3 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    cluster.create_monitor("/tmp/monitor")

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_001_stop_postgres():
    node1.stop_postgres()
    assert node1.wait_until_pg_is_running()

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/node2")
    node2.create()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]

@raises(Exception)
def test_005_writes_to_node2_fail():
    node2.run_sql_query("INSERT INTO t1 VALUES (3)")

def test_006_maintenance():
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")
    node2.stop_postgres()
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_007_fail_primary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")

def test_008_writes_to_node2_succeed():
    node2.run_sql_query("INSERT INTO t1 VALUES (4)")
    results = node2.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,), (4,)]

def test_009_start_node1_again():
    node1.run()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")

def test_010_read_from_new_secondary():
    results = node1.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,), (4,)]

@raises(Exception)
def test_011_writes_to_node1_fail():
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")

def test_012_fail_secondary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")

def test_013_drop_secondary():
    node1.run()
    assert node1.wait_until_state(target_state="secondary")
    node1.drop()
    assert node2.wait_until_state(target_state="single")

def test_014_add_new_secondary():
    global node3
    node3 = cluster.create_datanode("/tmp/node3")
    node3.create()
    node3.run()
    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

def test_015_drop_primary():
    node2.drop()
    assert node3.wait_until_state(target_state="single")
