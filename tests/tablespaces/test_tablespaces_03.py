from nose.tools import *
from tests.tablespaces.tablespace_utils import node1
from tests.tablespaces.tablespace_utils import node2

# Failover is performed by the makefile Makefile, wait for it to complete
def test_001_wait_for_failover_and_insert():
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")

    node2.run_sql_query("INSERT INTO t2 VALUES (7)")
    results = node1.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,), (7,)]

    node2.run_sql_query("INSERT INTO t3 VALUES (8)")
    results = node1.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,), (8,)]
