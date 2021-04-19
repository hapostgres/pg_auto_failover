import pgautofailover_utils as pgautofailover
import os.path

cluster = None
monitor = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/debian/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()


def test_001_custom_single():
    global node1

    print()
    node1_path = cluster.pg_createcluster("debian_node1", port=6001)

    postgres_conf_path = os.path.join(node1_path, "postgresql.conf")
    # verify postgresql.conf is not in data directory
    assert not os.path.exists(postgres_conf_path)

    node1 = cluster.create_datanode(node1_path, port=6001, listen_flag=True)
    node1.create(level="-vv")

    # verify postgresql.conf is in data directory now
    assert os.path.exists(postgres_conf_path)

    monitor.print_state()

    node1.destroy()

    # node is stuck in init state, destroy won't remove it from monitor
    # force remove from monitor
    monitor.run_sql_query(
        "select pgautofailover.remove_node('%s', %s)"
        % (str(node1.vnode.address), str(node1.port))
    )
