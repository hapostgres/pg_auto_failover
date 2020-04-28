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

    eq_(monitor.pg_config_get('ssl'), "off")

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/enable/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

    eq_(monitor.pg_config_get('ssl'), "off")

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

    eq_(monitor.pg_config_get('ssl'), "off")

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
    assert "sslmode=require" in node1.config_get("pg_autoctl.monitor")
    eq_(node1.pg_config_get('ssl'), "on")
    check_ssl_files(node1)

def test_007_enable_ssl_secondary():
    node2.enable_ssl(sslSelfSigned=True, sslMode="require")
    node2.sleep(5)

    eq_(node2.config_get("ssl.sslmode"), "require")

    node2.wait_until_pg_is_running()

    eq_(node2.pg_config_get('ssl'), "on")
    check_ssl_files(node2)

    if node2.pgmajor() >= 12:
        assert "sslmode=require" in node2.pg_config_get('primary_conninfo')

def test_008_disable_maintenance():
    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

# upgrade to verify full
def test_009_enable_maintenance():
    print()
    print("Enabling maintenance on node2")
    node2.enable_maintenance()

    assert node2.wait_until_state(target_state="maintenance")

def test_009_enable_ssl_verify_ca_monitor():
    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    cluster.create_root_cert(client_top_directory,
                             basename = "root",
                             CN = "/CN=root.pgautofailover.ca")

    # now create and sign the CLIENT certificate
    clientCert = cert.SSLCert(client_top_directory,
                              basename = "postgresql",
                              CN = "/CN=autoctl_node")
    clientCert.create_signed_certificate(rootKey = cluster.cert.key,
                                         rootCert = cluster.cert.crt)

    # now create and sign the SERVER certificate for the monitor
    monitorCert = cert.SSLCert("/tmp/certs/monitor", "server",
                              "/CN=monitor.pgautofailover.ca")
    monitorCert.create_signed_certificate(rootKey = cluster.cert.key,
                                          rootCert = cluster.cert.crt)

    monitor.enable_ssl(sslCAFile=cluster.cert.crt,
                       sslServerKey=monitorCert.key,
                       sslServerCert=monitorCert.crt,
                       sslMode="verify-ca")

    monitor.sleep(2) # we signaled, wait some time

    eq_(monitor.config_get("ssl.sslmode"), "verify-ca")
    eq_(monitor.pg_config_get('ssl'), "on")
    check_ssl_files(monitor)

def test_010_enable_ssl_verify_ca_primary():
    node1Cert = cert.SSLCert("/tmp/certs/node1", "server",
                              "/CN=node1.pgautofailover.ca")
    node1Cert.create_signed_certificate(rootKey = cluster.cert.key,
                                        rootCert = cluster.cert.crt)

    node1.stop_pg_autoctl()
    node1.enable_ssl(sslSelfSigned=True, sslMode="verify-ca")
    node1.run()
    node1.sleep(2)

    eq_(node1.config_get("ssl.sslmode"), "require")
    assert "sslmode=verify-ca" in node1.config_get("pg_autoctl.monitor")
    eq_(node1.pg_config_get('ssl'), "on")
    check_ssl_files(node1)

def test_011_enable_ssl_verify_ca_primary():
    node2Cert = cert.SSLCert("/tmp/certs/node2", "server",
                              "/CN=node2.pgautofailover.ca")
    node2Cert.create_signed_certificate(rootKey = cluster.cert.key,
                                        rootCert = cluster.cert.crt)

    node2.enable_ssl(sslSelfSigned=True, sslMode="require")
    node2.sleep(5)

    eq_(node2.config_get("ssl.sslmode"), "verify-ca")

    node2.wait_until_pg_is_running()

    eq_(node2.pg_config_get('ssl'), "on")
    check_ssl_files(node2)

    if node2.pgmajor() >= 12:
        assert "sslmode=verify-ca" in node2.pg_config_get('primary_conninfo')

def test_012_disable_maintenance():
    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
