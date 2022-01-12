import tests.pgautofailover_utils as pgautofailover
import time

from nose.tools import eq_

cluster = None
monitor = None
node1 = None
node2 = None

newmonitor = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/replace/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/replace/node1")
    node1.create(run=True)
    assert node1.wait_until_state(target_state="single")


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/replace/node2")
    node2.create(run=True)
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_004_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]


def test_005_drop_monitor():
    monitor.destroy()
    cluster.monitor = None


def test_006a_disable_monitor_node2():
    node2.disable_monitor()


def test_006b_disable_monitor_node1():
    node1.disable_monitor()


def test_006c_write_to_primary():
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_007_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,)]


def test_008_create_new_monitor():
    global newmonitor
    newmonitor = cluster.create_monitor("/tmp/replace/newmonitor")
    newmonitor.run()


def test_009a_enable_monitor_node1():
    node1.enable_monitor(newmonitor)
    assert node1.wait_until_state(target_state="single")


def test_009b_enable_monitor_node2():
    node2.enable_monitor(newmonitor)
    assert node2.wait_until_state(target_state="catchingup")
    assert node1.wait_until_state(target_state="wait_primary")


def test_010_wait_until_state():
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")


def test_011_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    newmonitor.failover()

    assert node2.wait_until_state(target_state="primary")
    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_1)",
    )

    assert node1.wait_until_state(target_state="secondary")


def test_012_read_from_secondary():
    results = node1.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,)]
