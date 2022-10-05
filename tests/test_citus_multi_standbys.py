import tests.pgautofailover_utils as pgautofailover
from nose.tools import eq_, raises

import os.path
import time
import subprocess

cluster = None
monitor = None
coordinator1a = None
coordinator1b = None
coordinator1c = None
worker1a = None
worker1b = None
worker1c = None
worker2a = None
worker2b = None
worker2c = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    # make sure all Citus nodes are in-sync before DROP TABLE
    coordinator1a.run_sql_query("select public.wait_until_metadata_sync()")
    coordinator1a.run_sql_query("DROP TABLE t1")
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/citus/multi/monitor")
    monitor.run()


def test_001_init_coordinator():
    global coordinator1a
    global coordinator1b
    global coordinator1c

    coordinator1a = cluster.create_datanode(
        "/tmp/citus/multi/coordinator1a", role=pgautofailover.Role.Coordinator
    )
    coordinator1a.create(name="coord0a")
    coordinator1a.run()

    assert coordinator1a.wait_until_state(target_state="single")

    # we need to expose some Citus testing internals
    assert coordinator1a.wait_until_pg_is_running()
    coordinator1a.create_wait_until_metadata_sync()

    # check that Citus did skip creating the SSL certificates (--no-ssl)
    assert not os.path.isfile(
        os.path.join("/tmp/citus/multi/coordinator1a", "server.key")
    )

    coordinator1b = cluster.create_datanode(
        "/tmp/citus/multi/coordinator1b", role=pgautofailover.Role.Coordinator
    )
    coordinator1b.create(name="coord0b")
    coordinator1b.run()

    assert coordinator1a.wait_until_state(target_state="primary")
    assert coordinator1b.wait_until_state(target_state="secondary")

    coordinator1c = cluster.create_datanode(
        "/tmp/citus/multi/coordinator1c", role=pgautofailover.Role.Coordinator
    )
    coordinator1c.create(
        name="coord0c", replicationQuorum=False, candidatePriority=0
    )

    coordinator1c.run()

    assert coordinator1a.wait_until_state(target_state="primary")
    assert coordinator1b.wait_until_state(target_state="secondary")
    assert coordinator1c.wait_until_state(target_state="secondary")

    eq_(coordinator1c.get_candidate_priority(), 0)


def test_002_init_workers():
    global worker1a
    global worker1b
    global worker1c
    global worker2a
    global worker2b
    global worker2c

    print()
    worker1a = cluster.create_datanode(
        "/tmp/citus/multi/worker1a", group=1, role=pgautofailover.Role.Worker
    )
    worker1a.create(name="worker1a")
    worker1a.run()

    worker1b = cluster.create_datanode(
        "/tmp/citus/multi/worker1b", group=1, role=pgautofailover.Role.Worker
    )
    worker1b.create(name="worker1b")
    worker1b.run()

    worker1c = cluster.create_datanode(
        "/tmp/citus/multi/worker1c", group=1, role=pgautofailover.Role.Worker
    )
    worker1c.create(
        name="worker1c", replicationQuorum=False, candidatePriority=0
    )
    worker1c.run()

    eq_(worker1c.get_candidate_priority(), 0)

    worker2a = cluster.create_datanode(
        "/tmp/citus/multi/worker2a", group=2, role=pgautofailover.Role.Worker
    )
    worker2a.create(name="worker2a")
    worker2a.run()

    worker2b = cluster.create_datanode(
        "/tmp/citus/multi/worker2b", group=2, role=pgautofailover.Role.Worker
    )
    worker2b.create(name="worker2b")
    worker2b.run()

    worker2c = cluster.create_datanode(
        "/tmp/citus/multi/worker2c", group=2, role=pgautofailover.Role.Worker
    )
    worker2c.create(
        name="worker2c", replicationQuorum=False, candidatePriority=0
    )
    worker2c.run()

    eq_(worker2c.get_candidate_priority(), 0)

    # wait here till all workers are stable and in the desired state
    # by not waiting on all workers separately this should save a bit of time
    print()  # make the debug output more readable

    monitor.print_state()

    assert worker1a.wait_until_state(target_state="primary")
    assert worker1b.wait_until_state(target_state="secondary")
    assert worker1c.wait_until_state(target_state="secondary")

    assert worker2a.wait_until_state(target_state="primary")
    assert worker2b.wait_until_state(target_state="secondary")
    assert worker2c.wait_until_state(target_state="secondary")

    ssn1 = "ANY 1 (pgautofailover_standby_5)"
    eq_(worker1a.get_synchronous_standby_names(), ssn1)
    eq_(worker1a.get_synchronous_standby_names_local(), ssn1)

    ssn2 = "ANY 1 (pgautofailover_standby_8)"
    eq_(worker2a.get_synchronous_standby_names(), ssn2)
    eq_(worker2a.get_synchronous_standby_names_local(), ssn2)


def test_003_001_create_distributed_table():
    # reduce number of shards to make it easier to test data sets on workers
    coordinator1a.run_sql_query(
        "ALTER DATABASE citus SET citus.shard_count TO 4"
    )
    coordinator1a.run_sql_query("CREATE TABLE t1 (a int)")
    coordinator1a.run_sql_query("SELECT create_distributed_table('t1', 'a')")
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

    results = coordinator1a.run_sql_query("TABLE t1 ORDER BY a")
    eq_(results, [(1,), (2,)])


def test_003_002_all_workers_have_data():
    # test that workers with replicationQuorum false still got the data
    q1 = "SELECT a FROM t1_102008 UNION ALL SELECT a FROM t1_102010 ORDER BY a"
    r1 = [(1,)]

    results = worker1a.run_sql_query(q1)
    eq_(results, r1)

    results = worker1b.run_sql_query(q1)
    eq_(results, r1)

    results = worker1c.run_sql_query(q1)
    eq_(results, r1)

    q1 = "SELECT a FROM t1_102009 UNION ALL SELECT a FROM t1_102011 ORDER BY a"
    r2 = [(2,)]

    results = worker2a.run_sql_query(q1)
    eq_(results, r2)

    results = worker2b.run_sql_query(q1)
    eq_(results, r2)

    results = worker2c.run_sql_query(q1)
    eq_(results, r2)


def test_004_001_fail_worker2():
    worker2a.fail()


@raises(Exception)
def test_004_002_writes_via_coordinator_to_worker2_fail():
    # value 3 is routed to the worker2 pair, which we just failed and
    # didn't had time to fail over yet. This will give an error due
    # to the failure of citus to contact the worker that just failed
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_004_003_reads_for_worker1_via_coordinator_work():
    # value 1 is routed to the worker1 pair. Since there is no failure
    # in this pair the coordinator is able to read the information
    results = coordinator1a.run_sql_query("SELECT a FROM t1 WHERE a = 1")
    eq_(results, [(1,)])


def test_004_004_wait_for_failover():
    print()  # make the debug output more readable
    assert worker2b.wait_until_state(target_state="wait_primary")
    assert worker2c.wait_until_state(target_state="secondary")

    eq_(worker2c.get_candidate_priority(), 0)

    ssn = ""
    eq_(worker2b.get_synchronous_standby_names(), ssn)
    eq_(worker2b.get_synchronous_standby_names_local(), ssn)


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


def test_007_start_worker2a_again():
    worker2a.run()

    print()  # make the debug output more readable
    assert worker2a.wait_until_state(target_state="secondary")
    assert worker2b.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_7)"
    eq_(worker2b.get_synchronous_standby_names(), ssn)
    eq_(worker2b.get_synchronous_standby_names_local(), ssn)


def test_008_read_from_workers_via_coordinator():
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_009_fail_worker2b():
    worker2b.fail()

    print()  # make the debug output more readable
    assert worker2a.wait_until_state(target_state="wait_primary")
    assert worker2c.wait_until_state(target_state="secondary")

    ssn = ""
    eq_(worker2a.get_synchronous_standby_names(), ssn)
    eq_(worker2a.get_synchronous_standby_names_local(), ssn)


def test_010_read_from_workers_via_coordinator():
    # after failing back to worker2a verify the value written to worker2b
    # has been safely replicated to worker2a
    results = coordinator1a.run_sql_query("SELECT a FROM t1 ORDER BY a ASC")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_011_start_worker2b_again():
    worker2b.run()

    print()  # make the debug output more readable
    assert worker2b.wait_until_state(target_state="secondary")
    assert worker2a.wait_until_state(target_state="primary")

    ssn = "ANY 1 (pgautofailover_standby_8)"
    eq_(worker2a.get_synchronous_standby_names(), ssn)
    eq_(worker2a.get_synchronous_standby_names_local(), ssn)
