import tests.pgautofailover_utils as pgautofailover
from nose.tools import assert_raises, raises, eq_

import os
import shutil
import subprocess
import time

cluster = None
monitor = None
node1 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/config_test/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/config_test/node1")
    node1.create()

    # the name of the node should be "%s_%d" % ("node", node1.nodeid)
    eq_(node1.get_nodename(), "node_%d" % node1.get_nodeid())

    # we can change the name on the monitor with pg_autoctl set node metadata
    node1.set_metadata(name="node a")
    eq_(node1.get_nodename(), "node a")

    node1.run()
    assert node1.wait_until_state(target_state="single")

    # we can also change the name directly in the configuration file
    node1.config_set("pg_autoctl.name", "a")

    # wait until the reload signal has been processed before checking
    time.sleep(2)
    eq_(node1.get_nodename(), "a")


def test_002_config_set_monitor():
    pg_ctl = monitor.config_get("postgresql.pg_ctl")

    # set something non-default to assert no side-effects later
    sslmode = "prefer"
    monitor.config_set("ssl.sslmode", sslmode)

    # set monitor config postgresql.pg_ctl to something invalid
    with assert_raises(subprocess.CalledProcessError):
        monitor.config_set("postgresql.pg_ctl", "invalid")

    # it should not get changed
    eq_(monitor.config_get("postgresql.pg_ctl"), pg_ctl)

    # try again with a keeper
    pg_ctl = node1.config_get("postgresql.pg_ctl")

    # set the keeper to something invalid
    with assert_raises(subprocess.CalledProcessError):
        node1.config_set("postgresql.pg_ctl", "invalid")

    # it should not get changed
    eq_(node1.config_get("postgresql.pg_ctl"), pg_ctl)

    # pg_ctl can be moved and `config set` will still operate.
    shutil.copy(pg_ctl, "/tmp/pg_ctl")
    monitor.config_set("postgresql.pg_ctl", "/tmp/pg_ctl")
    # "move" pg_ctl
    os.remove("/tmp/pg_ctl")
    monitor.config_set("postgresql.pg_ctl", pg_ctl)

    eq_(monitor.config_get("postgresql.pg_ctl"), pg_ctl)

    # no side effects
    eq_(monitor.config_get("ssl.sslmode"), sslmode)
