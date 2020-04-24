import pgautofailover_utils as pgautofailover
from nose.tools import *

import os

cluster = None
monitor = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()


def check_ssl_files(node):
    for setting, f in [("ssl_key_file", "server.key"),
                       ("ssl_cert_file", "server.crt")]:
        file_path = os.path.join(node.datadir, f)
        assert os.path.isfile(file_path)
        eq_(node.pg_config_get(setting), file_path)


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/enable/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/enable/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/enable/node2")
    node2.create()
    node2.run()

    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_maintenance():
    print()
    print("Enabling maintenance on node2")
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")

def test_005_enable_ssl_monitor():
    monitor.enable_ssl(sslSelfSigned=True, sslMode="require")
    monitor.sleep(2) # we signaled, wait some time

    eq_(monitor.config_get("ssl.sslmode"), "require")
    eq_(monitor.pg_config_get('ssl'), "on")
    check_ssl_files(monitor)

def test_006_enable_ssl_primary():
    # we stop pg_autoctl to make it easier for the test to be reliable
    # without too much delay/sleep hacking; when doing the `pg_autoctl
    # enable ssl` online we need to make sure the signal made it to the
    # running process and then was acted upon
    node1.stop_pg_autoctl()
    node1.enable_ssl(sslSelfSigned=True, sslMode="require")
    node1.run()
    node1.sleep(2)

    eq_(node1.config_get("ssl.sslmode"), "require")
    eq_(node1.pg_config_get('ssl'), "on")
    check_ssl_files(node1)

def test_007_enable_ssl_secondary():
    node2.enable_ssl(sslSelfSigned=True, sslMode="require")
    node2.sleep(5)

    eq_(node2.config_get("ssl.sslmode"), "require")

    node2.wait_until_pg_is_running()

    eq_(node2.pg_config_get('ssl'), "on")
    check_ssl_files(node2)

def test_008_disable_maintenance():
    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
