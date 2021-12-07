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
    pgautofailover.sudo_mkdir_p('/tmp/tablespaces/node1-extended')
    node1.run_psql("CREATE TABLESPACE extended LOCATION '/tmp/tablespaces/node1-extended';")
    node1.run_sql_query("CREATE TABLE t2(i int) TABLESPACE extended;")
    node1.run_sql_query("INSERT INTO t2 VALUES (3), (4)")

def test_004_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/tablespaces/node2")
    node2.create(run=True)
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_005_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]
    results = node2.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,)]

def test_006_create_tablespace_while_streaming():
    pgautofailover.sudo_mkdir_p('/tmp/tablespaces/node1-extended2')
    node1.run_psql("CREATE TABLESPACE extended2 LOCATION '/tmp/tablespaces/node1-extended2';")
    node1.run_sql_query("CREATE TABLE t3(i int) TABLESPACE extended2;")
    node1.run_sql_query("INSERT INTO t3 VALUES (5), (6)")

def test_007_read_from_secondary_again():
    results = node2.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,)]
