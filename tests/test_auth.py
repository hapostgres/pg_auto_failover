import tests.pgautofailover_utils as pgautofailover
from nose.tools import *

import os
import re

cluster = None
node1 = None
node2 = None
replication_password = "streaming_password"
monitor_password = "monitor_password"


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/auth/monitor", authMethod="md5")
    monitor.run()
    monitor.wait_until_pg_is_running()
    monitor.set_user_password("autoctl_node", "autoctl_node_password")

    monitor.create_formation("auth", kind="pgsql", secondary=True)


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode(
        "/tmp/auth/node1", authMethod="md5", formation="auth"
    )
    node1.create()
    node1.config_set("replication.password", replication_password)
    node1.run()

    node1.wait_until_pg_is_running()
    node1.set_user_password("pgautofailover_monitor", monitor_password)
    node1.set_user_password("pgautofailover_replicator", replication_password)

    assert node1.wait_until_state(target_state="single")


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode(
        "/tmp/auth/node2", authMethod="md5", formation="auth"
    )

    os.putenv("PGPASSWORD", replication_password)
    node2.create()
    node2.config_set("replication.password", replication_password)

    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    eq_(
        node1.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_2)",
    )

    # Prevent regression from bug fixed in PR #839
    logs = node2.logs("STDERR")
    match = re.search("Failed to CHECKPOINT before restart", logs)
    if match:
        print("Found CHECKPOINT in logs: ", match[0])
    assert not match
    node2.run()


def test_004_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover(formation="auth")
    assert node2.wait_until_state(target_state="primary")
    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_1)",
    )

    assert node1.wait_until_state(target_state="secondary")


def test_005_logging_of_passwords():
    logs = node2.logs()
    assert monitor_password not in logs
    assert "password=****" in logs

    # We are still logging passwords when the pguri is incomplete and when
    # printing settings, so assert that it's not there in other cases:
    assert not re.search(
        "^(?!primary_conninfo|Failed to find).*%s.*$" % replication_password,
        logs,
    )
