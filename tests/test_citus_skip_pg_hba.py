import tests.pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_

import subprocess
import os, os.path, time, shutil
import pprint

cluster = None
coord0a = None
coord0b = None
worker1a = None
worker1b = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    monitor = cluster.create_monitor(
        "/tmp/citus/skip/monitor", authMethod="skip"
    )
    monitor.run()
    monitor.wait_until_pg_is_running()

    with open(os.path.join(monitor.datadir, "pg_hba.conf"), "a") as hba:
        hba.write("host all all %s trust\n" % cluster.networkSubnet)

    monitor.reload_postgres()


def test_001a_init_coordinator():
    global coord0a

    # allow using unix domain sockets
    pgautofailover.sudo_mkdir_p("/tmp/socks/coord0a")
    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/coord0a"

    coord0a = cluster.create_datanode(
        "/tmp/citus/skip/coord0a",
        role=pgautofailover.Role.Coordinator,
        authMethod="skip",
    )
    coord0a.create()

    with open(os.path.join(coord0a.datadir, "pg_hba.conf"), "a") as hba:
        # run_sql_query() will need
        # host "172.27.1.1", user "docker", database "postgres"
        hba.write("host postgres docker %s trust\n" % cluster.networkSubnet)
        hba.write("host citus docker %s trust\n" % cluster.networkSubnet)
        hba.write("host all all %s trust\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

    coord0a.run()
    assert coord0a.wait_until_state(target_state="single")


def test_001b_init_coordinator():
    global coord0b

    # allow using unix domain sockets
    pgautofailover.sudo_mkdir_p("/tmp/socks/coord0b")
    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/coord0b"

    # coord0b will fetch the HBA from coord0a during pg_basebackup
    coord0b = cluster.create_datanode(
        "/tmp/citus/skip/coord0b",
        role=pgautofailover.Role.Coordinator,
        authMethod="skip",
    )
    coord0b.create()
    coord0b.run()

    assert coord0a.wait_until_state(target_state="primary")
    assert coord0b.wait_until_state(target_state="secondary")


def test_002a_init_worker():
    global worker1a

    # allow using unix domain sockets
    pgautofailover.sudo_mkdir_p("/tmp/socks/worker1a")
    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/worker1a"

    worker1a = cluster.create_datanode(
        "/tmp/citus/skip/worker1a",
        group=1,
        role=pgautofailover.Role.Worker,
        authMethod="skip",
    )


@raises(Exception)
def test_002b_create_worker():
    # we expect failure when calling master_activate_node on the coordinator
    # as the coordinator is not allowed to connect to this node yet
    worker1a.create(name="worker1a")


def test_002c_check_local_state():
    state = worker1a.get_local_state()
    eq_(("init", "single"), state)


def test_002d_run_worker():
    # edit the HBA, allowing the activation retry to succeed this time
    with open(os.path.join(worker1a.datadir, "pg_hba.conf"), "a") as hba:
        hba.write("host all all %s trust\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

    worker1a.run()


def test_002e_check_hba():
    # check the HBA has not been tampered by pg_autoctl
    eq_(False, worker1a.editedHBA())


def test_002f_activated_node():
    timeout = 10
    q = "select isactive from pg_dist_node where nodename = %s"
    isactive = coord0a.run_sql_query(q, str(worker1a.vnode.address))[0][0]

    while timeout > 0 and isactive is not True:
        print(".", end="")
        time.sleep(1)
        timeout -= 1

        isactive = coord0a.run_sql_query(q, str(worker1a.vnode.address))[0][0]

    if isactive is not True:
        raise Exception(
            "test failed: worker1a is still not activated "
            + "in the coordinator after 10s"
        )


def test_003_init_worker():
    global worker1b

    # allow using unix domain sockets
    pgautofailover.sudo_mkdir_p("/tmp/socks/worker1b")
    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/worker1b"

    worker1b = cluster.create_datanode(
        "/tmp/citus/skip/worker1b",
        group=1,
        role=pgautofailover.Role.Worker,
        authMethod="skip",
    )
    worker1b.create(name="worker1b")
    worker1b.run()

    eq_(False, worker1b.editedHBA())

    # wait here till all workers are stable and in the desired state
    # by not waiting on all workers separately this should save a bit of time
    print()  # make the debug output more readable
    assert worker1a.wait_until_state(target_state="primary")
    assert worker1b.wait_until_state(target_state="secondary")


def test_004_create_distributed_table():
    coord0a.run_sql_query("CREATE TABLE t1 (a int)")
    coord0a.run_sql_query("SELECT create_distributed_table('t1', 'a')")
    coord0a.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_005_failover():
    cluster.monitor.failover(group=1)
    assert worker1b.wait_until_state(target_state="wait_primary")
    assert worker1a.wait_until_state(target_state="secondary")
    assert worker1b.wait_until_state(target_state="primary")
