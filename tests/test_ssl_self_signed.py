import pgautofailover_utils as pgautofailover

from nose.tools import eq_

import os.path

cluster = None
node1 = None
node2 = None
expected_ciphers = (
    "ECDHE-ECDSA-AES128-GCM-SHA256:"
    "ECDHE-ECDSA-AES256-GCM-SHA384:"
    "ECDHE-RSA-AES128-GCM-SHA256:"
    "ECDHE-RSA-AES256-GCM-SHA384:"
    "ECDHE-ECDSA-AES128-SHA256:"
    "ECDHE-ECDSA-AES256-SHA384:"
    "ECDHE-RSA-AES128-SHA256:"
    "ECDHE-RSA-AES256-SHA384"
)


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def check_ssl_files(node):
    for setting, f in [("ssl_key_file", "server.key"), ("ssl_cert_file", "server.crt")]:
        file_path = os.path.join(node.datadir, f)
        assert os.path.isfile(file_path)
        eq_(node.pg_config_get(setting), file_path)


def check_ssl_ciphers(node):
    eq_(node.pg_config_get("ssl_ciphers"), expected_ciphers)


def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/ssl-self-signed/monitor",
                                     sslSelfSigned=True)
    monitor.run()
    assert monitor.config_get("ssl.sslmode") == "require"
    monitor.wait_until_pg_is_running()
    check_ssl_files(monitor)
    # TODO: Uncomment when there can be super user access to the monitor
    # check_ssl_ciphers(node1)


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/ssl-self-signed/node1",
                                    sslSelfSigned=True)
    node1.create()
    assert node1.config_get("ssl.sslmode") == "require"

    node1.run()
    assert node1.wait_until_state(target_state="single")
    check_ssl_files(node1)
    check_ssl_ciphers(node1)


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

    # TODO: Uncomment when we fix the bug that secondary has the same cert/key
    # path as primary
    # check_ssl_files(node1)
    check_ssl_ciphers(node1)


def test_004_failover():
    print()
    print("Calling pgautofailover.failover() on the monitor")
    cluster.monitor.failover()
    assert node2.wait_until_state(target_state="primary")
    assert node1.wait_until_state(target_state="secondary")
