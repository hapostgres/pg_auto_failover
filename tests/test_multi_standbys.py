import tests.pgautofailover_utils as pgautofailover
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
node6 = None


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


def test_004_001_add_three_standbys():
    # the next test wants to set number_sync_standbys to 2
    # so we need at least 3 standbys to allow that
    global node2

    node2 = cluster.create_datanode("/tmp/multi_standby/node2")
    node2.create()
    node2.run()

    assert node2.wait_until_state(target_state="secondary")

    assert node2.wait_until_pg_is_running()

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # with one standby, we have number_sync_standbys set to 0 still
    assert node1.get_number_sync_standbys() == 0


def test_004_002_add_three_standbys():
    global node3

    # refrain from waiting for the primary to be ready, to trigger a race
    # condition that could segfault the monitor (if the code was less
    # careful than it is now)
    # assert node1.wait_until_state(target_state="primary")

    node3 = cluster.create_datanode("/tmp/multi_standby/node3")
    node3.create()
    node3.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node3.wait_until_pg_is_running()

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to be set to 1 now
    assert node1.get_number_sync_standbys() == 1


def test_004_003_add_three_standbys():
    global node4

    node4 = cluster.create_datanode("/tmp/multi_standby/node4")
    node4.create()
    node4.run()

    assert node4.wait_until_state(target_state="secondary")

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")

    assert node4.wait_until_pg_is_running()

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

    ssn = "ANY 2 (pgautofailover_standby_2, pgautofailover_standby_3, pgautofailover_standby_4)"
    node1.check_synchronous_standby_names(ssn)

    print("set number_sync_standbys = 0")
    assert node1.set_number_sync_standbys(0)
    assert node1.get_number_sync_standbys() == 0

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3, pgautofailover_standby_4)"
    node1.check_synchronous_standby_names(ssn)

    print("set number_sync_standbys = 1")
    assert node1.set_number_sync_standbys(1)
    assert node1.get_number_sync_standbys() == 1

    # same ssn as before
    eq_(node1.get_synchronous_standby_names(), ssn)
    eq_(node1.get_synchronous_standby_names_local(), ssn)


def test_006_number_sync_standbys_trigger():
    assert node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 2

    node4.drop()

    assert node1.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)

    # there's no state change to instruct us that the replication slot
    # maintenance is now done, so we have to wait for awhile instead.

    node1.pg_autoctl.sighup()  # wake up from the 10s node_active delay
    node2.pg_autoctl.sighup()  # wake up from the 10s node_active delay
    node3.pg_autoctl.sighup()  # wake up from the 10s node_active delay

    time.sleep(6)

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()


def test_007_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")
    node1.run_sql_query("CHECKPOINT")


def test_008_set_candidate_priorities():
    # set priorities in a way that we know the candidate: node2
    node1.set_candidate_priority(90)  # current primary
    node2.set_candidate_priority(90)
    node3.set_candidate_priority(70)

    print()
    assert node1.wait_until_state(target_state="primary")


def test_009_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node2.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="secondary")

    ssn = "ANY 1 (pgautofailover_standby_1, pgautofailover_standby_3)"
    node2.check_synchronous_standby_names(ssn)

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

    # generate more WAL traffic for replication
    node2.run_sql_query("CHECKPOINT")


def test_012_fail_primary():
    print()
    print("Failing current primary node 2")
    node2.fail()

    # explicitly allow for the 30s timeout in stop_replication
    assert node1.wait_until_state(target_state="stop_replication")
    assert node1.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


def test_013_restart_node2():
    node2.run()

    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="secondary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


#
# When the two standby nodes are lost and then assigned catchingup, and the
# primary is now blocking writes, we can set number-sync-standbys to 0 to
# unblock writes, causing the primary to reach wait_primary
#
def test_014_001_fail_set_properties():
    eq_(node1.get_number_sync_standbys(), 1)

    node1.set_candidate_priority(50)
    node2.set_candidate_priority(50)
    node3.set_candidate_priority(50)

    node1.wait_until_state(target_state="primary")

    assert node1.get_replication_quorum()
    assert node2.get_replication_quorum()
    assert node3.get_replication_quorum()


def test_014_002_fail_two_standby_nodes():
    node2.fail()
    node3.fail()

    node2.wait_until_assigned_state(target_state="catchingup")
    node3.wait_until_assigned_state(target_state="catchingup")

    # node1 remains a primary, blocking writes, at this stage
    node1.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


def test_014_003_unblock_writes():
    node1.set_number_sync_standbys(0)

    # node1 unblocks writes because number-sync-standbys is now zero
    node1.wait_until_state(target_state="wait_primary")
    eq_(node1.get_number_sync_standbys(), 0)

    node1.check_synchronous_standby_names(ssn="")


def test_014_004_restart_nodes():
    node3.run()
    node2.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


#
# Now if number-sync-standbys is zero already, then when we lose all the
# standby nodes the primary is assigned wait_primary to unblock writes
#
def test_015_001_set_properties():
    node1.wait_until_state(target_state="primary")

    eq_(node1.get_number_sync_standbys(), 0)


def test_015_002_fail_two_standby_nodes():
    node2.fail()
    node3.fail()

    # node 1 is assigned wait_primary as soon as we lose all the candidates
    node1.wait_until_state(target_state="wait_primary")

    ssn = ""
    eq_(node1.get_synchronous_standby_names(), ssn)
    eq_(node1.get_synchronous_standby_names_local(), ssn)


def test_015_003_set_properties():
    # stop the data leak by re-implementing sync rep
    #
    # the primary is now expected to be in apply_settings/primary, and fail
    # to reach primary, which causes the set number-sync-standbys command to
    # fail.
    #
    # instead of using the command line (which waits for 60s and fail),
    # let's use the monitor SQL API instead
    q = "select pgautofailover.set_formation_number_sync_standbys('default', 1)"
    monitor.run_sql_query(q)

    assert node1.wait_until_assigned_state(target_state="primary")

    eq_(node1.get_number_sync_standbys(), 1)

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


def test_015_004_restart_nodes():
    node3.run()
    node2.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)


#
# Now test a failover when all the nodes have candidate priority set to zero
#
def test_016_001_set_candidate_priorities_to_zero():
    node1.set_candidate_priority(0)
    node2.set_candidate_priority(0)
    node3.set_candidate_priority(0)

    # no candidate for failover, we're wait_primary
    node1.wait_until_state(target_state="primary")


def test_016_002_trigger_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()

    assert node3.wait_until_state(target_state="report_lsn")
    assert node2.wait_until_state(target_state="report_lsn")
    assert node1.wait_until_state(target_state="report_lsn")


def test_016_003_set_candidate_priority_to_one():
    node2.set_candidate_priority(1)

    # no candidate for failover, we're wait_primary
    node2.wait_until_state(target_state="primary")
    node1.wait_until_state(target_state="secondary")
    node3.wait_until_state(target_state="secondary")


def test_016_004_reset_candidate_priority():
    node2.set_candidate_priority(0)

    node2.wait_until_state(target_state="primary")
    node1.wait_until_state(target_state="secondary")
    node3.wait_until_state(target_state="secondary")


def test_016_005_perform_promotion():
    print()
    print("Calling pg_autoctl perform promotion on node 1")
    node1.perform_promotion()

    node1.wait_until_state(target_state="primary")
    node2.wait_until_state(target_state="secondary")
    node3.wait_until_state(target_state="secondary")


def test_017_remove_old_primary():
    node2.drop()

    assert node1.wait_until_state(target_state="primary")
    assert node3.wait_until_state(target_state="secondary")
