import tests.pgautofailover_utils as pgautofailover
from nose.tools import *

import os
import subprocess

cluster = None
node1 = None
node2 = None
node3 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/sb-from-pgdata/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/sb-from-pgdata/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")
    node1.wait_until_pg_is_running()


def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_003_init_secondary():
    global node2

    # fail the registration of a node2 by using a PGDATA directory that has
    # already been created, with another system_identifier (initdb creates a
    # new one each time)
    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "pg_ctl",
            "initdb",
            "-s",
            "-D",
            "/tmp/sb-from-pgdata/node2",
        ]
    )
    assert p.wait() == 0

    node2 = cluster.create_datanode("/tmp/sb-from-pgdata/node2")


@raises(Exception)
def test_004_create_raises_error():
    try:
        node2.create()
    except Exception as e:
        # we want to see the failure here
        print(e)
        raise


def test_005_cleanup_after_failure():
    print("Failed as expected, cleaning up")
    print("rm -rf /tmp/sb-from-pgdata/node2")
    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "rm",
            "-rf",
            "/tmp/sb-from-pgdata/node2",
        ]
    )
    assert p.wait() == 0


def test_006_init_secondary():
    global node3

    # create node3 from a manual copy of node1 to test creating a standby
    # from an existing PGDATA (typically PGDATA would be deployed from a
    # backup and recovery mechanism)
    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "cp",
            "-a",
            "/tmp/sb-from-pgdata/node1",
            "/tmp/sb-from-pgdata/node3",
        ]
    )
    assert p.wait() == 0

    os.remove("/tmp/sb-from-pgdata/node3/postmaster.pid")

    node3 = cluster.create_datanode("/tmp/sb-from-pgdata/node3")
    node3.create()
    node3.run()
    cluster.monitor.print_state()
    assert node3.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")


def test_007_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover()
    assert node3.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")
