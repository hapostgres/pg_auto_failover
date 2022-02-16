import tests.pgautofailover_utils as pgautofailover
from nose.tools import *

import os
import json

cluster = None
node1 = None
node2 = None
node3 = None


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
    node1.do_fsm_assign("single")


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

    node1.do_fsm_nodes_set(nodesArray)
    node2.do_fsm_nodes_set(nodesArray)


def test_006_init_to_wait_standby():
    node2.do_fsm_assign("wait_standby")


def test_007_catchingup():
    node1.do_fsm_assign("wait_primary")
    node2.do_fsm_assign("catchingup")


def test_008_secondary():
    node1.do_fsm_assign("primary")
    node2.do_fsm_assign("secondary")

    eq_(node1.get_synchronous_standby_names_local(), "*")


def test_009_init_secondary():
    global node3
    node3 = cluster.create_datanode("/tmp/no-monitor/node3")
    node3.create(monitorDisabled=True, host=str(node3.vnode.address), nodeId=3)
    node3.run(name="c")


def test_010_fsm_nodes_set():
    LSN1 = node1.run_sql_query("select pg_current_wal_flush_lsn()")[0][0]
    LSN2 = node2.run_sql_query("select pg_last_wal_receive_lsn()")[0][0]
    nodesArray = [
        node1.jsDict(LSN1, True),
        node2.jsDict(LSN2, False),
        node3.jsDict("0/1", False),
    ]

    node1.do_fsm_nodes_set(nodesArray)
    node2.do_fsm_nodes_set(nodesArray)
    node3.do_fsm_nodes_set(nodesArray)


def test_011_init_to_wait_standby():
    node1.do_fsm_assign("primary")
    node3.do_fsm_assign("wait_standby")

    eq_(node1.get_synchronous_standby_names_local(), "*")


def test_012_catchingup():
    node3.do_fsm_assign("catchingup")

    eq_(node1.get_synchronous_standby_names_local(), "*")


def test_013_secondary():
    node3.do_fsm_assign("secondary")
    node1.do_fsm_assign("primary")

    # no monitor: use the generic value '*'
    eq_(node1.get_synchronous_standby_names_local(), "*")
