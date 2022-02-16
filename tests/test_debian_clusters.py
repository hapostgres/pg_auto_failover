import tests.pgautofailover_utils as pgautofailover
import os.path
import subprocess

cluster = None
monitor = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor

    print()
    monitor_path = cluster.pg_createcluster("monitor", port=6000)

    postgres_conf_path = os.path.join(monitor_path, "postgresql.conf")
    # verify postgresql.conf is not in data directory
    assert not os.path.exists(postgres_conf_path)

    monitor = cluster.create_monitor(monitor_path, port=6000)
    monitor.create(level="-vv")
    monitor.run(port=6000)
    monitor.wait_until_pg_is_running()

    # verify postgresql.conf is in data directory now
    assert os.path.exists(postgres_conf_path)

    pgversion = os.getenv("PGVERSION")

    p = subprocess.run(
        [
            "ls",
            "-ld",
            monitor_path,
            "/var/lib/postgresql/%s" % pgversion,
            "/etc/postgresql/%s" % pgversion,
            "/etc/postgresql/%s/monitor" % pgversion,
            "/etc/postgresql/%s/monitor/postgresql.conf" % pgversion,
            "/etc/postgresql/%s/monitor/pg_hba.conf" % pgversion,
            "/etc/postgresql/%s/monitor/pg_ident.conf" % pgversion,
        ],
        text=True,
        capture_output=True,
    )
    print("%s" % p.stdout)


def test_001_custom_single():
    global node1

    print()
    node1_path = cluster.pg_createcluster("debian_node1", port=6001)

    postgres_conf_path = os.path.join(node1_path, "postgresql.conf")
    # verify postgresql.conf is not in data directory
    assert not os.path.exists(postgres_conf_path)

    node1 = cluster.create_datanode(node1_path, port=6001, listen_flag=True)
    node1.create(level="-vv")

    # verify postgresql.conf is in data directory now
    assert os.path.exists(postgres_conf_path)

    pgversion = os.getenv("PGVERSION")

    p = subprocess.run(
        [
            "ls",
            "-ld",
            node1_path,
            "/var/lib/postgresql/%s" % pgversion,
            "/etc/postgresql/%s" % pgversion,
            "/etc/postgresql/%s/debian_node1" % pgversion,
            "/etc/postgresql/%s/debian_node1/postgresql.conf" % pgversion,
            "/etc/postgresql/%s/debian_node1/pg_hba.conf" % pgversion,
            "/etc/postgresql/%s/debian_node1/pg_ident.conf" % pgversion,
        ],
        text=True,
        capture_output=True,
    )
    print("%s" % p.stdout)

    monitor.print_state()


def test_002_chmod_debian_data_directory():
    # debian installs the following ownership and permissions:
    #
    # drwxr-xr-x  5 postgres postgres ... /var/lib/postgresql/11
    # drwx------ 20 docker   postgres ... /var/lib/postgresql/11/monitor
    # drwx------ 20 docker   postgres ... /var/lib/postgresql/11/debian_node1
    #
    # we need to give the postgres group the w on the top-level directory
    pgversion = os.getenv("PGVERSION")

    p = subprocess.Popen(
        ["chmod", "go+w", "/var/lib/postgresql/%s" % pgversion]
    )
    assert p.wait() == 0
