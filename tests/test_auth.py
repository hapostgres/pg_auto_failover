import pgautofailover_utils as pgautofailover
from nose.tools import *

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/auth/monitor", authMethod="md5")
    monitor.wait_until_pg_is_running()
    monitor.set_user_password("autoctl_node", "autoctl_node_password")

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/auth/node1", authMethod="md5")
    node1.create()
    node1.set_user_password("pgautofailover_monitor", "monitor_password")
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/auth/node2", authMethod="md5")
    node2.create()
    # no need to set passwords here because it will be inherited from the primary
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
