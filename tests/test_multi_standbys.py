import pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_
import time

import os.path

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None
node4 = None
node5 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/multi_standby/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/multi_standby/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_candidate_priority():
    assert node1.get_candidate_priority() == 50

    assert not node1.set_candidate_priority(-1)
    assert node1.get_candidate_priority() == 50

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

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # with one standby, we have number_sync_standbys set to 0 still
    assert node1.get_number_sync_standbys() == 0

    # refrain from waiting for the primary to be ready, to trigger a race
    # condition that could segfault the monitor (if the code was less
    # careful than it is now)
    # assert node1.wait_until_state(target_state="primary")

    node3 = cluster.create_datanode("/tmp/multi_standby/node3")
    node3.create()
    node3.run()
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to be set to 1 now
    assert node1.get_number_sync_standbys() == 1

    node4 = cluster.create_datanode("/tmp/multi_standby/node4")
    node4.create()
    node4.run()
    assert node4.wait_until_state(target_state="secondary")

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()
    assert node4.has_needed_replication_slots()

def test_005_number_sync_standbys():
    print()

    assert node1.get_number_sync_standbys() == 1
    node1.set_number_sync_standbys(-1)
    assert node1.get_number_sync_standbys() == 1

    print("set number_sync_standbys = 2")
    assert node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 2

    node1.print_synchronous_standby_names()
    eq_(node1.get_synchronous_standby_names(),
        node1.get_synchronous_standby_names_local())

    print("set number_sync_standbys = 0")
    assert node1.set_number_sync_standbys(0)
    assert node1.get_number_sync_standbys() == 0

    node1.print_synchronous_standby_names()
    eq_(node1.get_synchronous_standby_names(),
        node1.get_synchronous_standby_names_local())

    print("set number_sync_standbys = 1")
    assert node1.set_number_sync_standbys(1)
    assert node1.get_number_sync_standbys() == 1

    node1.print_synchronous_standby_names()
    eq_(node1.get_synchronous_standby_names(),
        node1.get_synchronous_standby_names_local())

def test_006_number_sync_standbys_trigger():
    assert node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 2

    node4.drop()

    # check synchronous_standby_names
    node1.print_synchronous_standby_names()
    eq_(node1.get_synchronous_standby_names(),
        node1.get_synchronous_standby_names_local())

    assert node1.wait_until_state(target_state="primary")

    # there's no state change to instruct us that the replication slot
    # maintenance is now done, so we have to wait for awhile instead.

    node1.pg_autoctl.sighup() # wake up from the 10s node_active delay
    node2.pg_autoctl.sighup() # wake up from the 10s node_active delay
    node3.pg_autoctl.sighup() # wake up from the 10s node_active delay

    time.sleep(6)

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

def test_007_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")
    node1.run_sql_query("CHECKPOINT")

@raises(Exception)
def test_008a_set_candidate_priorities_to_zero():
    # we need two nodes with non-zero candidate priority
    node1.set_candidate_priority(0)
    node2.set_candidate_priority(0)

def test_008b_set_candidate_priorities():
    # set priorities in a way that we know the candidate: node2
    node1.set_candidate_priority(90) # current primary
    node2.set_candidate_priority(90)
    node3.set_candidate_priority(70)

    # when we set candidate priority we go to join_primary then primary
    print()
    assert node1.wait_until_state(target_state="primary")

def test_009_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node2.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="secondary")

    eq_(node2.get_synchronous_standby_names(),
        node2.get_synchronous_standby_names_local())

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

def test_010_read_from_nodes():
    assert node1.run_sql_query("SELECT * FROM t1") == [(1,), (2,)]
    assert node2.run_sql_query("SELECT * FROM t1") == [(1,), (2,)]
    assert node3.run_sql_query("SELECT * FROM t1") == [(1,), (2,)]

def test_011_write_into_new_primary():
    node2.run_sql_query("INSERT INTO t1 VALUES (3), (4)")
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]

    # generate more WAL trafic for replication
    node2.run_sql_query("CHECKPOINT")

def test_012_fail_primary():
    print()
    print("Failing current primary node 2")
    node2.fail()

    assert node1.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")

    eq_(node1.get_synchronous_standby_names(),
        node1.get_synchronous_standby_names_local())

def test_013_remove_old_primary():
    node2.drop()

    assert node1.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")

def test_014_update_sync_standby_names_when_adding_and_removing_a_standby_at_same_time():
    global node5
    node5 = cluster.create_datanode("/tmp/multi_standby/node5")

    # Helpful aliases
    primary = node1
    new_secondary = node5

    # Push the primary into JOIN_PRIMARY state by adding a new secondary.
    new_secondary.create()
    new_secondary.run()

    # XXX This suffers from a race condition: the primary may switch away from
    # JOIN_PRIMARY before we are able to wait for it, in which case we'll fail
    # the test.
    assert primary.wait_until_state(target_state="join_primary")

    # Drop another secondary.
    node3.drop()
    assert primary.wait_until_state(target_state="primary")

    monitor_sync_standbys = primary.get_synchronous_standby_names()
    primary_sync_standbys = primary.get_synchronous_standby_names_local()

    eq_(monitor_sync_standbys, primary_sync_standbys)
