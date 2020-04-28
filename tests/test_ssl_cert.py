import pgautofailover_utils as pgautofailover
import ssl_cert_utils as cert
from nose.tools import *

import subprocess
import os, os.path, time, shutil

cluster = None
node1 = None
node2 = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    cluster.create_root_cert(client_top_directory,
                             basename = "root",
                             CN = "/CN=root.pgautofailover.ca")

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

def test_000_create_monitor():
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
    # Now, create a server certificate signed by the new root certificate
    # authority
    client_top_directory = os.path.join(os.getenv("HOME"), ".postgresql")

    server_csr, server_key, server_crt = \
        cert.create_signed_certificate("/tmp/certs/monitor",
                                       rootKey = cluster.cert.key,
                                       rootCert = cluster.cert.crt,
                                       basename = "server",
                                       CN = "/CN=monitor.pgautofailover.ca")

    # now create and sign the CLIENT certificate
    postgresql_csr, postgresql_key, postgresql_crt = \
        cert.create_signed_certificate(client_top_directory,
                                       rootKey = cluster.cert.key,
                                       rootCert = cluster.cert.crt,
                                       basename = "postgresql",
                                       CN = "/CN=autoctl_node")

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        cluster.cert.crt, cluster.cert.csr, cluster.cert.key,
                        postgresql_crt, postgresql_csr, postgresql_key,
                        server_crt, server_csr, server_key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # the root user also needs the certificates, tests are connecting with it
    subprocess.run(["ln", "-s", client_top_directory, "/root/.postgresql"])
    assert(p.returncode == 0)

    #
    # Now create the monitor Postgres instance with the certificates
    #
    monitor = cluster.create_monitor("/tmp/cert/monitor",
                                     authMethod="skip",
                                     sslMode="verify-ca",
                                     sslCAFile=cluster.cert.crt,
                                     sslServerKey=server_key,
                                     sslServerCert=server_crt)
    monitor.wait_until_pg_is_running()

    with open(os.path.join("/tmp/cert/monitor", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)

    monitor.reload_postgres()

    # check the SSL settings
    cmd = ["openssl", "s_client", "-starttls", "postgres",
           "-connect", "172.27.1.2:5432", "-showcerts",
           "-CAfile", cluster.cert.crt]
    print(" ".join(cmd))
    p = subprocess.run(["sudo", "-E", '-u', os.getenv("USER"),
                        'env', 'PATH=' + os.getenv("PATH")] + cmd,
                       input="",
                       text=True,
                       capture_output=True)
    if p.returncode != 0:
        print("" % p.stdout)
        print("" % p.stderr)
    assert(p.returncode == 0)

    # print connection string
    print("monitor: %s" % monitor.connection_string())

def test_001_init_primary():
    global node1

    # Create a server certificate signed by the root Certificate Authority
    certs_dir = "/tmp/certs/node1"

    server_csr, server_key, server_crt = \
        cert.create_signed_certificate(certs_dir,
                                       rootKey = cluster.cert.key,
                                       rootCert = cluster.cert.crt,
                                       basename = "server",
                                       CN = "/CN=node1.pgautofailover.ca")

    # Now create the server with the certificates
    node1 = cluster.create_datanode("/tmp/cert/node1",
                                    authMethod="skip",
                                    sslMode="verify-ca",
                                    sslCAFile=cluster.cert.crt,
                                    sslServerKey=server_key,
                                    sslServerCert=server_crt)
    node1.create(level='-vv')

    with open(os.path.join("/tmp/cert/node1", "pg_hba.conf"), 'a') as hba:
        # node1.run_sql_query will need
        # host "172.27.1.1", user "docker", database "postgres"
        hba.write("hostssl postgres docker %s cert\n" % cluster.networkSubnet)
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)
        hba.write("hostssl replication all %s cert map=pgautofailover\n" \
                  % cluster.networkSubnet)

    with open(os.path.join("/tmp/cert/node1", "pg_ident.conf"), 'a') as ident:
        # use an ident map to allow using the same cert for replication
        ident.write("pgautofailover autoctl_node pgautofailover_replicator\n")

    node1.reload_postgres()

    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    print()
    print(node1.connection_string())
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2

    # Create a server certificate signed by the root Certificate Authority
    certs_dir = "/tmp/certs/node2"

    root_key = os.path.join(os.getenv("HOME"), ".postgresql", "root.key")
    root_crt = os.path.join(os.getenv("HOME"), ".postgresql", "root.crt")

    server_csr, server_key, server_crt = \
        cert.create_signed_certificate(certs_dir,
                                       rootKey = cluster.cert.key,
                                       rootCert = cluster.cert.crt,
                                       basename = "server",
                                       CN = "/CN=node2.pgautofailover.ca")

    # Now create the server with the certificates
    node2 = cluster.create_datanode("/tmp/cert/node2",
                                    authMethod="skip",
                                    sslMode="verify-ca",
                                    sslCAFile=cluster.cert.crt,
                                    sslServerKey=server_key,
                                    sslServerCert=server_crt)
    node2.create(level='-vv')

    with open(os.path.join("/tmp/cert/node2", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)
        hba.write("hostssl replication all %s cert map=pgautofailover\n" \
                  % cluster.networkSubnet)

    with open(os.path.join("/tmp/cert/node1", "pg_ident.conf"), 'a') as ident:
        # use an ident map to allow using the same cert for replication
        ident.write("pgautofailover autoctl_node pgautofailover_replicator\n")

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
