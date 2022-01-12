from nose.tools import *
from tests.tablespaces.tablespace_utils import node1
from tests.tablespaces.tablespace_utils import node2

# Node 1 is brought back online by makefile, wait until it's up and replicating
def test_001_original_primary_comes_back_up():
    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

    results = node1.run_sql_query("SELECT * FROM t4")
    assert results == [(10,), (11,)]
    results = node1.run_sql_query("SELECT * FROM t2")
    assert results == [(3,), (4,), (7,), (12,)]
    results = node1.run_sql_query("SELECT * FROM t3")
    assert results == [(5,), (6,), (8,), (13,)]
