import pgautofailover_utils as pgautofailover
from nose.tools import eq_, raises

import os.path
import time
import subprocess

cluster = None
monitor = None
coordinator1a = None
coordinator1b = None
# we will be creating a citus cluster with 2 workers
# each has a secondary for replication. Workers are numbered
# a/b indicates primary/secondary (only true before first failover)
# a/b swap around primary and secondary roles when failures are induced
worker1a = None
worker1b = None
worker2a = None
worker2b = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    coordinator1b.run_sql_query("DROP TABLE t1")
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/citus/basic/monitor")
    monitor.run()


def test_001_init_coordinator():
    global coordinator1a
    global coordinator1b

    coordinator1a = cluster.create_datanode(
        "/tmp/citus/basic/coordinator1a", role=pgautofailover.Role.Coordinator
    )
    coordinator1a.create()
    coordinator1a.run()

    assert coordinator1a.wait_until_state(target_state="single")

    # check that Citus did skip creating the SSL certificates (--no-ssl)
    assert not os.path.isfile(
        os.path.join("/tmp/citus/basic/coordinator1a", "server.key")
    )

    coordinator1b = cluster.create_datanode(
        "/tmp/citus/basic/coordinator1b", role=pgautofailover.Role.Coordinator
    )
    coordinator1b.create()
    coordinator1b.run()
    assert coordinator1a.wait_until_state(target_state="primary")
    assert coordinator1b.wait_until_state(target_state="secondary")


def test_002_init_workers():
    global worker1a
    global worker1b
    global worker2a
    global worker2b

    worker1a = cluster.create_datanode(
        "/tmp/citus/basic/worker1a", group=1, role=pgautofailover.Role.Worker
    )
    worker1a.create(name="worker1a")
    worker1a.run()

    worker1b = cluster.create_datanode(
        "/tmp/citus/basic/worker1b", group=1, role=pgautofailover.Role.Worker
    )
    worker1b.create(name="worker1b")
    worker1b.run()

    worker2a = cluster.create_datanode(
        "/tmp/citus/basic/worker2a", group=2, role=pgautofailover.Role.Worker
    )
    worker2a.create(name="worker2a")
    worker2a.run()

    worker2b = cluster.create_datanode(
        "/tmp/citus/basic/worker2b", group=2, role=pgautofailover.Role.Worker
    )
    worker2b.create(name="worker2b")
    worker2b.run()

    # wait here till all workers are stable and in the desired state
    # by not waiting on all workers separately this should save a bit of time
    print()  # make the debug output more readable
    assert worker1a.wait_until_state(target_state="primary")
    assert worker1b.wait_until_state(target_state="secondary")
    assert worker2a.wait_until_state(target_state="primary")
    assert worker2b.wait_until_state(target_state="secondary")


def test_003_create_distributed_table():
    coordinator1a.run_sql_query("CREATE TABLE t1 (a int)")
    coordinator1a.run_sql_query("SELECT create_distributed_table('t1', 'a')")
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_004_001_fail_worker2():
    worker2a.fail()


@raises(Exception)
def test_004_002_writes_via_coordinator_to_worker2_fail():
    # value 3 is routed to the worker2 pair, which we just failed and
    # didn't had time to fail over yet. This will give an error due
    # to the failure of citus to contact the worker that just failed
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_004_003_reads_for_worker1_via_coordinator_work():
    # value 2 is routed to the worker1 pair. Since there is no failure
    # in this pair the coordinator is able to read the information
    results = coordinator1a.run_sql_query("SELECT a FROM t1 WHERE a = 2")
    eq_(results, [(2,)])


def test_004_004_wait_for_failover():
    print()  # make the debug output more readable
    assert worker2b.wait_until_state(target_state="wait_primary")


def test_004_005_writes_via_coordinator_to_worker2_succeed_after_failover():
    # same as test 004_002, but now that the failover has been performed
    # the value can be correctly written to worker2b (worker2a is still dead)
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_005_read_from_workers_via_coordinator():
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,)])


def test_006_writes_to_coordinator_succeed():
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (4)")
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_007_start_worker2_again():
    worker2a.run()

    print()  # make the debug output more readable
    assert worker2b.wait_until_state(target_state="primary")
    assert worker2a.wait_until_state(target_state="secondary")

    ssn = worker2b.run_sql_query("show synchronous_standby_names")[0][0]
    eq_(ssn, "ANY 1 (pgautofailover_standby_5)")


def test_008_read_from_workers_via_coordinator():
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_009_fail_worker2b():
    worker2b.fail()

    print()  # make the debug output more readable
    assert worker2a.wait_until_state(target_state="wait_primary")


def test_010_read_from_workers_via_coordinator():
    # after failing back to worker2a verify the value written to worker2b
    # has been safely replicated to worker2a
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_011_start_worker2b_again():
    worker2b.run()

    print()
    assert worker2b.wait_until_state(target_state="secondary")
    assert worker2a.wait_until_state(target_state="primary")


def test_012_perform_failover_worker2():
    print()

    print("Calling pgautofailover.failover(group => 2) on the monitor")
    cluster.monitor.failover(group=2)
    assert worker2a.wait_until_state(target_state="secondary")
    assert worker2b.wait_until_state(target_state="primary")

    assert worker2a.has_needed_replication_slots()
    assert worker2b.has_needed_replication_slots()


# we had a bug where if the old primary has already reached draining (both
# assigned and reported the state) when the secondary starts the transition
# from secondary to prepare_promotion, then the secondary transition fails
# because calling pgautofailover.get_primary() on the monitor returned no
# rows.
#
# that's fixed by not calling pgautofailover.get_primary() in the
# transition, it can be avoided easily.
#
def test_013_perform_failover_worker2b_draining():
    print()

    worker2a.stop_pg_autoctl()

    print("Calling pgautofailover.failover(group => 2) on the monitor")
    cluster.monitor.failover(group=2)
    assert worker2b.wait_until_state(target_state="draining")

    worker2a.run()

    assert worker2b.wait_until_state(target_state="secondary")
    assert worker2a.wait_until_state(target_state="primary")

    assert worker2a.has_needed_replication_slots()
    assert worker2b.has_needed_replication_slots()


def test_014_perform_failover_coordinator():
    print()

    print("Calling pgautofailover.failover(group => 0) on the monitor")
    cluster.monitor.failover(group=0)
    assert coordinator1a.wait_until_state(target_state="secondary")
    assert coordinator1b.wait_until_state(target_state="primary")

    assert coordinator1a.has_needed_replication_slots()
    assert coordinator1b.has_needed_replication_slots()
