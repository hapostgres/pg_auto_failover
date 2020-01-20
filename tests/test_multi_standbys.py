import pgautofailover_utils as pgautofailover
from nose.tools import *
import time

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None
node4 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/multi_standby/monitor")
    monitor.wait_until_pg_is_running()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/multi_standby/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_candidate_priority():
    assert node1.get_candidate_priority() == 100

    assert not node1.set_candidate_priority(-1)
    assert node1.get_candidate_priority() == 100

    assert node1.set_candidate_priority(99)
    assert node1.get_candidate_priority() == 99

def test_003_replication_quorum():
    assert node1.get_replication_quorum()

    assert not node1.set_replication_quorum("wrong quorum")
    assert node1.get_replication_quorum()

    assert node1.set_replication_quorum("false")
    assert not node1.get_replication_quorum()

    assert node1.set_replication_quorum("true")
    assert node1.get_replication_quorum()

def test_004_add_three_standbys():
    # the next test wants to set number_sync_standbys to 2
    # so we need at least 3 standbys to allow that
    global node2
    global node3
    global node4

    node2 = cluster.create_datanode("/tmp/multi_standby/node2")
    node2.create()
    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    node3 = cluster.create_datanode("/tmp/multi_standby/node3")
    node3.create()
    node3.run()
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    node4 = cluster.create_datanode("/tmp/multi_standby/node4")
    node4.create()
    node4.run()
    assert node4.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_005_number_sync_standbys():
    print()
    assert node1.get_number_sync_standbys() == 1
    assert not node1.set_number_sync_standbys(-1)
    assert node1.get_number_sync_standbys() == 1

    assert not node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 1

    print("set number_sync_standbys = 0")
    assert node1.set_number_sync_standbys(0)
    assert node1.get_number_sync_standbys() == 0
    print("synchronous_standby_names = '%s'" %
          node1.get_synchronous_standby_names())

    print("set number_sync_standbys = 1")
    assert node1.set_number_sync_standbys(1)
    assert node1.get_number_sync_standbys() == 1
    print("synchronous_standby_names = '%s'" %
          node1.get_synchronous_standby_names())
