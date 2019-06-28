import pgautofailover_utils as pgautofailover
from nose.tools import *
import time

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
    monitor = cluster.create_monitor("/tmp/monitor", authMethod="md5")
    monitor.wait_until_pg_is_running()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/node1", authMethod="md5")
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
    node2 = cluster.create_datanode("/tmp/auth/node2", authMethod="md5")
    node2.create()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
