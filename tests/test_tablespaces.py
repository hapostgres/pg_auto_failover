import pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_

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
    monitor = cluster.create_monitor("/tmp/tablespaces/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/tablespaces/node1")
    node1.create(run=True)
    assert node1.wait_until_state(target_state="single")


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_create_tablespace():
    pgautofailover.sudo_mkdir_p("/tmp/tablespaces/extended")
    node1.run_psql(
        "CREATE TABLESPACE extended LOCATION '/tmp/tablespaces/extended';"
    )
    node1.run_sql_query("CREATE TABLE t2(i int) TABLESPACE extended;")
    node1.run_sql_query("INSERT INTO t2 VALUES (3), (4);")


def test_004_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/tablespaces/node2")
    node2.create(
        run=False,
        tablespaceMappings="/tmp/tablespaces/extended=/tmp/tablespaces/extended_backup",
    )
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_005_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]
    results = node2.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,)]


def test_006_create_tablespace_while_streaming():
    pgautofailover.sudo_mkdir_p("/tmp/tablespaces/extended2")
    node1.run_psql(
        "CREATE TABLESPACE extended2 LOCATION '/tmp/tablespaces/extended2';"
    )
    node1.run_sql_query("CREATE TABLE t3(i int) TABLESPACE extended2;")
    node1.run_sql_query("INSERT INTO t3 VALUES (5), (6)")


def test_007_read_from_secondary_again():
    results = node2.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,)]


def test_008_promote_the_secondary():
    node2.perform_promotion()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")

    node2.run_sql_query("INSERT INTO t2 VALUES (7)")
    results = node1.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,), (7,)]

    node2.run_sql_query("INSERT INTO t3 VALUES (8)")
    results = node1.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,), (8,)]


def test_009_old_primary_goes_down():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")

    pgautofailover.sudo_mkdir_p("/tmp/tablespaces/extended3")
    node2.run_psql(
        "CREATE TABLESPACE extended3 LOCATION '/tmp/tablespaces/extended3';"
    )
    node2.run_sql_query("CREATE TABLE t4(i int) TABLESPACE extended3;")
    node2.run_sql_query("INSERT INTO t4 VALUES (10), (11)")
    node2.run_sql_query("INSERT INTO t2 VALUES (12)")

    node1.run()
    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")
    results = node1.run_sql_query("SELECT * FROM t4")
    assert results == [(10,), (11,)]
    results = node1.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,), (7,), (12,)]


def test_009_promote_the_original_primary():
    node1.perform_promotion()
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")
