import pgautofailover_utils as pgautofailover
from nose.tools import *

import subprocess
import shutil
import os

cluster = None
node1 = None
node2 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/check/monitor")
    monitor.run()
    monitor.wait_until_pg_is_running()


def test_001_add_hba_entry():
    with open(os.path.join("/tmp/check/monitor", "pg_hba.conf"), "a") as hba:
        hba.write("host all all %s trust\n" % cluster.networkSubnet)

    # print()
    # with open(os.path.join("/tmp/check/monitor", "pg_hba.conf"), "r") as hba:
    #     lines = hba.readlines()
    #     for line in lines[-10:]:
    #         print("%s" % line[:-1])

    cluster.monitor.reload_postgres()


def test_002_make_installcheck():
    p = subprocess.Popen(
        [
            "sudo",
            shutil.which("chmod"),
            "-R",
            "go+w",
            "/usr/src/pg_auto_failover/src/monitor",
        ]
    )
    assert p.wait() == 0

    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "PGHOST=" + str(cluster.monitor.vnode.address),
            "make",
            "-C",
            "/usr/src/pg_auto_failover/src/monitor",
            "installcheck",
        ]
    )

    if p.wait() != 0:
        diff = "/usr/src/pg_auto_failover/src/monitor/regression.diffs"
        with open(diff, "r") as d:
            print("%s" % d.read())

        raise Exception("make installcheck failed")
