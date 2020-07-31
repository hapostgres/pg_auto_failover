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
    assert not node1.set_number_sync_standbys(-1)
    assert node1.get_number_sync_standbys() == 1

    print("set number_sync_standbys = 2")
    assert node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 2
    print("synchronous_standby_names = '%s'" %
          node1.get_synchronous_standby_names())

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

def test_006_number_sync_standbys_trigger():
    assert node1.set_number_sync_standbys(2)
    assert node1.get_number_sync_standbys() == 2

    node4.drop()
    assert node1.get_number_sync_standbys() == 1
    assert node1.wait_until_state(target_state="primary")

    # there's no state change to instruct us that the replication slot
    # maintenance is now done, so we have to wait for awhile instead.
    # pg_autoctl connects to the monitor every 5s, so let's sleep 6s
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

def test_012_set_candidate_priorities():
    print()
    assert node2.wait_until_state(target_state="primary")

    # set priorities in a way that we know the candidate: node3
    node1.set_candidate_priority(70)
    node2.set_candidate_priority(90) # current primary
    node3.set_candidate_priority(90)

    # when we set candidate priority we go to apply_settings then primary
    print()
    assert node2.wait_until_state(target_state="primary")

def test_013_maintenance_and_failover():
    print()
    print("Enabling maintenance on node1")
    node1.enable_maintenance()
    assert node1.wait_until_state(target_state="maintenance")
    node1.stop_postgres()

    # assigned and goal state must be the same
    assert node2.wait_until_state(target_state="join_primary")

    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node3.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")

    print("Disabling maintenance on node1, should connect to the new primary")
    node1.disable_maintenance()

    # allow manual checking of primary_conninfo
    print("current primary is node3 at %s" % str(node3.vnode.address))

    if node1.pgmajor() < 12:
        fn = "recovery.conf"
    else:
        fn = "postgresql-auto-failover-standby.conf"

    fn = os.path.join(node1.datadir, fn)
    print("%s:\n%s" % (fn, open(fn).read()))

    assert node1.wait_until_state(target_state="secondary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

def test_014_read_from_new_primary():
    results = node3.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,), (3,), (4,)]

def test_015_set_candidate_priorities():
    print()
    assert node3.wait_until_state(target_state="primary")

    node1.set_candidate_priority(90) # secondary
    node2.set_candidate_priority(0)  # not a candidate anymore
    node3.set_candidate_priority(90) # current primary

    # when we set candidate priority we go to apply_settings then primary
    print()
    assert node3.wait_until_state(target_state="primary")

    # node2 should still be "sync"
    assert node3.get_number_sync_standbys() == 1
    assert node2.get_replication_quorum()

    # also let's see synchronous_standby_names here
    print("Monitor: %s" % node3.get_synchronous_standby_names())
    print("Node 3:  %s" %
          node3.run_sql_query("show synchronous_standby_names")[0][0])

def test_016_ifdown_node1():
    node1.ifdown()

def test_017_insert_rows():
    node3.run_sql_query(
        "INSERT INTO t1 SELECT x+10 FROM generate_series(1, 100000) as gs(x)")
    node3.run_sql_query("CHECKPOINT")

    lsn3 = node3.run_sql_query("select pg_current_wal_lsn()")[0][0]
    print("%s " % lsn3, end="", flush=True)

    # node2 is sync and should get the WAL
    lsn2 = node2.run_sql_query("select pg_last_wal_receive_lsn()")[0][0]
    print("%s " % lsn2, end="", flush=True)

    while lsn2 != lsn3:
        time.sleep(1)
        lsn2 = node2.run_sql_query("select pg_last_wal_receive_lsn()")[0][0]
        print("%s " % lsn2, end="", flush=True)

    eq_(lsn3, lsn2)

def test_018_failover():
    print()
    print("Injecting failure of node3")
    node3.fail()

    # have node2 re-join the network and hopefully reconnect etc
    print("Reconnecting node1 (ifconfig up)")
    node1.ifup()

    # now we should be able to continue with the failover, and fetch missing
    # WAL bits from node2
    node1.wait_until_pg_is_running()
    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()

    assert node1.wait_until_state(target_state="wait_primary", timeout=120)
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_019_read_from_new_primary():
    results = node1.run_sql_query("SELECT count(*) FROM t1")
    assert results == [(100004,)]

def test_020_start_node3_again():
    node3.run()
    assert node3.wait_until_state(target_state="secondary")

    assert node1.wait_until_state(target_state="primary")
    assert node2.wait_until_state(target_state="secondary")

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

# test_021_XXX, test_022_XXX, test_023_XXX, test_024_XXX and test_025_XXX
# are meant to test the scenario when the most advanced secondary
# becomes inaccessible at the same time when the primary is inaccessible
def test_021_prepare_candidate_priorities():
    # we are aiming to promote node2
    assert node2.set_candidate_priority(100)  # the next primary

    # other nodes are already candidates for primary, but with less
    # priority
    assert node1.get_candidate_priority() == 90
    assert node3.get_candidate_priority() == 90

def test_022_prepare_replication_quorums():
    # for the purpose of this test, we need one node
    # async, to allow that we should decrement the sync stanbys
    node1.set_number_sync_standbys(0)

    # to emulate one node is behind, it is easier to make it async
    # we want node2 to be behind others
    assert node2.set_replication_quorum("false")

    # others should be sync
    assert node1.get_replication_quorum()
    assert node3.get_replication_quorum()

def test_023_secondary_gets_behind_primary():
    # make sure that node2 gets behind of the primary
    node2.ifdown()

    # primary ingests some data
    node1.run_sql_query("INSERT INTO t1 VALUES (5), (6)")
    node1.run_sql_query("CHECKPOINT")

    # ensure that the healthy secondary gets the change
    results = node3.run_sql_query("SELECT count(*) FROM t1")
    assert results == [(100006,)]

    lsn3 = node3.run_sql_query("select pg_last_wal_receive_lsn()")[0][0]
    print("%s " % lsn3, end="", flush=True)

    # ensure the monitor received this lsn
    time.sleep(2)
    q = "select reportedlsn from pgautofailover.node where nodeid = 3"
    lsn3m = monitor.run_sql_query(q)[0][0]
    print("%s " % lsn3m, end="", flush=True)

    eq_(lsn3, lsn3m)

def test_024_secondary_reports_lsn():
    # make the primary and mostAdvanced secondary inaccessible
    # and the candidate for failover as accessible
    # which means that node2 will not be able to fetch wal
    # and blocked until the other secondary is up
    node1.ifdown()    # primary
    node3.ifdown()    # most advanced standby
    node2.ifup()      # failover candidate

    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()

    # node2 reports its LSN while others are inaccessible
    assert node2.wait_until_state(target_state="report_lsn")

def test_025_finalize_failover_after_most_advanced_secondary_gets_back():
    # when they are accessible again, both should become
    # secondaries
    node3.ifup()    # old most advanced secondary, now secondary
    node1.ifup()    # old primary, now secodary

    # and, node2 should finally become the primary without losing any data
    assert node2.wait_until_state(target_state="wait_primary")

    print("%s" % monitor.pg_autoctl.err)

    results = node2.run_sql_query("SELECT count(*) FROM t1")
    eq_(results, [(100006,)])

    assert node1.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

# test_026 and test_027 aims to test the scenario when
# all secondaries are async
def test_026_prepare_replication_quorumans_and_priority():
    # for the purpose of this test, we need all nodes async
    eq_(node1.get_number_sync_standbys(), 0)

    # to emulate one node is behind, it is easier to make it async
    # we want node2 to be behind others
    eq_(node2.get_replication_quorum(), False)    # primary

    # when we set candidate priority we go to join_primary then primary
    print()
    assert node2.wait_until_state(target_state="primary")

    assert node1.set_replication_quorum("false")    # secondary
    assert node3.set_replication_quorum("false")    # secondary

    # to make the tests consistent, we assign higher
    # priorty to node1
    assert node2.set_candidate_priority(90)      # previous primary
    assert node1.set_candidate_priority(100)     # the next primary
    assert node3.get_candidate_priority() == 90  # secondary

    # when we set candidate priority we go to join_primary then primary
    print()
    assert node2.wait_until_state(target_state="primary")

def test_027_failover_when_all_nodes_async():

    print()
    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()

    assert node1.wait_until_state(target_state="wait_primary") # primary
    assert node2.wait_until_state(target_state="secondary")    # secondary
    assert node3.wait_until_state(target_state="secondary")    # secondary
    assert node1.wait_until_state(target_state="primary")      # primary