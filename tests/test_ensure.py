import pgautofailover_utils as pgautofailover
from nose.tools import *

import time

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/ensure/monitor")
    monitor.wait_until_pg_is_running()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/ensure/node1")
    print()
    print("create node1")
    node1.create()
    print("stop postgres")
    node1.stop_postgres()
    print("run node1")
    node1.run()
    print("wait until Postgres is running")
    node1.wait_until_pg_is_running()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/ensure/node2")
    node2.create()
    node2.stop_postgres()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_demoted():
    print()
    node1.stop_postgres()
    node1.stop_pg_autoctl()
    # we need the pg_autoctl process to run to reach the state demoted,
    # otherwise the monitor assigns that state to node1 but we never reach
    # it
    print("stopped pg_autoctl and postgres, now waiting for 30s")
    node2.sleep(30)
    node1.run()

    assert node1.wait_until_state(target_state="demoted", other_node=node2)

    # ideally we should be able to check that we refrain from starting
    # postgres again before calling the transition function
    print("re-starting pg_autoctl on node1")
    assert node1.wait_until_state(target_state="secondary")
