import os
import time

import pgautofailover_utils as pgautofailover
from nose.tools import *

cluster = None

def setup_module():
    global cluster
    cluster = pgautofailover.Cluster()

def teardown_module():
    cluster.monitor.stop_pg_autoctl()
    cluster.destroy()

    del os.environ["PG_AUTOCTL_EXTENSION_VERSION"]

def test_000_create_monitor():
    cluster.create_monitor("/tmp/update/monitor")

def test_001_update_extension():
    os.environ["PG_AUTOCTL_DEBUG"] = '1'
    os.environ["PG_AUTOCTL_EXTENSION_VERSION"] = 'dummy'

    cluster.monitor.run(loglevel=pgautofailover.LogLevel.TRACE)
    cluster.monitor.wait_until_pg_is_running()

    results = cluster.monitor.run_sql_query(
        """SELECT installed_version
             FROM pg_available_extensions
            WHERE name = 'pgautofailover'
        """)
    version = results[0][0]

    if version != 'dummy':
        out, err, ret = cluster.monitor.pg_autoctl.stop()
        print("Monitor exit code %d:\n\n%s\n%s" % (ret, out, err))

    assert version == 'dummy'
