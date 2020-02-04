import pgautofailover_utils as pgautofailover
from nose.tools import *
import time

cluster = None
monitor = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/debian/monitor")
    monitor.wait_until_pg_is_running()

def test_001_custom_primary():
    global node1
    node1_path = cluster.create_pgcluster("custom_node1", port=6001)
    
    node1 = cluster.create_datanode(node1_path, port=6001)
    node1.create(run=True)

    node1.destroy()

def test_002_custom_secondary():
    global node1
    global node2
    node1 = cluster.create_datanode("/tmp/debian/node1")
    node1.create(run=True)
    node1.wait_until_pg_is_running()

    node2_path = cluster.create_pgcluster("custom_node2")
    node2 = cluster.create_datanode(node2_path)
    node2.create(run=True)
    assert node2.wait_until_state(target_state="secondary")
    
