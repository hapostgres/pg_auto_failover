import pgautofailover_utils as pgautofailover
from nose.tools import *

import subprocess
import os, os.path, time, shutil

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
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)

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
                          "-subj", "/CN=monitor.pgautofailover.ca"])
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

    # now create and sign the CLIENT certificate
    postgresql_csr = os.path.join(client_top_directory, "postgresql.csr")
    postgresql_key = os.path.join(client_top_directory, "postgresql.key")
    postgresql_crt = os.path.join(client_top_directory, "postgresql.crt")

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "req", "-new", "-nodes", "-text",
                          "-out", postgresql_csr, "-keyout", postgresql_key,
                          "-subj", "/CN=autoctl_node"])
    assert(p.wait() == 0)

    p = subprocess.Popen(["chmod", "og-rwx", postgresql_key])
    assert(p.wait() == 0)

    p = subprocess.Popen(["sudo", "-E", '-u', os.getenv("USER"),
                          'env', 'PATH=' + os.getenv("PATH"),
                          "openssl", "x509", "-req", "-in", postgresql_csr,
                          "-text", "-days", "365",
                          "-CA", root_crt, "-CAkey", root_key,
                          "-CAcreateserial", "-out", postgresql_crt])
    assert(p.wait() == 0)

    p = subprocess.run(["ls", "-ld",
                        client_top_directory,
                        root_crt, root_csr, root_key,
                        postgresql_crt, postgresql_csr, postgresql_key,
                        server_crt, server_csr, server_key],
                       text=True,
                       capture_output=True)
    print("%s" % p.stdout)

    # ensure SSL = on in the config
    p = subprocess.Popen(["pg_conftool", "/tmp/skip/monitor/postgresql.conf",
                          "set", "ssl", "on"])
    assert(p.wait() == 0)

    p = subprocess.Popen(["pg_conftool", "/tmp/skip/monitor/postgresql.conf",
                          "set", "ssl_ca_file", root_crt])
    assert(p.wait() == 0)

    # reload the configuration changes to activate SSL settings
    monitor.reload_postgres()

    # the root user also needs the certificates, tests are connecting with it
    subprocess.Popen(["ln", "-s", client_top_directory, "/root/.postgresql"])
    assert(p.wait() == 0)

    # check the SSL settings
    cmd = ["openssl", "s_client", "-starttls", "postgres",
           "-connect", "172.27.1.2:5432", "-showcerts", "-CAfile", root_crt]
    print(" ".join(cmd))
    p = subprocess.run(["sudo", "-E", '-u', os.getenv("USER"),
                        'env', 'PATH=' + os.getenv("PATH")] + cmd,
                       input="",
                       text=True,
                       capture_output=True)
    assert(p.returncode == 0)

    # print connection string
    print("monitor: %s" % monitor.connection_string())

def test_001_init_primary():
    global node1
    node1_path = cluster.pg_createcluster("node1")

    # make a copy of the debian's HBA file
    hba_path = os.path.join("/etc",
                            "/".join(node1_path.split("/")[3:]),
                            "pg_hba.conf")
    shutil.copyfile(hba_path, "/tmp/pg_hba.debian.conf")

    node1 = cluster.create_datanode(node1_path,
                                    authMethod="skip",
                                    listen_flag=True)
    node1.create()

    #
    # Check that we didn't edit the HBA file, thanks to --skip-pg-hba, here
    # in the test file spelled the strange way --auth skip.
    #
    # Thing is, in the test environment, we don't allow Postgres to listen
    # on "localhost", and we don't use Unix Domain Sockets (because we have
    # a single file-system and all servers are listening on the same port),
    # so we have to introduce two new rules in the HBA file.
    #
    # So we make a diff and ignore those two lines we know we had to add to
    # the HBA rules even though we're using --skip-pg-hba.
    #
    test_hba_line_node = \
        "host all all %s/32 trust # Auto-generated by pg_auto_failover" % \
        node1.vnode.address
    test_hba_line_network = \
        "host all all %s trust # Auto-generated by pg_auto_failover" % \
        cluster.networkSubnet

    p = subprocess.run(["diff",
                        "--ignore-matching-lines", test_hba_line_node,
                        "--ignore-matching-lines", test_hba_line_network,
                        "/tmp/pg_hba.debian.conf",
                        os.path.join(node1_path, "pg_hba.conf")],
                       text=True,
                       capture_output=True)

    print("diff %s <test env hba rules> %s %s" \
          % (p.args[1], p.args[5], p.args[6]))

    if p.returncode != 0:
        print("%s" % p.stdout)

    assert(p.returncode == 0)

    with open(os.path.join(node1_path, "pg_hba.conf"), 'a') as hba:
        # node1.run_sql_query will need
        # host "172.27.1.1", user "docker", database "postgres"
        hba.write("host postgres docker %s trust\n" % cluster.networkSubnet)
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

    node1.reload_postgres()

    node1.run()
    assert node1.wait_until_state(target_state="single")

def test_002_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")

def test_003_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/skip/node2",
                                    authMethod="skip",
                                    listen_flag=True)
    node2.create()

    with open(os.path.join("/tmp/skip/node2", "pg_hba.conf"), 'a') as hba:
        hba.write("hostssl all all %s cert\n" % cluster.networkSubnet)
        hba.write("host replication all %s trust\n" % cluster.networkSubnet)

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
