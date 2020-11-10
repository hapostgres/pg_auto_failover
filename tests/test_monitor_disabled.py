import pgautofailover_utils as pgautofailover
from nose.tools import *

import os
import json

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/no-monitor/node1")
    node1.create(monitorDisabled=True, host=str(node1.vnode.address), nodeId=1)
    node1.run(name="a")

def test_002_init_to_single():
    print()
    node1.do_fsm_assign("single")
    node1.wait_until_local_state("single")

def test_003_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_004_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/no-monitor/node2")
    node2.create(monitorDisabled=True, host=str(node2.vnode.address), nodeId=2)
    node2.run(name="b")

def test_005_fsm_nodes_set():
    nodesArray = [node1.jsDict("0/1", True), node2.jsDict("0/1", False)]

    print()
    node1.do_fsm_nodes_set(nodesArray)
    node2.do_fsm_nodes_set(nodesArray)

def test_006_init_to_wait_standby():
    print()
    node2.do_fsm_assign("wait_standby")
    node2.wait_until_local_state("wait_standby")

def test_007_catchingup():
    print()
    node1.do_fsm_assign("wait_primary")
    node1.wait_until_local_state("wait_primary")

    node2.do_fsm_assign("catchingup")
    node2.wait_until_local_state("catchingup")

def test_008_secondary():
    print()
    node1.do_fsm_assign("primary")
    node2.do_fsm_assign("secondary")

    node1.wait_until_local_state("primary")
    node2.wait_until_local_state("secondary")

    eq_(node1.get_synchronous_standby_names_local(), '*')
