import pgautofailover_utils as pgautofailover
from nose.tools import *

import subprocess
import os, os.path
import json

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/ssl-self-signed/monitor",
                                     sslSelfSigned=True)

    assert monitor.config_get("ssl.sslmode") == "require"
    monitor.wait_until_pg_is_running()

    for f in ["server.key", "server.crt"]:
        assert(os.path.isfile(os.path.join("/tmp/ssl-self-signed/monitor", f)))

    p = subprocess.run(["openssl", "ciphers", "-v",
                        "TLSv1.2+HIGH:!aNULL:!eNULL"],
                       text=True,
                       capture_output=True)
    print()
    print("%s" % " ".join(p.args))
    print("%s" % p.stdout)

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/ssl-self-signed/node1",
                                    sslSelfSigned=True)
    node1.create()
    assert node1.config_get("ssl.sslmode") == "require"

    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/ssl-self-signed/node2",
                                    sslSelfSigned=True)

    node2.create()
    assert node2.config_get("ssl.sslmode") == "require"

    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")
