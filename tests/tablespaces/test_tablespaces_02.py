from nose.tools import *
from tests.tablespaces.tablespace_utils import node1
from tests.tablespaces.tablespace_utils import node2


# Node 2 is spun up by Makefile, wait until it's up and replicating
def test_001_init_secondary():
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_002_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]
    results = node2.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,)]


def test_003_create_tablespace_while_streaming():
    node1.run_sql_query(
        "CREATE TABLESPACE extended_b LOCATION '/extra_volumes/extended_b';"
    )
    node1.run_sql_query("CREATE TABLE t3(i int) TABLESPACE extended_b;")
    node1.run_sql_query("INSERT INTO t3 VALUES (5), (6)")


def test_004_read_from_secondary_again():
    results = node2.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,)]
