from nose.tools import *
from tests.tablespaces.tablespace_utils import node1


# Node 1 is spun up by Makefile, wait until it's up
def test_000_init_monitor_and_primary():
    # Wait a bit longer than normal for start up
    assert node1.wait_until_state(
        target_state="single", timeout=90, sleep_time=3
    )


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_create_tablespace():
    node1.run_sql_query(
        "CREATE TABLESPACE extended_a LOCATION '/extra_volumes/extended_a';"
    )
    node1.run_sql_query("CREATE TABLE t2(i int) TABLESPACE extended_a;")
    node1.run_sql_query("INSERT INTO t2 VALUES (3), (4);")
