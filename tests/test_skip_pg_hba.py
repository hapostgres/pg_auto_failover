import pgautofailover_utils as pgautofailover
from nose.tools import *

import subprocess
import os, os.path, time

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.destroy()

def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/skip/monitor",
                                     authMethod="skip",
                                     sslMode="verify-ca")
    monitor.wait_until_pg_is_running()

    with open(os.path.join("/tmp/skip/monitor", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all 172.27.1.0/24 trust\n")

    monitor.reload_postgres()

    # create SSL certs and keys for this server
    #
    # https://www.postgresql.org/docs/11/ssl-tcp.html
    #
    # server.crt and server.key should be stored on the server, and root.crt
    # should be stored on the client so the client can verify that the
    # server's leaf certificate was signed by its trusted root certificate.
    # root.key should be stored offline for use in creating future
    # certificates.
    #
    # https://www.postgresql.org/docs/current/libpq-ssl.html
    #
    # If the server attempts to verify the identity of the client by
    # requesting the client's leaf certificate, libpq will send the
    # certificates stored in file ~/.postgresql/postgresql.crt in the user's
    # home directory
    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "mkdir", "-p", client_top_directory])
    assert(p.wait() == 0)

    root_csr = os.path.join(client_top_directory, "root.csr")
    root_key = os.path.join(client_top_directory, "root.key")
    root_crt = os.path.join(client_top_directory, "root.crt")

    # first create a certificate signing request (CSR) and a public/private
    # key file
    print()
    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "req", "-new", "-nodes", "-text",
                          "-out", root_csr, "-keyout", root_key,
                          "-subj", "/CN=root.pgautofailover.ca"])
    assert(p.wait() == 0)

    p = subprocess.Popen(["chmod", "og-rwx", root_key])
    assert(p.wait() == 0)

    # Then, sign the request with the key to create a root certificate authority
    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "x509", "-req", "-in", root_csr,
                          "-text", "-days", "3650",
                          "-extfile", "/etc/ssl/openssl.cnf",
                          "-extensions", "v3_ca",
                          "-signkey", root_key,
                          "-out", root_crt])
    assert(p.wait() == 0)

    # Finally, create a server certificate signed by the new root
    # certificate authority
    server_crt = os.path.join("/tmp/skip/monitor", "server.crt")
    server_csr = os.path.join("/tmp/skip/monitor", "server.csr")
    server_key = os.path.join("/tmp/skip/monitor", "server.key")

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "req", "-new", "-nodes", "-text",
                          "-out", server_csr, "-keyout", server_key,
                          "-subj", "/CN=pgautofailover"])
    assert(p.wait() == 0)

    p = subprocess.Popen(["chmod", "og-rwx", server_key])
    assert(p.wait() == 0)

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "x509", "-req", "-in", server_csr,
                          "-text", "-days", "365",
                          "-CA", root_crt, "-CAkey", root_key,
                          "-CAcreateserial", "-out", server_crt])
    assert(p.wait() == 0)

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        root_crt, root_csr, root_key,
                        server_crt, server_csr, server_key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # ensure SSL = on in the config
    p = subprocess.Popen(["pg_conftool", "/tmp/skip/monitor/postgresql.conf",
                          "set", "ssl", "on"])
    assert(p.wait() == 0)

    p = subprocess.Popen(["pg_conftool", "/tmp/skip/monitor/postgresql.conf",
                          "set", "ssl_ca_file", server_crt])
    assert(p.wait() == 0)

    # reload the configuration changes to activate SSL settings
    monitor.reload_postgres()

    # the root user also needs the certificates, tests are connecting with it
    subprocess.Popen(["ln", "-s", client_top_directory, "/root/.postgresql"])
    assert(p.wait() == 0)

    # print connection string
    print("monitor: %s" % monitor.connection_string())
    #time.sleep(3600)

def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/skip/node1", authMethod="skip")
    node1.create()

    with open(os.path.join("/tmp/skip/node1", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all 172.27.1.0/24 trust\n")
        hba.write("host replication all 172.27.1.0/24 trust\n")

    node1.reload_postgres()

    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/skip/node2", authMethod="skip")
    node2.create()

    with open(os.path.join("/tmp/skip/node1", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all 172.27.1.0/24 trust\n")
        hba.write("host replication all 172.27.1.0/24 trust\n")

    node2.reload_postgres()

    node2.run()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

def test_004_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")
