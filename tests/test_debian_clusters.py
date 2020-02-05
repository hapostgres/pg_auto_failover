import pgautofailover_utils as pgautofailover
from nose.tools import *
import time
import os.path

cluster = None
monitor = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/debian/monitor")
    monitor.wait_until_pg_is_running()

def test_001_custom_single():
    global node1
    node1_path = cluster.create_pgcluster("custom_node1", port=6001)
    
    postgres_conf_path = os.path.join(node1_path, "postgresql.conf");
    # verify postgresql.conf is not in data directory
    assert not os.path.exists(postgres_conf_path)
    
    node1 = cluster.create_datanode(node1_path, port=6001)
    node1.create(run=True)
    
    node1.wait_until_pg_is_running()

    # verify postgresql.conf is in data directory now
    assert os.path.exists(postgres_conf_path)
    
    print(monitor.run_sql_query("select nodeid, nodename, nodeport, goalstate, reportedstate from pgautofailover.node order by nodeid asc"))

    node1.destroy()
    # print(monitor.run_sql_query("select nodeid, goalstate, reportedstate from pgautofailover.node order by nodeid asc"))
    
    # node is stuck in init state, destroy won't remove it from monitor
    # force remove from monitor
    
    monitor.run_sql_query("select pgautofailover.remove_node('%s', %s)" %(str(node1.vnode.address), str(node1.port)))
    # print(monitor.run_sql_query("select nodeid, goalstate, reportedstate from pgautofailover.node order by nodeid asc"))     
