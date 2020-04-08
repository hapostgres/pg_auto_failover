import pgautofailover_utils as pgautofailover
from nose.tools import *

import subprocess
import os, os.path, time, shutil

cluster = None
node1 = None
node2 = None
node1_path = ""

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/skip/monitor", authMethod="skip")
    monitor.wait_until_pg_is_running()

    # Wait a little bit more to make sure the test passes
    time.sleep(1)

    with open(os.path.join("/tmp/skip/monitor", "pg_hba.conf"), 'a') as hba:
        hba.write("host all all %s trust\n" % cluster.networkSubnet)

    monitor.reload_postgres()

def test_001a_init_primary():
    global node1
    global node1_path

    print()
    time.sleep(1)
    node1_path = cluster.pg_createcluster("node1")

    # make a copy of the debian's HBA file
    hba_path = os.path.join("/etc",
                            "/".join(node1_path.split("/")[3:]),
                            "pg_hba.conf")
    shutil.copyfile(hba_path, "/tmp/pg_hba.debian.conf")

    # allow using unix domain sockets
    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "mkdir", "-p", "/tmp/socks/node1"])
    assert(p.wait(timeout=pgautofailover.COMMAND_TIMEOUT) == 0)

def test_001b_init_primary():
    global node1
    global node1_path

    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/node1"

    # we need to give the nodename here, because our method to find it
    # automatically will fail in the test environment
    node1 = cluster.create_datanode(node1_path,
                                    authMethod="skip")
def test_001c_init_primary():
    global node1
    global node1_path
    node1.create(level='-vvv')

def test_001d_init_primary():
    global node1
    global node1_path

    #
    # Check that we didn't edit the HBA file, thanks to --skip-pg-hba, here
    # in the test file spelled the strange way --auth skip.
    #
    p = subprocess.run(["diff",
                        "/tmp/pg_hba.debian.conf",
                        os.path.join(node1_path, "pg_hba.conf")],
                       text=True,
                       capture_output=True)

    print("diff %s" % " ".join(p.args))

    if p.returncode != 0:
        print("%s" % p.stdout)

    assert(p.returncode == 0)

def test_001e_init_primary():
    global node1
    global node1_path

    with open(os.path.join(node1_path, "pg_hba.conf"), 'a') as hba:
        # node1.run_sql_query will need
        # host "172.27.1.1", user "docker", database "postgres"
        hba.write("host postgres docker %s trust\n" % cluster.networkSubnet)
        hba.write("host all all %s trust\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

    node1.reload_postgres()

def test_001f_init_primary():
    global node1
    global node1_path
    node1.run()

def test_001g_init_primary():
    global node1
    global node1_path
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2

    # allow using unix domain sockets
    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "mkdir", "-p", "/tmp/socks/node2"])
    assert(p.wait() == 0)

    os.environ["PG_REGRESS_SOCK_DIR"] = "/tmp/socks/node2"

    node2 = cluster.create_datanode("/tmp/skip/node2", authMethod="skip")
    node2.create()

    with open(os.path.join("/tmp/skip/node2", "pg_hba.conf"), 'a') as hba:
        hba.write("host all all %s trust\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

    node2.reload_postgres()

    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover()
    assert node2.wait_until_state(target_state="primary", other_node=node1)
    assert node1.wait_until_state(target_state="secondary", other_node=node2)
