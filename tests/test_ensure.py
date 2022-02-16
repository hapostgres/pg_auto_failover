import tests.pgautofailover_utils as pgautofailover
from nose.tools import *

import time
import os.path

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
    monitor.run()
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
    # We must not wait for PG to run, since otherwise we might miss the demoted
    # state

    assert node1.wait_until_state(target_state="demoted")

    # ideally we should be able to check that we refrain from starting
    # postgres again before calling the transition function
    print("re-starting pg_autoctl on node1")
    assert node1.wait_until_state(target_state="secondary")


def test_005_inject_error_in_node2():
    assert node2.wait_until_state(target_state="primary")

    # break Postgres setup on the primary, and restart Postgres: then
    # Postgres keeps failing to start, and pg_autoctl still communicates
    # with the monitor, which should still orchestrate a failover.
    pgconf = os.path.join(node2.datadir, "postgresql.conf")

    with open(pgconf, "a+") as f:
        f.write("\n")
        f.write("shared_preload_libraries='wrong_extension'\n")

    node2.restart_postgres()

    # the first step is the promotion of the other node as the new primary:
    assert node1.wait_until_state("wait_primary")

    # then when the failover happens, the new primary postgresql.conf gets
    # copied over, and we get the nodes back to primary/secondary
    assert node2.wait_until_state("secondary")
    assert node1.wait_until_state("primary")
