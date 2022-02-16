import tests.pgautofailover_utils as pgautofailover
import time

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/create-run/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/create-run/node1")
    node1.create(run=True)
    assert node1.wait_until_state(target_state="single")


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/create-run/node2")
    node2.create(run=True)
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_004_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]


def test_005_maintenance():
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")
    node2.fail()
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")
    node2.run()
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_006_fail_primary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary", timeout=180)


def test_007_start_node1_again():
    node1.create(run=True)
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")


def test_008_read_from_new_secondary():
    results = node1.run_sql_query("SELECT * FROM t1 ORDER BY a")
    assert results == [(1,), (2,), (3,)]


def test_009_fail_secondary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")


def test_010_drop_secondary():
    node1.run()
    assert node1.wait_until_state(target_state="secondary")
    node1.drop()
    time.sleep(2)  # avoid timing issue
    assert not node1.pg_is_running()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="single")
