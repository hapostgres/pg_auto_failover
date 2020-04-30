import pgautofailover_utils as pgautofailover
import ssl_cert_utils as cert
import subprocess
from nose.tools import eq_

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


def check_ssl(node, ssl, sslmode, monitor=False, primary=False, SSLCert=None):
    key = None
    crt = None
    crl = None
    rootCert = None
    if SSLCert is not None:
        key = SSLCert.key
        crt = SSLCert.crt
        crl = SSLCert.crl
        rootCert = SSLCert.rootCert
    elif ssl == "on":
        key = os.path.join(node.datadir, "server.key")
        crt = os.path.join(node.datadir, "server.crt")

    eq_(node.pg_config_get('ssl'), ssl)
    eq_(node.config_get("ssl.sslmode"), sslmode)

    def check_conn_string(conn_string):
        print("checking connstring =", conn_string)
        assert f"sslmode={sslmode}" in conn_string
        if rootCert:
            assert f"sslrootcert={rootCert}" in conn_string
        if crl:
            assert f"sslcrl={crl}" in conn_string

    conn_string, _ = pgautofailover.PGAutoCtl(node)\
        .execute("show uri --monitor", 'show', 'uri', '--monitor')
    check_conn_string(conn_string)
    if not monitor:
        check_conn_string(node.config_get("pg_autoctl.monitor"))
        conn_string, _ = pgautofailover.PGAutoCtl(node)\
            .execute("show uri --monitor", 'show', 'uri', '--formation', 'default')

    for pg_setting, autoctl_setting, file_path in [
            ("ssl_key_file", "ssl.key_file", key),
            ("ssl_cert_file", "ssl.cert_file", crt),
            ("ssl_crl_file", "ssl.crl_file", crl),
            ("ssl_ca_file", "ssl.ca_file", rootCert)]:
        if file_path is None:
            continue
        assert os.path.isfile(file_path)
        print("checking", pg_setting)
        eq_(node.pg_config_get(pg_setting), file_path)
        eq_(node.config_get(autoctl_setting), file_path)

    if monitor or primary:
        return

    if node.pgmajor() >= 12:
        check_conn_string(node.pg_config_get('primary_conninfo'))


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/enable/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()

    check_ssl(monitor, "off", "prefer", monitor=True)

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/enable/node1")
    node1.create()
    node1.run()
    assert node1.wait_until_state(target_state="single")

    check_ssl(node1, "off", "prefer", primary=True)

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

    check_ssl(node2, "off", "prefer")

def test_004_maintenance():
    print()
    print("Enabling maintenance on node2")
    node2.enable_maintenance()
    assert node2.wait_until_state(target_state="maintenance")

def test_005_enable_ssl_monitor():
    monitor.enable_ssl(sslSelfSigned=True, sslMode="require")
    monitor.sleep(2) # we signaled, wait some time

    check_ssl(monitor, "on", "require", monitor=True)

def test_006_enable_ssl_primary():
    # we stop pg_autoctl to make it easier for the test to be reliable
    # without too much delay/sleep hacking; when doing the `pg_autoctl
    # enable ssl` online we need to make sure the signal made it to the
    # running process and then was acted upon
    node1.stop_pg_autoctl()
    node1.enable_ssl(sslSelfSigned=True, sslMode="require")
    node1.run()
    node1.sleep(2)

    check_ssl(node1, "on", "require", primary=True)

def test_007_enable_ssl_secondary():
    node2.enable_ssl(sslSelfSigned=True, sslMode="require")
    node2.sleep(5)

    node2.wait_until_pg_is_running()

    check_ssl(node2, "on", "require")

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
    clientCert.create_signed_certificate(cluster.cert)

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
    monitorCert.create_signed_certificate(cluster.cert)

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

    check_ssl(monitor, "on", "verify-ca", monitor=True, SSLCert=monitorCert)

def test_011_enable_ssl_verify_ca_primary():
    node1Cert = cert.SSLCert("/tmp/certs/node1", "server",
                              "/CN=node1.pgautofailover.ca")
    node1Cert.create_signed_certificate(cluster.cert)

    node1.stop_pg_autoctl()
    node1.enable_ssl(sslCAFile = cluster.cert.crt,
                     sslServerKey = node1Cert.key,
                     sslServerCert = node1Cert.crt,
                     sslMode="verify-ca")
    node1.run()
    node1.sleep(2)

    check_ssl(node1, "on", "verify-ca", primary=True, SSLCert=node1Cert)

def test_012_enable_ssl_verify_ca_primary():
    node2Cert = cert.SSLCert("/tmp/certs/node2", "server",
                              "/CN=node2.pgautofailover.ca")
    node2Cert.create_signed_certificate(cluster.cert)

    node2.enable_ssl(sslCAFile = cluster.cert.crt,
                     sslServerKey = node2Cert.key,
                     sslServerCert = node2Cert.crt,
                     sslMode="verify-ca")
    node2.sleep(5)

    node2.wait_until_pg_is_running()

    check_ssl(node2, "on", "verify-ca", SSLCert=node2Cert)

def test_013_disable_maintenance():
    print("Disabling maintenance on node2")
    node2.disable_maintenance()
    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
