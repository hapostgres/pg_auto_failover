import pgautofailover_utils as pgautofailover
import psycopg2
from nose.tools import *

ha_cluster = None
monitor = None
coordinator1a = None
coordinator1b = None

# we will be creating a citus cluster with 2 workers, each has a secondary
# for read-only queries
worker1a = None
worker1b = None
worker2a = None
worker2b = None


def setup_module():
    global ha_cluster
    ha_cluster = pgautofailover.Cluster()


def teardown_module():
    ha_cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = ha_cluster.create_monitor("/tmp/citus/read-only/monitor")
    monitor.run()


def test_001_init_coordinator():
    global coordinator1a
    global coordinator1b

    coordinator1a = ha_cluster.create_datanode(
        "/tmp/citus/read-only/coordinator1a",
        role=pgautofailover.Role.Coordinator,
    )
    coordinator1a.create()
    coordinator1a.run()

    print()  # make the debug output more readable
    assert coordinator1a.wait_until_state(target_state="single")

    coordinator1b = ha_cluster.create_datanode(
        "/tmp/citus/foce/coordinator1b", role=pgautofailover.Role.Coordinator
    )
    coordinator1b.create(
        candidatePriority=0, citusSecondary=True, citusClusterName="readonly"
    )
    coordinator1b.run()
    assert coordinator1a.wait_until_state(target_state="primary")
    assert coordinator1b.wait_until_state(target_state="secondary")


def test_002_001_init_workers():
    global worker1a
    global worker1b

    worker1a = ha_cluster.create_datanode(
        "/tmp/citus/read-only/worker1a",
        group=1,
        role=pgautofailover.Role.Worker,
    )
    worker1a.create()
    worker1a.run()

    worker1b = ha_cluster.create_datanode(
        "/tmp/citus/read-only/worker1b",
        group=1,
        role=pgautofailover.Role.Worker,
    )
    worker1b.create(
        candidatePriority=0, citusSecondary=True, citusClusterName="readonly"
    )
    worker1b.run()


def test_002_002_init_workers():
    global worker2a
    global worker2b

    worker2a = ha_cluster.create_datanode(
        "/tmp/citus/read-only/worker2a",
        group=2,
        role=pgautofailover.Role.Worker,
    )
    worker2a.create()
    worker2a.run()

    worker2b = ha_cluster.create_datanode(
        "/tmp/citus/read-only/worker2b",
        group=2,
        role=pgautofailover.Role.Worker,
    )
    worker2b.create(
        candidatePriority=0, citusSecondary=True, citusClusterName="readonly"
    )
    worker2b.run()


def test_002_003_wait_for_nodes():
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

    coordinator1a.run_sql_query(
        "INSERT INTO t1 SELECT x FROM generate_series(1, 1000) as gs(x)"
    )
    coordinator1a.run_sql_query("CHECKPOINT")


def test_004_read_only_cluster():
    query = """select current_setting('citus.use_secondary_nodes'),
    current_setting('citus.cluster_name')
    """
    ra = coordinator1a.run_sql_query(query)[0]
    rb = coordinator1b.run_sql_query(query)[0]

    eq_(ra, ("never", "default"))
    eq_(rb, ("always", "readonly"))

    count = coordinator1b.run_sql_query("select count(*) from t1")[0][0]
    eq_(count, 1000)


def test_005_drop_table():
    coordinator1a.run_sql_query("DROP TABLE t1")
