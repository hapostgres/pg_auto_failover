import pgautofailover_utils as pgautofailover
import ssl_cert_utils as cert
import subprocess
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

    # remove client side setup for certificates too
    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "rm", "-rf", client_top_directory])
    assert(p.wait() == 0)

    # also remove certificates we created for the servers
    p = subprocess.run(["sudo", "-E", '-u', os.getenv("USER"),
                        'env', 'PATH=' + os.getenv("PATH"),
                        "rm", "-rf", "/tmp/certs"])
    assert(p.returncode == 0)


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

def test_010_enable_ssl_verify_ca_monitor():
    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    print()
    print("Creating cluster root certificate")
    cluster.create_root_cert(client_top_directory,
                             basename = "root",
                             CN = "/CN=root.pgautofailover.ca")

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        cluster.cert.crt, cluster.cert.csr, cluster.cert.key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # now create and sign the CLIENT certificate
    print("Creating cluster client certificate")
    clientCert = cert.SSLCert(client_top_directory,
                              basename = "postgresql",
                              CN = "/CN=autoctl_node")
    clientCert.create_signed_certificate(rootKey = cluster.cert.key,
                                         rootCert = cluster.cert.crt)

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        clientCert.crt, clientCert.csr, clientCert.key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # the root user also needs the certificates, tests are connecting with it
    subprocess.run(["ln", "-s", client_top_directory, "/root/.postgresql"])
    assert(p.returncode == 0)

    p = subprocess.run(["ls", "-l", "/root/.postgresql"],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # now create and sign the SERVER certificate for the monitor
    print("Creating monitor server certificate")
    monitorCert = cert.SSLCert("/tmp/certs/monitor", "server",
                              "/CN=monitor.pgautofailover.ca")
    monitorCert.create_signed_certificate(rootKey = cluster.cert.key,
                                          rootCert = cluster.cert.crt)

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        cluster.cert.crt, cluster.cert.csr, cluster.cert.key,
                        clientCert.crt, clientCert.csr, clientCert.key,
                        monitorCert.crt, monitorCert.csr, monitorCert.key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    monitor.enable_ssl(sslCAFile=cluster.cert.crt,
                       sslServerKey=monitorCert.key,
                       sslServerCert=monitorCert.crt,
                       sslMode="verify-ca")

    monitor.sleep(2) # we signaled, wait some time

    eq_(monitor.config_get("ssl.cert_file"), monitorCert.crt)
    eq_(monitor.config_get("ssl.sslmode"), "verify-ca")
    eq_(monitor.pg_config_get('ssl'), "on")
    check_ssl_files(monitor)

def test_011_enable_ssl_verify_ca_primary():
    node1Cert = cert.SSLCert("/tmp/certs/node1", "server",
                              "/CN=node1.pgautofailover.ca")
    node1Cert.create_signed_certificate(rootKey = cluster.cert.key,
                                        rootCert = cluster.cert.crt)

    node1.stop_pg_autoctl()
    node1.enable_ssl(sslSelfSigned=True, sslMode="verify-ca")
    node1.run()
    node1.sleep(2)

    eq_(node1.config_get("ssl.sslmode"), "verify-ca")
    assert "sslmode=verify-ca" in node1.config_get("pg_autoctl.monitor")
    eq_(node1.pg_config_get('ssl'), "on")
    check_ssl_files(node1)

def test_012_enable_ssl_verify_ca_primary():
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

def test_013_disable_maintenance():
    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
