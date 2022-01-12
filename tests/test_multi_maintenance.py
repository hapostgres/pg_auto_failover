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


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/multi_maintenance/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/multi_maintenance/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")


def test_002_add_standby():
    global node2

    node2 = cluster.create_datanode("/tmp/multi_maintenance/node2")
    node2.create()
    node2.run()

    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")

    node1.check_synchronous_standby_names(
        ssn="ANY 1 (pgautofailover_standby_2)"
    )


def test_003_add_standby():
    global node3

    node3 = cluster.create_datanode("/tmp/multi_maintenance/node3")
    node3.create()
    node3.run()

    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to be set to 1 now
    eq_(node1.get_number_sync_standbys(), 1)

    ssn = "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
    node1.check_synchronous_standby_names(ssn)

    # make sure we reached primary on node1 before next tests
    assert node1.wait_until_state(target_state="primary")


def test_004_write_into_primary():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2), (3), (4)")
    node1.run_sql_query("CHECKPOINT")

    results = node1.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]


def test_005_set_candidate_priorities():
    print()
    assert node1.wait_until_state(target_state="primary")

    # set priorities in a way that we know the candidate: node3
    node1.set_candidate_priority(80)  # current primary
    node2.set_candidate_priority(70)  # remain secondary
    node3.set_candidate_priority(90)  # favorite for failover

    # when we set candidate priority we go to apply_settings then primary
    print()
    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="secondary")

    # node2 should still be "sync"
    eq_(node2.get_replication_quorum(), True)

    # other replication settings should still be the same as before
    eq_(node1.get_number_sync_standbys(), 1)

    ssn = "ANY 1 (pgautofailover_standby_3, pgautofailover_standby_2)"
    node1.check_synchronous_standby_names(ssn)


def test_006a_maintenance_and_failover():
    print()
    print("Enabling maintenance on node2")
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")
    node2.stop_postgres()

    # assigned and goal state must be the same
    assert node1.wait_until_state(target_state="primary")

    # ssn is not changed during maintenance operations
    ssn = "ANY 1 (pgautofailover_standby_3, pgautofailover_standby_2)"
    eq_(node1.get_synchronous_standby_names(), ssn)
    eq_(node1.get_synchronous_standby_names_local(), ssn)

    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node3.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")

    # now that node3 is primary, synchronous_standby_names has changed
    ssn = "ANY 1 (pgautofailover_standby_1, pgautofailover_standby_2)"
    node3.check_synchronous_standby_names(ssn)

    print("Disabling maintenance on node2, should connect to the new primary")
    node2.disable_maintenance()

    # allow manual checking of primary_conninfo
    primary_conninfo_ipaddr = str(node3.vnode.address)
    print("current primary is node3 at %s" % primary_conninfo_ipaddr)

    if node2.pgmajor() < 12:
        fn = "recovery.conf"
    else:
        fn = "postgresql-auto-failover-standby.conf"

    fn = os.path.join(node2.datadir, fn)
    conf = open(fn).read()

    if primary_conninfo_ipaddr not in conf:
        raise Exception(
            "Primary ip address %s not found in %s:\n%s"
            % (primary_conninfo_ipaddr, fn, conf)
        )

    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")

    # ssn is not changed during maintenance operations
    node3.check_synchronous_standby_names(ssn)

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()


def test_006b_read_from_new_primary():
    results = node3.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]


def test_007a_node1_to_maintenance():
    print()
    assert node3.wait_until_state(target_state="primary")

    print("Enabling maintenance on node1")
    node1.enable_maintenance()

    assert node3.wait_until_state(target_state="primary")


def test_007b_node2_to_maintenance():
    # node3 is the current primary
    assert node3.get_number_sync_standbys() == 1

    print("Enabling maintenance on node2")
    node2.enable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    # when both secondaries are put to maintenance, writes are blocked on
    # the primary
    ssn = "ANY 1 (pgautofailover_standby_1, pgautofailover_standby_2)"
    node3.check_synchronous_standby_names(ssn)


def test_008a_stop_primary():
    # node3 is the current primary
    assert node3.get_state().assigned == "primary"
    node3.fail()

    # check that even after 30s node3 is still not set to draining
    node3.sleep(30)
    assert not node3.get_state().assigned == "draining"
    assert node3.get_state().assigned == "primary"


def test_008b_start_primary():
    node3.run()
    assert node3.wait_until_state(target_state="primary")


@raises(Exception)
def test_009a_enable_maintenance_on_primary_should_fail():
    node3.enable_maintenance(allowFailover=True)


def test_009b_disable_maintenance():
    print("Disabling maintenance on node1 and node2")
    node1.disable_maintenance()
    node2.disable_maintenance()

    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")


def test_010_set_number_sync_standby_to_zero():
    assert node3.set_number_sync_standbys(0)
    eq_(node3.get_number_sync_standbys(), 0)


def test_011_all_to_maintenance():
    print()
    assert node3.wait_until_state(target_state="primary")

    print("Enabling maintenance on node1")
    node1.enable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    # now we can, because we don't care about having any standbys
    print("Enabling maintenance on node2")
    node2.enable_maintenance()

    assert node3.wait_until_state(target_state="wait_primary")

    # also let's see synchronous_standby_names here
    node3.check_synchronous_standby_names(ssn="")


def test_012_can_write_during_maintenance():
    node3.run_sql_query("INSERT INTO t1 VALUES (5), (6)")
    node3.run_sql_query("CHECKPOINT")


def test_013_add_standby():
    global node4

    node4 = cluster.create_datanode("/tmp/multi_maintenance/node4")
    node4.create()
    node4.run()

    assert node4.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="maintenance")
    assert node1.wait_until_state(target_state="maintenance")

    assert node3.has_needed_replication_slots()
    assert node4.has_needed_replication_slots()

    # the formation number_sync_standbys is expected to not be changed
    eq_(node3.get_number_sync_standbys(), 0)

    # make sure we reached primary on node1 before next tests
    assert node3.wait_until_state(target_state="primary")


def test_014_disable_maintenance():
    print()
    # make sure node2 is still in maintenance, then disable maintenance
    print("Disabling maintenance on node2")
    assert node2.wait_until_state(target_state="maintenance")
    node2.disable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    print("Disabling maintenance on node1")
    # make sure node1 is still in maintenance, then disable maintenance
    assert node1.wait_until_state(target_state="maintenance")
    node1.disable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    # also let's see synchronous_standby_names here
    print("Monitor: %s" % node3.get_synchronous_standby_names())
    print(
        "Node 3:  %s"
        % node3.run_sql_query("show synchronous_standby_names")[0][0]
    )


def test_015_set_number_sync_standby_to_one():
    node3.set_number_sync_standbys(1)
    eq_(node3.get_number_sync_standbys(), 1)

    assert node3.wait_until_state(target_state="primary")


def test_016_two_standbys_in_maintenance():
    print()

    print("Enabling maintenance on node1")
    node1.enable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    print("Enabling maintenance on node2")
    node2.enable_maintenance()

    assert node3.wait_until_state(target_state="primary")


@raises(Exception)
def test_017_primary_to_maintenance():
    # this should fail, because we have 4 nodes and number_sync_standbys = 1
    # and 2 nodes are already in maintenance
    #
    # if we allowed a 3rd node to be in maintenance, we would have no
    # standby node left and 0 < 1
    print()
    print("Enabling maintenance on node3 (primary)")
    node3.enable_maintenance()


def test_018_disable_maintenance():
    print()
    print("Disabling maintenance on node2")
    assert node2.wait_until_state(target_state="maintenance")
    node2.disable_maintenance()

    assert node3.wait_until_state(target_state="primary")

    # make sure node1 is still in maintenance, then disable maintenance
    print("Disabling maintenance on node1")
    assert node1.wait_until_state(target_state="maintenance")
    node1.disable_maintenance()

    assert node3.wait_until_state(target_state="primary")


def test_019_set_priorities():
    # set priorities in a way that we know the candidate: node1
    node1.set_candidate_priority(90)
    node2.set_candidate_priority(70)
    node3.set_candidate_priority(70)  # current primary
    node4.set_candidate_priority(70)

    # when we set candidate priority we go to apply_settings then primary
    print()
    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")
    assert node4.wait_until_state(target_state="secondary")


def test_020_primary_to_maintenance():
    print()
    assert node3.wait_until_state(target_state="primary")

    print("Enabling maintenance on node3, allowing failover")
    node3.enable_maintenance(allowFailover=True)

    assert node3.wait_until_state(target_state="maintenance")
    assert node2.wait_until_state(target_state="secondary")
    assert node4.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_021_stop_maintenance():
    print()
    print("Disabling maintenance on node3")
    node3.disable_maintenance()

    assert node3.wait_until_pg_is_running()
    assert node3.wait_until_state(target_state="secondary")

    assert node1.wait_until_state(target_state="primary")

    assert node2.wait_until_state(target_state="secondary")
    assert node4.wait_until_state(target_state="secondary")
