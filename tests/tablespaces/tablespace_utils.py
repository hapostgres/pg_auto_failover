import time
from tests.pgautofailover_utils import QueryRunner
from tests.pgautofailover_utils import StatefulNode


class PGNodeTS(QueryRunner):
    def run_sql_query(self, query, *args):
        return super().run_sql_query(query, True, *args)

    def connection_string(self):
        return "postgresql://%s@%s:%s/%s" % (
            self.username,
            self.service_name,
            self.port,
            self.database,
        )


class MonitorNodeTS(PGNodeTS):
    def __init__(self, port, service_name):
        self.port = port
        self.service_name = service_name
        self.monitor = self
        self.username = "autoctl_node"
        self.database = "pg_auto_failover"


class DataNodeTS(PGNodeTS, StatefulNode):
    def __init__(self, port, service_name, monitor_node):
        self.port = port
        self.service_name = service_name
        self.monitor = monitor_node
        self.username = "docker"
        self.database = "postgres"

    def logger_name(self):
        return self.service_name

    def sleep(self, sleep_time):
        time.sleep(sleep_time)

    def print_debug_logs(self):
        # no-op, can't get logs easily in this test-type
        return

    def get_state(self):
        return super().get_state(
            "node with port %s not found on the monitor" % self.port,
            """
    SELECT reportedstate, goalstate
    FROM pgautofailover.node
    WHERE nodeport=%s
    """,
            self.port,
        )


monitor = MonitorNodeTS(
    "5432",
    "monitor",
)
node1 = DataNodeTS(
    "5433",
    "node1",
    monitor,
)
node2 = DataNodeTS(
    "5434",
    "node2",
    monitor,
)
