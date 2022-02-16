from nose.tools import *
from tests.tablespaces.tablespace_utils import node2

# Node 1 is paused by the Makefile, wait for pgaf to acknowledge
def test_001_old_primary_goes_down():
    assert node2.wait_until_state(target_state="wait_primary")

    node2.run_sql_query(
        "CREATE TABLESPACE extended_c LOCATION '/extra_volumes/extended_c';"
    )
    node2.run_sql_query("CREATE TABLE t4(i int) TABLESPACE extended_c;")
    node2.run_sql_query("INSERT INTO t4 VALUES (10), (11)")
    node2.run_sql_query("INSERT INTO t2 VALUES (12)")
    node2.run_sql_query("INSERT INTO t3 VALUES (13)")
