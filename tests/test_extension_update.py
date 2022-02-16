import os
import time

import tests.pgautofailover_utils as pgautofailover
from nose.tools import eq_

cluster = None


def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()


def teardown_module():
    cluster.monitor.stop_pg_autoctl()
    cluster.destroy()


def test_000_create_monitor():
    monitor = cluster.create_monitor("/tmp/update/monitor")


def test_001_update_extension():
    os.environ["PG_AUTOCTL_DEBUG"] = "1"
    os.environ["PG_AUTOCTL_EXTENSION_VERSION"] = "dummy"

    cluster.monitor.run()

    cluster.monitor.wait_until_pg_is_running()

    # Wait until extension is installed
    time.sleep(1)

    results = cluster.monitor.run_sql_query(
        """SELECT installed_version
             FROM pg_available_extensions
            WHERE name = 'pgautofailover'
        """
    )

    if results[0][0] != "dummy":
        cluster.monitor.print_debug_logs()

    eq_(results, [("dummy",)])

    del os.environ["PG_AUTOCTL_EXTENSION_VERSION"]
    assert "PG_AUTOCTL_EXTENSION_VERSION" not in os.environ
