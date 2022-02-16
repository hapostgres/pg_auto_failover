import tests.pgautofailover_utils as pgautofailover
from nose.tools import raises, eq_

import time

cluster = None
monitor = None
node1 = None
node2 = None
node3 = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.destroy()


def test_000_create_monitor():
    global monitor
    monitor = cluster.create_monitor("/tmp/basic/monitor")
    monitor.run()


def test_001_init_primary():
    global node1
    node1 = cluster.create_datanode("/tmp/basic/node1")
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


def test_002_stop_postgres():
    node1.stop_postgres()
    assert node1.wait_until_pg_is_running()


def test_003_create_t1():
    node1.run_sql_query("CREATE TABLE t1(a int)")
    node1.run_sql_query("INSERT INTO t1 VALUES (1), (2)")


def test_004_init_secondary():
    global node2
    node2 = cluster.create_datanode("/tmp/basic/node2")

    # register the node on the monitor with a first name for tests
    node2.create(name="node_b")
    eq_(node2.get_nodename(), "node_b")

    # now run the node and change its name again
    node2.run(name="b")
    time.sleep(1)
    eq_(node2.get_nodename(), "b")

    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")
    eq_(
        node1.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_2)",
    )

    assert node1.has_needed_replication_slots()
    assert node2.has_needed_replication_slots()


def test_005_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    eq_(results, [(1,), (2,)])


@raises(Exception)
def test_006_001_writes_to_node2_fail():
    node2.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_006_002_read_from_secondary():
    results = node2.run_sql_query("SELECT * FROM t1")
    assert results == [(1,), (2,)]


def test_007_001_wait_until_primary():
    assert node1.wait_until_state(target_state="primary")


@raises(Exception)
def test_007_002_maintenance_primary():
    node1.enable_maintenance()  # without --allow-failover, that fails


def test_007_003_maintenance_primary():
    assert node1.wait_until_state(target_state="primary")


def test_007_004_maintenance_primary_allow_failover():
    print()
    print("Enabling maintenance on node1, allowing failover")
    node1.enable_maintenance(allowFailover=True)

    assert node1.wait_until_state(target_state="maintenance")
    assert node2.wait_until_state(target_state="wait_primary")

    node2.check_synchronous_standby_names(ssn="")


def test_007_005_disable_maintenance():
    print()
    print("Disabling maintenance on node1")
    node1.disable_maintenance()
    assert node1.wait_until_pg_is_running()
    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

    node2.check_synchronous_standby_names(
        ssn="ANY 1 (pgautofailover_standby_1)"
    )


def test_008_001_enable_maintenance_secondary():
    print()
    print("Enabling maintenance on node2")
    assert node2.wait_until_state(target_state="primary")
    node1.enable_maintenance()
    assert node1.wait_until_state(target_state="maintenance")
    node1.stop_postgres()
    node2.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_008_002_disable_maintenance_secondary():
    print()
    print("Disabling maintenance on node2")
    node1.disable_maintenance()
    assert node1.wait_until_pg_is_running()
    assert node1.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

    node2.check_synchronous_standby_names(
        ssn="ANY 1 (pgautofailover_standby_1)"
    )


# the rest of the tests expect node1 to be primary, make it so
def test_009_failback():
    print()
    monitor.failover()
    assert node2.wait_until_state(target_state="secondary")
    assert node1.wait_until_state(target_state="primary")

    eq_(
        node1.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_2)",
    )


def test_010_fail_primary():
    print()
    print("Injecting failure of node1")
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")


def test_011_writes_to_node2_succeed():
    node2.run_sql_query("INSERT INTO t1 VALUES (4)")
    results = node2.run_sql_query("SELECT * FROM t1 ORDER BY a")
    eq_(results, [(1,), (2,), (3,), (4,)])


def test_012_start_node1_again():
    node1.run()

    assert node2.wait_until_state(target_state="primary")
    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_1)",
    )

    assert node1.wait_until_state(target_state="secondary")


def test_013_read_from_new_secondary():
    results = node1.run_sql_query("SELECT * FROM t1 ORDER BY a")
    eq_(results, [(1,), (2,), (3,), (4,)])


@raises(Exception)
def test_014_writes_to_node1_fail():
    node1.run_sql_query("INSERT INTO t1 VALUES (3)")


def test_015_fail_secondary():
    node1.fail()
    assert node2.wait_until_state(target_state="wait_primary")


def test_016_drop_secondary():
    node1.run()
    assert node1.wait_until_state(target_state="secondary")
    node1.drop()
    assert not node1.pg_is_running()
    assert node2.wait_until_state(target_state="single")

    # replication slot list should be empty now
    assert node2.has_needed_replication_slots()


def test_017_add_new_secondary():
    global node3
    node3 = cluster.create_datanode("/tmp/basic/node3")
    node3.create()


@raises(Exception)
def test_018_cant_failover_yet():
    monitor.failover()


def test_019_run_secondary():
    node3.run()
    assert node3.wait_until_state(target_state="secondary")
    assert node2.wait_until_state(target_state="primary")

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_3)",
    )


# In previous versions of pg_auto_failover we removed the replication slot
# on the secondary after failover. Now, we instead maintain the replication
# slot's replay_lsn thanks for the monitor tracking of the nodes' LSN
# positions.
#
# So rather than checking that we want to zero replication slots after
# replication, we check that we still have a replication slot for the other
# node.
#
def test_020_multiple_manual_failover_verify_replication_slots():
    print()

    print("Calling pgautofailover.failover() on the monitor")
    monitor.failover()
    assert node2.wait_until_state(target_state="secondary")
    assert node3.wait_until_state(target_state="primary")

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()

    eq_(
        node3.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_2)",
    )

    print("Calling pg_autoctl perform promotion on node 2")
    node2.perform_promotion()
    assert node2.wait_until_state(target_state="primary")
    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_3)",
    )

    assert node3.wait_until_state(target_state="secondary")

    assert node2.has_needed_replication_slots()
    assert node3.has_needed_replication_slots()


#
# Now test network partition detection. Cut the primary out of the network
# by means of `ifconfig down` on its virtual network interface, and then
# after 30s the primary should demote itself, and the monitor should
# failover to the secondary.
#
def test_021_ifdown_primary():
    print()
    assert node2.wait_until_state(target_state="primary")
    eq_(
        node2.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_3)",
    )
    node2.ifdown()


def test_022_detect_network_partition():
    # wait for network partition detection to kick-in, allow some head-room
    timeout = 90
    demoted = False

    while not demoted and timeout > 0:
        states = node2.get_local_state()
        demoted = states == ("demote_timeout", "demote_timeout")

        if demoted:
            break

        time.sleep(1)
        timeout -= 1

    if node2.pg_is_running() or timeout <= 0:
        node2.print_debug_logs()
        raise Exception("test failed: node2 didn't stop running in 90s")

    print()
    assert not node2.pg_is_running()
    assert node3.wait_until_state(target_state="wait_primary")
    eq_(node3.get_synchronous_standby_names_local(), "")


def test_023_ifup_old_primary():
    print()
    node2.ifup()

    assert node2.wait_until_pg_is_running()
    assert node2.wait_until_state("secondary")
    assert node3.wait_until_state("primary")

    eq_(
        node3.get_synchronous_standby_names_local(),
        "ANY 1 (pgautofailover_standby_2)",
    )


def test_024_stop_postgres_monitor():
    original_state = node3.get_state().reported
    monitor.stop_postgres()

    # allow trying twice to make Travis CI stable
    if not monitor.wait_until_pg_is_running():
        assert monitor.wait_until_pg_is_running()

    print()
    assert node3.wait_until_state(target_state=original_state)


def test_025_drop_primary():
    node3.drop()
    assert not node3.pg_is_running()
    assert node2.wait_until_state(target_state="single")
