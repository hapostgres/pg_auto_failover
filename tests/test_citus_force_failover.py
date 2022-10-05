import tests.pgautofailover_utils as pgautofailover
import psycopg2
from nose.tools import *

ha_cluster = None
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
    global ha_cluster
    ha_cluster = pgautofailover.Cluster()


def teardown_module():
    coordinator1b.run_sql_query("select public.wait_until_metadata_sync()")
    coordinator1b.run_sql_query("DROP TABLE t1")
    ha_cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = ha_cluster.create_monitor("/tmp/citus/force/monitor")
    monitor.run()


def test_001_init_coordinator():
    global coordinator1a
    global coordinator1b

    coordinator1a = ha_cluster.create_datanode(
        "/tmp/citus/force/coordinator1a", role=pgautofailover.Role.Coordinator
    )
    coordinator1a.create(name="coord1a")
    coordinator1a.run()

    print()  # make the debug output more readable
    assert coordinator1a.wait_until_state(target_state="single")

    # we need to expose some Citus testing internals
    assert coordinator1a.wait_until_pg_is_running()
    coordinator1a.create_wait_until_metadata_sync()

    coordinator1b = ha_cluster.create_datanode(
        "/tmp/citus/force/coordinator1b", role=pgautofailover.Role.Coordinator
    )
    coordinator1b.create(name="coord1b")
    coordinator1b.run()
    assert coordinator1a.wait_until_state(target_state="primary")
    assert coordinator1b.wait_until_state(target_state="secondary")


def test_002_init_workers():
    global worker1a
    global worker1b
    global worker2a
    global worker2b

    worker1a = ha_cluster.create_datanode(
        "/tmp/citus/force/worker1a", group=1, role=pgautofailover.Role.Worker
    )
    worker1a.create(name="worker1a")
    worker1a.run()

    worker1b = ha_cluster.create_datanode(
        "/tmp/citus/force/worker1b", group=1, role=pgautofailover.Role.Worker
    )
    worker1b.create(name="worker1b")
    worker1b.run()

    worker2a = ha_cluster.create_datanode(
        "/tmp/citus/force/worker2a", group=2, role=pgautofailover.Role.Worker
    )
    worker2a.create(name="worker2a")
    worker2a.run()

    worker2b = ha_cluster.create_datanode(
        "/tmp/citus/force/worker2b", group=2, role=pgautofailover.Role.Worker
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
    coordinator1a.run_sql_query(
        """
    SET citus.shard_count TO 4;
    CREATE TABLE t1 (a int);
    SELECT create_distributed_table('t1', 'a');
    """
    )


def test_004_fail_while_transaction_is_in_progress():
    # can't use with here :( when using that it will throw an exception once the
    # connection gets closed due to the failover killing the competing backend
    conn = psycopg2.connect(coordinator1a.connection_string())
    cur = conn.cursor()
    # this becomes a multi worker query with locks held on the shards
    # these locks compete with the failover that will be triggered by worker1a.fail
    # if no progress is made on this transaction the failover stays blocked.
    # This is undesirable so the failover mechanism waits for a certain time before
    # aggressively terminating backends that keep competing locks that would prevent the
    # failvover from completing.
    cur.execute("INSERT INTO t1 VALUES (1), (2)")  # has nothing to fetch

    worker1a.fail()
    print()  # make the debug output more readable
    assert worker1b.wait_until_state(target_state="wait_primary")

    try:
        # should throw exception as the connection has been terminated
        cur.execute("INSERT INTO t1 VALUES (3)")
        raise Exception(
            "competing transaction should have been terminated during failover"
        )
    except psycopg2.OperationalError:
        # expected behaviour
        pass

    conn.close()


def test_005_drop_primary_worker():
    worker2a.fail()
    # can't drop a worker node with shard placements, wait until failover
    worker2b.wait_until_state("wait_primary")
    worker2a.drop()
    worker2b.wait_until_state("single")


def test_005_drop_primary_coordinator():
    coordinator1a.drop()
    coordinator1b.wait_until_state("single")
