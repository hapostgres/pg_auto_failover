from nose.tools import *
from tests.tablespaces.tablespace_utils import node1
from tests.tablespaces.tablespace_utils import node2

# Failover is performed by the makefile Makefile, wait for it to complete
def test_001_promote_the_original_primary_successfully():
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")
