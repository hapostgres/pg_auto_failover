import tests.pgautofailover_utils as pgautofailover
from nose.tools import *

cluster = None
monitor = None
coordinator1a = None
coordinator1b = None
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
    monitor = cluster.create_monitor("/tmp/nonha/monitor")
    monitor.run()


def test_001_create_formation():
    global monitor

    monitor.create_formation("non-ha", kind="citus", secondary=False)


def test_002_init_coordinator():
    global coordinator1a

    coordinator1a = cluster.create_datanode(
        "/tmp/citus/nonha/coordinator1a",
        role=pgautofailover.Role.Coordinator,
        formation="non-ha",
    )
    coordinator1a.create()
    coordinator1a.run()

    print()  # make the debug output more readable
    assert coordinator1a.wait_until_state(target_state="single")


def test_003_init_workers():
    global worker1a
    global worker2a

    worker1a = cluster.create_datanode(
        "/tmp/citus/nonha/worker1a",
        role=pgautofailover.Role.Worker,
        group=1,
        formation="non-ha",
    )
    worker1a.create()
    worker1a.run()

    worker2a = cluster.create_datanode(
        "/tmp/citus/nonha/worker2a",
        role=pgautofailover.Role.Worker,
        group=2,
        formation="non-ha",
    )
    worker2a.create()
    worker2a.run()

    # wait here till all workers are stable and in the desired state
    # by not waiting on all workers separately this should save a bit of time
    print()  # make the debug output more readable
    assert worker1a.wait_until_state(target_state="single")
    assert worker2a.wait_until_state(target_state="single")


def test_004_create_distributed_table():
    assert coordinator1a.wait_until_pg_is_running()
    coordinator1a.run_sql_query("CREATE TABLE t1 (a int)")
    coordinator1a.run_sql_query("SELECT create_distributed_table('t1', 'a')")
    coordinator1a.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_005_enable_secondary():
    global monitor
    monitor.enable(pgautofailover.Feature.Secondary, formation="non-ha")


def test_006_add_secondaries():
    global coordinator1b
    global worker1b
    global worker2b

    coordinator1b = cluster.create_datanode(
        "/tmp/citus/nonha/coordinator1b",
        role=pgautofailover.Role.Coordinator,
        formation="non-ha",
    )
    coordinator1b.create()
    coordinator1b.run()

    worker1b = cluster.create_datanode(
        "/tmp/citus/nonha/worker1b",
        role=pgautofailover.Role.Worker,
        group=1,
        formation="non-ha",
    )
    worker1b.create()
    worker1b.run()

    worker2b = cluster.create_datanode(
        "/tmp/citus/nonha/worker2b",
        role=pgautofailover.Role.Worker,
        group=2,
        formation="non-ha",
    )
    worker2b.create()
    worker2b.run()
    # wait here till all workers are stable and in the desired state
    # by not waiting on all workers separately this should save a bit of time
    print()  # make the debug output more readable
    assert coordinator1b.wait_until_state(target_state="secondary")
    assert worker1b.wait_until_state(target_state="secondary")
    assert worker2b.wait_until_state(target_state="secondary")


@raises(Exception)
def test_007_fail_when_disabling_with_secondaries():
    global monitor
    monitor.disable(pgautofailover.Feature.Secondary, formation="non-ha")


def test_008_shutdown_primaries():
    print()  # make the debug output more readable
    coordinator1a.fail()
    assert coordinator1b.wait_until_state(target_state="wait_primary")

    worker1a.fail()
    assert worker1b.wait_until_state(target_state="wait_primary")

    worker2a.fail()
    assert worker2b.wait_until_state(target_state="wait_primary")


def test_009_remove_failed_nodes():
    coordinator1a.drop()
    worker1a.drop()
    worker2a.drop()

    print()  # make the debug output more readable
    assert coordinator1b.wait_until_state(target_state="single")
    assert worker1b.wait_until_state(target_state="single")
    assert worker2b.wait_until_state(target_state="single")


def test_010_disable_secondaries():
    global monitor
    monitor.disable(pgautofailover.Feature.Secondary, formation="non-ha")
