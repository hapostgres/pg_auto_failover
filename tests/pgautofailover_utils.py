import os
import os.path
import signal
import shutil
import time
import tests.network as network
import psycopg2
import psycopg2.errors
import subprocess
import datetime as dt
from collections import namedtuple
from nose.tools import eq_
from enum import Enum
import json

import tests.ssl_cert_utils as cert

COMMAND_TIMEOUT = network.COMMAND_TIMEOUT
POLLING_INTERVAL = 0.1
STATE_CHANGE_TIMEOUT = 90
PGVERSION = os.getenv("PGVERSION", "11")

NodeState = namedtuple("NodeState", "reported assigned")


# Append stderr output to default CalledProcessError message
class CalledProcessError(subprocess.CalledProcessError):
    def __str__(self):
        return super().__str__() + "\n\t" + self.stderr


class Role(Enum):
    Monitor = 1
    Postgres = 2
    Coordinator = 3
    Worker = 4

    def command(self):
        return self.name.lower()


class Feature(Enum):
    Secondary = 1

    def command(self):
        return self.name.lower()


class Cluster:
    # Docker uses 172.17.0.0/16 by default, so we use 172.27.1.0/24 to not
    # conflict with that.
    def __init__(
        self, networkNamePrefix="pgauto", networkSubnet="172.27.1.0/24"
    ):
        """
        Initializes the environment, virtual network, and other local state
        necessary for operation of the Cluster.
        """
        os.environ["PG_REGRESS_SOCK_DIR"] = ""
        os.environ["PG_AUTOCTL_DEBUG"] = ""
        os.environ["PGHOST"] = "localhost"
        self.networkSubnet = networkSubnet
        self.vlan = network.VirtualLAN(networkNamePrefix, networkSubnet)
        self.monitor = None
        self.datanodes = []

    def create_monitor(
        self,
        datadir,
        port=5432,
        hostname=None,
        authMethod=None,
        sslMode=None,
        sslSelfSigned=False,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        """
        Initializes the monitor and returns an instance of MonitorNode.
        """
        if self.monitor is not None:
            raise Exception("Monitor has already been created.")
        vnode = self.vlan.create_node()
        self.monitor = MonitorNode(
            self,
            datadir,
            vnode,
            port,
            hostname,
            authMethod,
            sslMode,
            sslSelfSigned,
            sslCAFile=sslCAFile,
            sslServerKey=sslServerKey,
            sslServerCert=sslServerCert,
        )
        self.monitor.create()
        return self.monitor

    # TODO group should auto sense for normal operations and passed to the
    # create cli as an argument when explicitly set by the test
    def create_datanode(
        self,
        datadir,
        port=5432,
        group=0,
        listen_flag=False,
        role=Role.Postgres,
        formation=None,
        authMethod=None,
        sslMode=None,
        sslSelfSigned=False,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        """
        Initializes a data node and returns an instance of DataNode. This will
        do the "keeper init" and "pg_autoctl run" commands.
        """
        vnode = self.vlan.create_node()
        nodeid = len(self.datanodes) + 1

        dbname = "postgres"
        if role == Role.Coordinator or role == Role.Worker:
            dbname = "citus"

        datanode = DataNode(
            self,
            datadir,
            vnode,
            port,
            os.getenv("USER"),
            authMethod,
            dbname,
            self.monitor,
            nodeid,
            group,
            listen_flag,
            role,
            formation,
            sslMode=sslMode,
            sslSelfSigned=sslSelfSigned,
            sslCAFile=sslCAFile,
            sslServerKey=sslServerKey,
            sslServerCert=sslServerCert,
        )
        self.datanodes.append(datanode)
        return datanode

    def pg_createcluster(self, datadir, port=5432):
        """
        Initializes a postgresql node using pg_createcluster and returns
        directory path.
        """
        vnode = self.vlan.create_node()

        create_command = [
            "sudo",
            shutil.which("pg_createcluster"),
            "--user",
            os.getenv("USER"),
            "--group",
            "postgres",
            "-p",
            str(port),
            PGVERSION,
            datadir,
            "--",
            "--auth-local",
            "trust",
        ]

        print("%s" % " ".join(create_command))

        vnode.run_and_wait(create_command, "pg_createcluster")

        abspath = os.path.join("/var/lib/postgresql/", PGVERSION, datadir)

        chmod_command = [
            "sudo",
            shutil.which("install"),
            "-d",
            "-o",
            os.getenv("USER"),
            "/var/lib/postgresql/%s/backup" % PGVERSION,
        ]

        print("%s" % " ".join(chmod_command))
        vnode.run_and_wait(chmod_command, "chmod")

        return abspath

    def destroy(self, force=True):
        """
        Cleanup whatever was created for this Cluster.
        """
        for datanode in list(reversed(self.datanodes)):
            datanode.destroy(force=force, ignore_failure=True, timeout=3)
        if self.monitor:
            self.monitor.destroy()
        self.vlan.destroy()

    def nodes(self):
        """
        Returns a list of all nodes in the cluster including the monitor

        NOTE: Monitor is explicitly last in this list. So this list of nodes
        can be stopped in order safely.
        """
        nodes = self.datanodes.copy()
        if self.monitor:
            nodes.append(self.monitor)
        return nodes

    def flush_output(self):
        """
        flush the output for all running pg_autoctl processes in the cluster
        """
        for node in self.nodes():
            node.flush_output()

    def sleep(self, secs):
        """
        sleep for the specified time while flushing output of the cluster at
        least every second
        """
        full_secs = int(secs)

        for i in range(full_secs):
            self.flush_output()
            time.sleep(1)

        self.flush_output()
        time.sleep(secs - full_secs)

    def communicate(self, proc, timeout):
        """
        communicate with the process with the specified timeout while flushing
        output of the cluster at least every second
        """
        full_secs = int(timeout)

        for i in range(full_secs):
            self.flush_output()
            try:
                # wait until process is done for one second each iteration
                # of this loop, to add up to the actual timeout argument
                return proc.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                pass

        self.flush_output()
        return proc.communicate(timeout=timeout - full_secs)

    def create_root_cert(self, directory, basename="root", CN="root"):
        self.cert = cert.SSLCert(directory, basename, CN)
        self.cert.create_root_cert()


class QueryRunner:
    def connection_string(self):
        raise NotImplementedError

    def run_sql_query(self, query, autocommit, *args):
        """
        Runs the given sql query with the given arguments in this postgres node
        and returns the results. Returns None if there are no results to fetch.
        """
        result = None
        conn = psycopg2.connect(self.connection_string())
        conn.autocommit = autocommit

        try:
            with conn.cursor() as cur:
                cur.execute(query, args)
                try:
                    result = cur.fetchall()
                except psycopg2.ProgrammingError:
                    pass
            if not autocommit:
                conn.commit()
        except Exception:
            if not autocommit:
                conn.rollback()
            raise
        finally:
            conn.close()

        return result

    def alter_system_set(self, gucs):
        """
        Calls ALTER SYSTEM SET on the provided GUCs, then pg_reload_conf().
        """
        psql_command = [shutil.which("psql"), "-d", self.database, "-c"]
        for key in gucs:
            sql = "alter system set %s = %s" % (key, gucs[key])
            command = psql_command + [sql]
            self.vnode.run_and_wait(command, name="alter system set")

        sql = "select pg_reload_conf()"
        command = psql_command + [sql]
        self.vnode.run_and_wait(command, name="alter system set")


class PGNode(QueryRunner):
    """
    Common stuff between MonitorNode and DataNode.
    """

    def __init__(
        self,
        cluster,
        datadir,
        vnode,
        port,
        username,
        authMethod,
        database,
        role,
        sslMode=None,
        sslSelfSigned=False,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        self.cluster = cluster
        self.datadir = datadir
        self.vnode = vnode
        self.port = port
        self.username = username
        self.authMethod = authMethod or "trust"
        self.database = database
        self.role = role
        self.pg_autoctl = None
        self.authenticatedUsers = {}
        self._pgversion = None
        self._pgmajor = None
        self.sslMode = sslMode
        self.sslSelfSigned = sslSelfSigned
        self.sslCAFile = sslCAFile
        self.sslServerKey = sslServerKey
        self.sslServerCert = sslServerCert

        self._pgversion = None
        self._pgmajor = None

    def connection_string(self):
        """
        Returns a connection string which can be used to connect to this postgres
        node.
        """
        host = self.vnode.address

        if self.authMethod and self.username in self.authenticatedUsers:
            dsn = "postgres://%s:%s@%s:%d/%s" % (
                self.username,
                self.authenticatedUsers[self.username],
                host,
                self.port,
                self.database,
            )
        else:
            dsn = "postgres://%s@%s:%d/%s" % (
                self.username,
                host,
                self.port,
                self.database,
            )

        # If a local CA is used, or even a self-signed certificate,
        # using verify-ca often provides enough protection.
        if self.sslMode:
            sslMode = self.sslMode
        elif self.sslSelfSigned:
            sslMode = "require"
        else:
            sslMode = "prefer"

        dsn += "?sslmode=%s" % sslMode

        if self.sslCAFile:
            dsn += f"&sslrootcert={self.sslCAFile}"

        return dsn

    def run(self, env={}, name=None, host=None, port=None):
        """
        Runs "pg_autoctl run"
        """
        self.name = name

        self.pg_autoctl = PGAutoCtl(self)
        self.pg_autoctl.run(name=name, host=host, port=port)

    def wait_until_pg_autoctl_is_running(self, timeout=STATE_CHANGE_TIMEOUT):
        """
        Polls until pg_autoctl has written its config file (i.e. its
        initialization is far enough that inspect/state commands work).
        Raises if the config file is not present within timeout seconds.
        """
        deadline = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while dt.datetime.now() < deadline:
            if os.path.exists(self.config_file_path()):
                return
            time.sleep(POLLING_INTERVAL)
        raise Exception(
            "pg_autoctl config file for %s not found after %d seconds"
            % (self.datadir, timeout)
        )

    def running(self):
        return self.pg_autoctl and self.pg_autoctl.run_proc

    def flush_output(self):
        """
        Flushes the output of pg_autoctl if it's running to be sure that it
        does not get stuck, because of a filled up pipe.
        """
        if self.running():
            self.pg_autoctl.consume_output(0.001)

    def sleep(self, secs):
        """
        Sleep for the specfied amount of seconds but meanwile consume output of
        the pg_autoctl process to make sure it does not lock up.
        """
        if self.running():
            self.pg_autoctl.consume_output(secs)
        else:
            time.sleep(secs)

    def run_sql_query(self, query, *args):
        return super().run_sql_query(query, False, *args)

    def run_sql_query_retry(self, query, *args, timeout=30):
        """
        Like run_sql_query but retries on transient connectivity errors such as
        "the database system is starting up" (ERRCODE 57P03, CannotConnectNow)
        or plain OperationalError (connection refused / reset).  Retries for up
        to timeout seconds with 0.5s back-off.
        """
        deadline = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while True:
            try:
                return self.run_sql_query(query, *args)
            except (psycopg2.OperationalError,
                    psycopg2.errors.CannotConnectNow) as e:
                if dt.datetime.now() >= deadline:
                    raise
                print(
                    "transient connectivity error on %s (%s), retrying ..."
                    % (self.datadir, e)
                )
                time.sleep(0.5)

    def citus_run_ddl_after_sync(self, query, timeout=60):
        """
        Run a Citus DDL statement (e.g. DROP TABLE on a distributed table)
        after waiting for metadata to be in sync on all nodes.

        wait_until_metadata_sync() covers the coordinator's view of sync, but
        a recently-rejoined worker node may still be applying metadata updates
        when the coordinator considers sync complete.  If the DDL fails with
        ObjectNotInPrerequisiteState ("is a metadata node, but is out of sync"),
        wait again and retry the DDL for up to timeout seconds.
        """
        deadline = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while True:
            self.run_sql_query("select public.wait_until_metadata_sync()")
            try:
                return self.run_sql_query(query)
            except psycopg2.errors.ObjectNotInPrerequisiteState as e:
                if dt.datetime.now() >= deadline:
                    raise
                print(
                    "Citus metadata not yet in sync on %s (%s), retrying ..."
                    % (self.datadir, e)
                )
                time.sleep(1)

    def pg_config_get(self, settings):
        """
        Returns the current value of the given postgres settings"
        """
        if isinstance(settings, str):
            return self.run_sql_query(f"SHOW {setting}")[0][0]

        else:
            # we have a list of settings to grab
            s = {}
            q = "select name, setting from pg_settings where name = any(%s)"
            for name, setting in self.run_sql_query(q, settings):
                s[name] = setting

            return s

    def set_user_password(self, username, password):
        """
        Sets user passwords on the PGNode
        """
        alter_user_set_passwd_command = "alter user %s with password '%s'" % (
            username,
            password,
        )
        passwd_command = [
            shutil.which("psql"),
            "-d",
            self.database,
            "-c",
            alter_user_set_passwd_command,
        ]
        self.vnode.run_and_wait(passwd_command, name="user passwd")
        self.authenticatedUsers[username] = password

    def stop_pg_autoctl(self, sig=signal.SIGTERM):
        """
        Kills the keeper by sending "sig" to keeper's process group. Default
        SIGTERM is a graceful stop, used by tests that want a plain
        restart (not a simulated failure -- see DataNode.fail() for that).
        See PGAutoCtl.stop() for what signal choice controls.
        """
        if self.pg_autoctl:
            return self.pg_autoctl.stop(sig)

    def stop_postgres(self):
        """
        Stops the postgres process by running:
          pg_ctl -D ${self.datadir} --wait --mode fast stop
        """
        # pg_ctl stop is racey when another process is trying to start postgres
        # again in the background. It will not finish in that case. pg_autoctl
        # does this, so we try stopping postgres a couple of times. This way we
        # make sure the race does not impact our tests.
        #
        # The race can be easily reproduced by in one shell doing:
        #    while true; do pg_ctl --pgdata monitor/ start; done
        # And in another:
        #    pg_ctl --pgdata monitor --wait --mode fast stop
        # The second command will not finish, since the first restarts postgres
        # before the second finds out it has been killed.

        for i in range(60):
            stop_command = [
                shutil.which("pg_ctl"),
                "-D",
                self.datadir,
                "--wait",
                "--mode",
                "fast",
                "stop",
            ]
            try:
                with self.vnode.run(stop_command) as stop_proc:
                    out, err = stop_proc.communicate(timeout=1)
                    if stop_proc.returncode > 0:
                        print(
                            "stopping postgres for '%s' failed, out: %s\n, err: %s"
                            % (self.vnode.address, out, err)
                        )
                        return False
                    elif stop_proc.returncode is None:
                        print("stopping postgres for '%s' timed out")
                        return False
                    return True
            except subprocess.TimeoutExpired:
                pass
        else:
            raise Exception("Postgres could not be stopped after 60 attempts")

    def reload_postgres(self):
        """
        Reload the postgres configuration by running:
          pg_ctl -D ${self.datadir} reload
        """
        reload_command = [shutil.which("pg_ctl"), "-D", self.datadir, "reload"]
        with self.vnode.run(reload_command) as reload_proc:
            out, err = self.cluster.communicate(reload_proc, COMMAND_TIMEOUT)
            if reload_proc.returncode > 0:
                print(
                    "reloading postgres for '%s' failed, out: %s\n, err: %s"
                    % (self.vnode.address, out, err)
                )
                return False
            elif reload_proc.returncode is None:
                print("reloading postgres for '%s' timed out")
                return False
            return True

    def restart_postgres(self):
        """
        Restart Postgres with pg_autoctl manual service restart postgres
        """
        command = PGAutoCtl(self)

        command.execute(
            "service restart postgres", "manual", "service", "restart", "postgres"
        )

    def pg_is_running(self, timeout=COMMAND_TIMEOUT):
        """
        Returns true when Postgres is running. We use pg_ctl status.
        """
        command = PGAutoCtl(self)

        try:
            command.execute("pgsetup ready", "inspect", "pgsetup", "ready", "-vvv")
        except Exception as e:
            # pg_autoctl uses EXIT_CODE_PGSQL when Postgres is not ready
            return False
        return True

    def wait_until_pg_is_running(self, timeout=STATE_CHANGE_TIMEOUT):
        """
        Waits until the underlying Postgres process is running.
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "pgsetup ready",
            "inspect",
            "pgsetup",
            "wait",
            "-vvv",
            "--timeout",
            str(timeout),
            timeout=timeout + 10,
        )

        return ret == 0

    def fail(self, sig=signal.SIGQUIT):
        """
        Simulates a data node failure by terminating the keeper and stopping
        postgres.

        Default SIGQUIT exits immediately, without running pg_autoctl's own
        signal handler cleanup (see PGAutoCtl.stop()) -- the closest
        simulation of a real crash. SIGTERM is no longer a hard-crash
        simulation: it now triggers a graceful shutdown that, for a primary,
        requests maintenance so a standby can take over immediately (see
        keeper_shutdown_via_maintenance() in service_keeper.c); most tests
        asserting on a node being permanently excluded from candidate
        selection right after a failure need the default here, not SIGTERM.

        If a specific test turns out to be flaky with SIGQUIT (too little
        control over exactly how abruptly the process exits), pass
        sig=signal.SIGINT instead: it is still an immediate exit request that
        bypasses the graceful/maintenance path, but runs one more loop
        iteration and a clean libpq disconnect before exiting, which can be
        easier to reason about under test-timing pressure.
        """
        self.stop_pg_autoctl(sig)

        # stopping pg_autoctl also stops Postgres, unless bugs.
        if self.pg_is_running():
            self.stop_postgres()

    def config_file_path(self):
        """
        Returns the path of the config file for this data node.
        """
        # Config file is located at:
        # ~/.config/pg_autoctl/${PGDATA}/pg_autoctl.cfg
        home = os.getenv("HOME")
        pgdata = os.path.abspath(self.datadir)[1:]  # Remove the starting '/'
        return os.path.join(
            home, ".config/pg_autoctl", pgdata, "pg_autoctl.cfg"
        )

    def state_file_path(self):
        """
        Returns the path of the state file for this data node.
        """
        # State file is located at:
        # ~/.local/share/pg_autoctl/${PGDATA}/pg_autoctl.state
        home = os.getenv("HOME")
        pgdata = os.path.abspath(self.datadir)[1:]  # Remove the starting '/'
        return os.path.join(
            home, ".local/share/pg_autoctl", pgdata, "pg_autoctl.state"
        )

    def get_postgres_logs(self):
        ldir = os.path.join(self.datadir, "log")
        try:
            logfiles = os.listdir(ldir)
        except FileNotFoundError:
            # If the log directory does not exist then there's also no logs to
            # display
            return ""
        logfiles.sort()

        logs = []
        for logfile in logfiles:
            logs += ["\n\n%s:\n" % logfile]
            logs += open(os.path.join(ldir, logfile)).readlines()

        # it's not really logs but we want to see that too
        for inc in [
            "recovery.conf",
            "postgresql.auto.conf",
            "postgresql-auto-failover.conf",
            "postgresql-auto-failover-standby.conf",
        ]:
            conf = os.path.join(self.datadir, inc)
            if os.path.isfile(conf):
                logs += ["\n\n%s:\n" % conf]
                logs += open(conf).readlines()
            else:
                logs += ["\n\n%s does not exist\n" % conf]

        return "".join(logs)

    def pgversion(self):
        """
        Query local Postgres for its version. Cache the result.
        """
        if self._pgversion:
            return self._pgversion

        # server_version_num is 110005 for 11.5
        self._pgversion = int(
            self.run_sql_query("show server_version_num")[0][0]
        )
        self._pgmajor = self._pgversion // 10000

        return self._pgversion

    def pgmajor(self):
        if self._pgmajor:
            return self._pgmajor

        self.pgversion()
        return self._pgmajor

    def ifdown(self):
        """
        Bring the network interface down for this node
        """
        self.vnode.ifdown()

    def ifup(self):
        """
        Bring the network interface up for this node
        """
        self.vnode.ifup()

    def config_set(self, setting, value):
        """
        Set a configuration parameter to given value
        """
        command = PGAutoCtl(self)
        command.execute(
            "config set %s" % setting, "config", "set", setting, value
        )
        return True

    def config_get(self, setting):
        """
        Set a configuration parameter to given value
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "config get %s" % setting, "config", "get", setting
        )
        return out[:-1]

    def show_uri(self, json=False):
        """
        Runs pg_autoctl show uri
        """
        command = PGAutoCtl(self)
        if json:
            out, err, ret = command.execute("show uri", "show", "uri", "--json")
        else:
            out, err, ret = command.execute("show uri", "show", "uri")
        return out

    def logs(self, log_type=""):
        log_string = ""
        if self.running():
            out, err, ret = self.stop_pg_autoctl()
            if not log_type or (log_type == "STDOUT"):
                log_string += f"STDOUT OF PG_AUTOCTL FOR {self.datadir}:\n"
                log_string += f"{self.pg_autoctl.cmd}\n{out}\n"
            if not log_type or (log_type == "STDERR"):
                log_string += (
                    f"STDERR OF PG_AUTOCTL FOR {self.datadir}:\n{err}\n"
                )
        if not log_type or (log_type == "POSTGRES"):
            pglogs = self.get_postgres_logs()
            log_string += f"POSTGRES LOGS FOR {self.datadir}:\n{pglogs}\n"
        return log_string

    def get_events_str(self):
        "2020-08-03 12:04:41.513761+00:00"
        events = self.get_events()

        if events:
            return "\n".join(
                [
                    "%32s %15s %17s/%-17s %10s %10s %s"
                    % (
                        "eventtime",
                        "name",
                        "state",
                        "goal state",
                        "repl st",
                        "tli:lsn",
                        "event",
                    )
                ]
                + [
                    "%32s %8s %17s/%-17s %10s %3s:%7s %s" % result
                    for result in events
                ]
            )
        else:
            return ""

    def print_debug_logs(self):
        events = self.get_events_str()
        logs = f"MONITOR EVENTS:\n{events}\n"

        for node in self.cluster.nodes():
            logs += node.logs()
        print(logs)

        # we might be running with the monitor disabled
        if self.cluster.monitor and self.cluster.monitor.pg_autoctl:
            print("%s" % self.cluster.monitor.pg_autoctl.err)

    def enable_ssl(
        self,
        sslMode=None,
        sslSelfSigned=None,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        """
        Enables SSL on a pg_autoctl node
        """
        self.sslMode = sslMode
        self.sslSelfSigned = sslSelfSigned
        self.sslCAFile = sslCAFile
        self.sslServerKey = sslServerKey
        self.sslServerCert = sslServerCert

        ssl_args = ["enable", "ssl", "-vvv", "--pgdata", self.datadir]

        if self.sslMode:
            ssl_args += ["--ssl-mode", self.sslMode]

        if self.sslSelfSigned:
            ssl_args += ["--ssl-self-signed"]

        if self.sslCAFile:
            ssl_args += ["--ssl-ca-file", self.sslCAFile]

        if self.sslServerKey:
            ssl_args += ["--server-key", self.sslServerKey]

        if self.sslServerCert:
            ssl_args += ["--server-cert", self.sslServerCert]

        if not self.sslSelfSigned and not self.sslServerKey:
            ssl_args += ["--no-ssl"]

        command = PGAutoCtl(self, argv=ssl_args)
        out, err, ret = command.execute("enable ssl")

    def get_monitor_uri(self):
        """
        pg_autoctl show uri --monitor
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "show uri --monitor", "show", "uri", "--formation", "monitor"
        )
        return out

    def get_formation_uri(self, formationName="default"):
        """
        pg_autoctl show uri --formation {formationName}
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "show uri --formation", "show", "uri", "--formation", formationName
        )
        return out

    def check_conn_string_ssl(self, conn_string, sslmode):
        """
        Asserts that given connection string embeds expected SSL settings.
        """
        crl = None
        rootCert = self.sslCAFile

        print("checking connstring =", conn_string)
        assert f"sslmode={sslmode}" in conn_string
        if rootCert:
            assert f"sslrootcert={rootCert}" in conn_string
        if crl:
            assert f"sslcrl={crl}" in conn_string

    def check_ssl(self, ssl, sslmode, monitor=False, primary=False):
        """
        Checks if ssl settings match how the node is set up
        """
        key = self.sslServerKey
        crt = self.sslServerCert
        crl = None
        rootCert = self.sslCAFile
        if self.sslSelfSigned:
            key = os.path.join(self.datadir, "server.key")
            crt = os.path.join(self.datadir, "server.crt")

        # grab all the settings we want to check in a single round-trip
        pg_settings_names = [
            "ssl",
            "ssl_ciphers",
            "ssl_key_file",
            "ssl_cert_file",
            "ssl_crl_file",
            "ssl_ca_file",
        ]

        if self.pgmajor() >= 12:
            pg_settings_names += ["primary_conninfo"]

        pg_settings = self.pg_config_get(pg_settings_names)

        eq_(pg_settings["ssl"], ssl)
        eq_(self.config_get("ssl.sslmode"), sslmode)

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

        # TODO: also do this for monitor once we can have superuser access to
        # the monitor
        if not monitor:
            eq_(pg_settings["ssl_ciphers"], expected_ciphers)

        monitor_uri = self.get_monitor_uri()
        self.check_conn_string_ssl(monitor_uri, sslmode)

        if not monitor:
            monitor_uri = self.config_get("pg_autoctl.monitor")
            self.check_conn_string_ssl(monitor_uri, sslmode)

            formation_uri = self.get_formation_uri()
            self.check_conn_string_ssl(formation_uri, sslmode)

        for pg_setting, autoctl_setting, file_path in [
            ("ssl_key_file", "ssl.key_file", key),
            ("ssl_cert_file", "ssl.cert_file", crt),
            ("ssl_crl_file", "ssl.crl_file", crl),
            ("ssl_ca_file", "ssl.ca_file", rootCert),
        ]:
            if file_path is None:
                continue
            assert os.path.isfile(file_path)
            print("checking", pg_setting)
            eq_(pg_settings[pg_setting], file_path)
            eq_(self.config_get(autoctl_setting), file_path)

        if monitor or primary:
            return

        if self.pgmajor() >= 12:
            self.check_conn_string_ssl(pg_settings["primary_conninfo"], sslmode)

    def editedHBA(self):
        """
        Returns True when pg_autoctl has edited the HBA file found in datadir,
        False otherwise.
        """
        editedHBA = False
        hbaFilePath = os.path.join(self.datadir, "pg_hba.conf")

        with open(hbaFilePath, "r") as hba:
            lines = hba.readlines()

            for line in lines:
                if line == "":
                    continue

                if line[0] == "#":
                    continue

                if "# Auto-generated by pg_auto_failover" in line:
                    # make the output easier to follow
                    if editedHBA is False:
                        print()

                    # do not print the ending \n in line
                    print("Edited HBA line: %s" % line[:-1])
                    editedHBA = True

        return editedHBA


class StatefulNode:
    def logger_name(self):
        raise NotImplementedError

    def sleep(self, sleep_time):
        raise NotImplementedError

    def print_debug_logs(self):
        raise NotImplementedError

    def wait_until_state(
        self,
        target_state,
        timeout=STATE_CHANGE_TIMEOUT,
        sleep_time=POLLING_INTERVAL,
    ):
        """
        Waits until this node reaches the target state, and then returns
        True. If this doesn't happen until "timeout" seconds, returns False.
        """
        prev_state = None
        wait_until = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while wait_until > dt.datetime.now():
            self.sleep(sleep_time)

            try:
                current_state, assigned_state = self.get_state()
            except Exception:
                continue

            # only log the state if it has changed
            if current_state != prev_state:
                if current_state == target_state:
                    print(
                        "state of %s is '%s', done waiting"
                        % (self.logger_name(), current_state)
                    )
                else:
                    print(
                        "state of %s is '%s', waiting for '%s' ..."
                        % (self.logger_name(), current_state, target_state)
                    )

            if current_state == target_state:
                return True

            prev_state = current_state

        print(
            "%s didn't reach %s after %d seconds"
            % (self.logger_name(), target_state, timeout)
        )
        error_msg = (
            f"{self.logger_name()} failed to reach {target_state} "
            f"after {timeout} seconds\n"
        )
        self.print_debug_logs()
        raise Exception(error_msg)

    def wait_until_assigned_state(
        self,
        target_state,
        timeout=STATE_CHANGE_TIMEOUT,
        sleep_time=POLLING_INTERVAL,
        or_state=None,
    ):
        """
        Waits until this data node is assigned the target state. Typically used
        when the node has been stopped or failed and we want to check the
        monitor FSM.

        Pass or_state to accept either target_state or or_state as success —
        useful when the FSM can skip through a transient state faster than the
        poll interval (e.g. draining vs demote_timeout).
        """
        target_states = {target_state}
        if or_state is not None:
            target_states.add(or_state)

        label = " or ".join(sorted(target_states))
        prev_state = None
        wait_until = dt.datetime.now() + dt.timedelta(seconds=timeout)

        while wait_until > dt.datetime.now():
            self.cluster.sleep(sleep_time)

            try:
                current_state, assigned_state = self.get_state()
            except Exception:
                continue

            # only log the state if it has changed
            if assigned_state != prev_state:
                if assigned_state in target_states:
                    print(
                        "assigned state of %s is '%s', done waiting"
                        % (self.datadir, assigned_state)
                    )
                else:
                    print(
                        "assigned state of %s is '%s', waiting for '%s' ..."
                        % (self.datadir, assigned_state, label)
                    )

            if assigned_state in target_states:
                return True

            prev_state = assigned_state

        print(
            "%s didn't reach %s after %d seconds"
            % (self.logger_name(), label, timeout)
        )
        error_msg = (
            f"{self.logger_name()} failed to reach {label} "
            f"after {timeout} seconds\n"
        )
        self.print_debug_logs()
        raise Exception(error_msg)

    def get_state(self, not_found_message, query, *args):
        """
        Returns the current state of the data node. This is done by querying the
        monitor node.
        """
        results = self.monitor.run_sql_query(query, *args)

        if len(results) == 0:
            raise Exception(not_found_message)
        else:
            res = NodeState(results[0][0], results[0][1])
            return res

        # default case, unclean when reached
        return NodeState(None, None)


class DataNode(PGNode, StatefulNode):
    def __init__(
        self,
        cluster,
        datadir,
        vnode,
        port,
        username,
        authMethod,
        database,
        monitor,
        nodeid,
        group,
        listen_flag,
        role,
        formation,
        sslMode=None,
        sslSelfSigned=False,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        super().__init__(
            cluster,
            datadir,
            vnode,
            port,
            username,
            authMethod,
            database,
            role,
            sslMode=sslMode,
            sslSelfSigned=sslSelfSigned,
            sslCAFile=sslCAFile,
            sslServerKey=sslServerKey,
            sslServerCert=sslServerCert,
        )
        self.monitor = monitor
        self.nodeid = nodeid
        self.group = group
        self.listen_flag = listen_flag
        self.formation = formation
        self.monitorDisabled = None

    def run_and_wait_with_retry(
        self,
        target_state,
        timeout=STATE_CHANGE_TIMEOUT,
        retries=3,
        name=None,
        host=None,
        port=None,
    ):
        """
        Runs pg_autoctl and waits for this node to reach target_state,
        restarting (not just re-waiting) up to `retries` times if it
        doesn't get there.

        This exists for restarting a node after a hard-crash simulation
        (node.fail()): Postgres occasionally fails to (re)bind its port
        with "no socket created for listening" even though nothing else
        holds it -- suspected to be a pyroute2/network-namespace-
        attachment race in this test harness's per-node namespace
        simulation, not a pg_autoctl bug, since the port is confirmed
        free at the OS level when this happens. pg_autoctl's own keeper
        does not retry that internally, so plain node.run() can get
        stuck. Use this instead of a plain node.run() wherever a test
        restarts a node that was previously killed hard.

        Deliberately polls directly (like wait_until_state() does
        internally) rather than calling wait_until_state() itself: despite
        its docstring ("returns False" on timeout), it actually *raises* on
        timeout, and before doing so it calls print_debug_logs(), which
        iterates every node in the cluster and stop_pg_autoctl()'s any that
        are still running -- a much bigger side effect than "log something
        and let the caller retry". Since this method only restarts *this*
        node on the next attempt, that would silently leave every other
        node in the cluster stopped and never restarted, turning one node's
        transient bind race into the whole cluster wedging. Polling
        directly avoids that side effect entirely.
        """
        per_attempt_timeout = max(timeout // retries, 30)

        for attempt in range(1, retries + 1):
            self.run(name=name, host=host, port=port)

            deadline = dt.datetime.now() + dt.timedelta(
                seconds=per_attempt_timeout
            )
            reached = False

            while dt.datetime.now() < deadline:
                self.sleep(POLLING_INTERVAL)

                try:
                    current_state, _ = self.get_state()
                except Exception:
                    continue

                if current_state == target_state:
                    reached = True
                    break

            if reached:
                return True

            print(
                "%s did not reach '%s' within %ds (attempt %d/%d), "
                "restarting and retrying"
                % (
                    self.logger_name(),
                    target_state,
                    per_attempt_timeout,
                    attempt,
                    retries,
                )
            )

            # the stuck attempt's pg_autoctl may not be responsive to a
            # plain stop if it's wedged on the failed bind; fail() (SIGQUIT,
            # then a direct Postgres stop if needed) is more likely to
            # actually free the pgdata lock before the next attempt.
            #
            # fail() itself can raise subprocess.TimeoutExpired here: its
            # underlying Cluster.communicate() waits up to its own timeout
            # in 1-second steps, then makes one last attempt with whatever
            # is left of the budget, which by then can be ~0 (or a tiny
            # negative float from rounding) and raise even though the
            # SIGQUIT'd process is, in practice, already gone -- that's a
            # pre-existing quirk of that helper, unrelated to the bind race
            # this method exists for. Don't let it abort the retry loop.
            try:
                self.fail()
            except subprocess.TimeoutExpired as e:
                print(
                    "%s: cleanup before retry raised %s (continuing anyway)"
                    % (self.logger_name(), e)
                )

        return False

    def create(
        self,
        run=False,
        level="-v",
        name=None,
        host=None,
        port=None,
        candidatePriority=None,
        replicationQuorum=None,
        monitorDisabled=False,
        nodeId=None,
        citusSecondary=False,
        citusClusterName="default",
    ):
        """
        Runs "pg_autoctl create"
        """
        pghost = "localhost"
        sockdir = os.environ["PG_REGRESS_SOCK_DIR"]

        if self.listen_flag:
            pghost = str(self.vnode.address)

        if sockdir and sockdir != "":
            pghost = sockdir

        if monitorDisabled:
            self.monitorDisabled = True

        # don't pass --hostname to Postgres nodes in order to exercise the
        # automatic detection of the hostname.
        create_args = [
            "create",
            self.role.command(),
            level,
            "--pgdata",
            self.datadir,
            "--pghost",
            pghost,
            "--pgport",
            str(self.port),
            "--pgctl",
            shutil.which("pg_ctl"),
        ]

        if self.authMethod == "skip":
            create_args += ["--skip-pg-hba"]
        else:
            create_args += ["--auth", self.authMethod]

        if not self.monitorDisabled:
            create_args += ["--monitor", self.monitor.connection_string()]

        if self.sslMode:
            create_args += ["--ssl-mode", self.sslMode]

        if self.sslSelfSigned:
            create_args += ["--ssl-self-signed"]

        if self.sslCAFile:
            create_args += ["--ssl-ca-file", self.sslCAFile]

        if self.sslServerKey:
            create_args += ["--server-key", self.sslServerKey]

        if self.sslServerCert:
            create_args += ["--server-cert", self.sslServerCert]

        if not self.sslSelfSigned and not self.sslCAFile:
            create_args += ["--no-ssl"]

        if self.listen_flag:
            create_args += ["--listen", str(self.vnode.address)]

        if self.formation:
            create_args += ["--formation", self.formation]

        if self.group:
            create_args += ["--group", str(self.group)]

        if self.database:
            create_args += ["--dbname", self.database]

        if name:
            self.name = name
            create_args += ["--name", name]

        if host:
            create_args += ["--hostname", host]

        if port:
            create_args += ["--pgport", port]

        if candidatePriority is not None:
            create_args += ["--candidate-priority", str(candidatePriority)]

        if replicationQuorum is not None:
            create_args += ["--replication-quorum", str(replicationQuorum)]

        if citusSecondary is True:
            create_args += ["--citus-secondary"]

        if citusClusterName is not None and citusClusterName != "default":
            create_args += ["--citus-cluster", citusClusterName]

        if self.monitorDisabled:
            assert nodeId is not None
            create_args += ["--disable-monitor"]
            create_args += ["--node-id", str(nodeId)]

        if run:
            create_args += ["--run"]

        # when run is requested pg_autoctl does not terminate
        # therefore we do not wait for process to complete
        # we just record the process
        self.pg_autoctl = PGAutoCtl(self, create_args)
        if run:
            self.pg_autoctl.run()
        else:
            self.pg_autoctl.execute("pg_autoctl create")

        # sometimes we might have holes in the nodeid sequence
        # grab the current nodeid, if it's already available
        # when run=True the background process may not have written its state
        # file yet — tolerate that and leave self.nodeid at its default
        try:
            nodeid = self.get_nodeid()
            if nodeid > 0:
                self.nodeid = nodeid
        except CalledProcessError:
            pass

    def logger_name(self):
        return self.datadir

    def get_nodeid(self):
        """
        Fetch the nodeid from the pg_autoctl state file.
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute("get node id", "inspect", "fsm", "state")

        self.state = json.loads(out)
        return self.state["state"]["nodeId"]

    def jsDict(self, lsn="0/1", isPrimary=False):
        """
        Returns a python dict with the information to fill-in a JSON
        representation of the node.
        """
        return {
            "node_id": self.get_nodeid(),
            "node_name": self.name,
            "node_host": str(self.vnode.address),
            "node_port": self.port,
            "node_lsn": lsn,
            "node_is_primary": isPrimary,
        }

    def get_local_state(self):
        """
        Fetch the assigned_state from the pg_autoctl state file.
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "get node id", "-vv", "inspect", "fsm", "state"
        )

        self.state = json.loads(out)
        return (
            self.state["state"]["current_role"],
            self.state["state"]["assigned_role"],
        )

    def get_state(self):
        return super().get_state(
            "node %s in group %s not found on the monitor"
            % (self.nodeid, self.group),
            """
    SELECT reportedstate, goalstate
    FROM pgautofailover.node
    WHERE nodeid=%s and groupid=%s
    """,
            self.nodeid,
            self.group,
        )

    def get_nodename(self, nodeId=None):
        """
        Fetch the node name from the monitor, given its nodeid
        """
        if nodeId is None:
            nodeId = self.get_nodeid()

        self.name = self.cluster.monitor.run_sql_query(
            "select nodename from pgautofailover.node where nodeid = %s", nodeId
        )[0][0]

        return self.name

    def destroy(
        self, force=False, ignore_failure=False, timeout=COMMAND_TIMEOUT
    ):
        """
        Cleans up processes and files created for this data node.
        """

        try:
            # SIGINT (unlike the stop_pg_autoctl() default of SIGTERM) skips
            # pg_autoctl's graceful-shutdown maintenance handoff: there's no
            # standby to protect when we're about to tear down the whole
            # cluster anyway, and that handoff can legitimately take up to
            # KEEPER_MAINTENANCE_SHUTDOWN_LOOP_MAX_SECS +
            # KEEPER_SHUTDOWN_LOOP_MAX_SECS (60s total, see defaults.h) --
            # right up against this module's own COMMAND_TIMEOUT (60s), with
            # no margin for overhead. A node that hits that ceiling used to
            # raise TimeoutExpired straight out of this call (uncaught, since
            # it ran before the try/except below), aborting the rest of
            # Cluster.destroy()'s loop and leaking every remaining node plus
            # the monitor as orphaned processes for the calling test to hang
            # on.
            self.stop_pg_autoctl(sig=signal.SIGINT)
        except Exception as e:
            if ignore_failure:
                print(str(e))
            else:
                raise

        flags = ["--destroy"]
        if force:
            flags.append("--force")

        try:
            destroy = PGAutoCtl(self)
            destroy.execute(
                "pg_autoctl drop node --destroy",
                "drop",
                "node",
                *flags,
                timeout=timeout,
            )
        except Exception as e:
            if ignore_failure:
                print(str(e))
            else:
                raise

        try:
            os.remove(self.config_file_path())
        except FileNotFoundError:
            pass

        try:
            os.remove(self.state_file_path())
        except FileNotFoundError:
            pass

        # Remove self from the cluster if present so that future calls to
        # cluster.destroy() will not emit errors for the already destroyed node
        try:
            self.cluster.datanodes.remove(self)
        except ValueError:
            pass

    def sleep(self, sleep_time):
        self.cluster.sleep(sleep_time)

    def get_events(self):
        """
        Returns the current list of events from the monitor.
        """
        if self.monitor:
            last_events_query = (
                "select eventtime, nodename, "
                "reportedstate, goalstate, "
                "reportedrepstate, reportedtli, reportedlsn, description "
                "from pgautofailover.last_events('default', count => 20)"
            )
            return self.monitor.get_events()

    def enable_maintenance(self, allowFailover=False):
        """
        Enables maintenance on a pg_autoctl standby node

        :return:
        """
        command = PGAutoCtl(self)
        if allowFailover:
            command.execute(
                "enable maintenance",
                "enable",
                "maintenance",
                "--allow-failover",
            )
        else:
            command.execute("enable maintenance", "enable", "maintenance")

    def disable_maintenance(self):
        """
        Disables maintenance on a pg_autoctl standby node

        :return:
        """
        command = PGAutoCtl(self)
        command.execute("disable maintenance", "disable", "maintenance")

    def perform_promotion(self):
        """
        Calls pg_autoctl perform promotion on a Postgres node
        """
        command = PGAutoCtl(self)
        command.execute("perform promotion", "perform", "promotion")

    def enable_monitor(self, monitor):
        """
        Disables the monitor on a pg_autoctl node

        :return:
        """
        command = PGAutoCtl(self)
        command.execute(
            "enable monitor",
            "enable",
            "monitor",
            monitor.connection_string(),
        )

        self.monitor = monitor
        self.monitorDisabled = False

    def disable_monitor(self):
        """
        Disables the monitor on a pg_autoctl node

        :return:
        """
        command = PGAutoCtl(self)
        command.execute("disable monitor", "disable", "monitor", "--force")

        self.monitor = None
        self.monitorDisabled = True

    def drop(self):
        """
        Drops a pg_autoctl node from its formation

        :return:
        """
        command = PGAutoCtl(self)
        command.execute("drop node", "drop", "node")
        return True

    def do_fsm_assign(self, target_state):
        """
        Runs `pg_autoctl manual fsm assign` on a node

        :return:
        """
        command = PGAutoCtl(self)
        command.execute(
            "manual fsm assign", "-vv", "manual", "fsm", "assign", target_state
        )
        return True

    def do_fsm_nodes_set(self, nodesArray):
        """
        Runs `pg_autoctl manual fsm nodes set` on a node

        :return:
        """
        filename = "/tmp/nodes.json"

        with open(filename, "w") as nodesFile:
            nodesFile.write(json.dumps(nodesArray))

        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "manual fsm nodes set", "manual", "fsm", "nodes", "set", filename
        )
        return True

    def do_fsm_step(self):
        """
        Runs `pg_autoctl manual fsm step` on a node

        :return:
        """
        command = PGAutoCtl(self)
        command.execute("manual fsm step", "manual", "fsm", "step")
        return True

    def set_metadata(self, name=None, host=None, port=None):
        """
        Sets node metadata via pg_autoctl
        """
        args = ["set node metadata", "set", "node", "metadata"]

        if name:
            args += ["--name", name]

        if host:
            args += ["--hostname", host]

        if port:
            args += ["--pgport", port]

        command = PGAutoCtl(self)
        command.execute(*args)

    def set_candidate_priority(self, candidatePriority):
        """
        Sets candidate priority via pg_autoctl
        """
        command = PGAutoCtl(self)
        try:
            command.execute(
                "set canditate priority",
                "set",
                "node",
                "candidate-priority",
                "--",
                str(candidatePriority),
            )
        except Exception as e:
            if command.last_returncode == 1:
                return False
            raise e
        return True

    def get_candidate_priority(self):
        """
        Gets candidate priority via pg_autoctl
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "get canditate priority", "get", "node", "candidate-priority"
        )
        return int(out)

    def set_replication_quorum(self, replicationQuorum):
        """
        Sets replication quorum via pg_autoctl
        """
        command = PGAutoCtl(self)
        try:
            command.execute(
                "set replication quorum",
                "set",
                "node",
                "replication-quorum",
                replicationQuorum,
            )
        except Exception as e:
            if command.last_returncode == 1:
                return False
            raise e
        return True

    def get_replication_quorum(self):
        """
        Gets replication quorum via pg_autoctl
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "get replication quorum", "get", "node", "replication-quorum"
        )

        value = out.strip()

        if value not in ["true", "false"]:
            raise Exception("Unknown replication quorum value %s" % value)

        return value == "true"

    def set_number_sync_standbys(self, numberSyncStandbys):
        """
        Sets number sync standbys via pg_autoctl
        """
        command = PGAutoCtl(self)
        try:
            command.execute(
                "set number sync standbys",
                "set",
                "formation",
                "number-sync-standbys",
                str(numberSyncStandbys),
            )
        except Exception as e:
            if command.last_returncode == 1:
                return False
            raise e
        return True

    def get_number_sync_standbys(self):
        """
        Gets number sync standbys  via pg_autoctl
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "get number sync standbys",
            "get",
            "formation",
            "number-sync-standbys",
        )

        return int(out)

    def get_synchronous_standby_names(self):
        """
        Gets synchronous standby names  via pg_autoctl
        """
        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "get synchronous_standby_names", "show", "standby-names"
        )
        # strip spaces and single-quotes from the output
        return out.strip("' \n\r\t")

    def get_synchronous_standby_names_local(self):
        """
        Gets synchronous standby names via sql query on data node
        """
        query = "select current_setting('synchronous_standby_names')"

        result = self.run_sql_query(query)
        return result[0][0]

    def check_synchronous_standby_names(self, ssn):
        """
        Checks both monitor a local synchronous_standby_names do match ssn.
        """
        eq_(self.get_synchronous_standby_names_local(), ssn)
        eq_(self.get_synchronous_standby_names(), ssn)

    def print_synchronous_standby_names(self):
        monitorStandbyNames = self.get_synchronous_standby_names()
        localStandbyNames = self.get_synchronous_standby_names_local()

        print("synchronous_standby_names       = '%s'" % monitorStandbyNames)
        print("synchronous_standby_names_local = '%s'" % localStandbyNames)
        return

    def list_replication_slot_names(self):
        """
        Returns a list of the replication slot names on the local Postgres.
        """
        query = (
            "select slot_name from pg_replication_slots "
            + "where slot_name ~ '^pgautofailover_standby_' "
            + " and slot_type = 'physical'"
        )

        try:
            result = self.run_sql_query(query)
            return [row[0] for row in result]
        except Exception as e:
            self.print_debug_logs()
            raise e

    def has_needed_replication_slots(self, retry_timeout=5):
        """
        Each node is expected to maintain a slot for each of the other nodes
        the primary through streaming replication, the secondary(s) manually
        through calls to pg_replication_slot_advance() on the local Postgres.

        Postgres 10 lacks the function pg_replication_slot_advance() so when
        the local Postgres is version 10 we don't create any replication
        slot on the standby servers.

        A newly-promoted or rejoined node can transiently reject connections
        right after the FSM state converges.  Retry for up to retry_timeout
        seconds on any connectivity error before surfacing the exception.
        The retry covers both the pgmajor() probe and the subsequent
        list_replication_slot_names() call, since Postgres can drop the
        socket between the two.
        """
        deadline = dt.datetime.now() + dt.timedelta(seconds=retry_timeout)
        while True:
            try:
                pgmajor = self.pgmajor()

                if pgmajor == 10:
                    return True

                other_nodes = self.monitor.get_other_nodes(self.nodeid)
                expected_slots = [
                    "pgautofailover_standby_%s" % n[0] for n in other_nodes
                ]
                current_slots = self.list_replication_slot_names()

                # just to make it easier to read through the print()ed list
                expected_slots.sort()
                current_slots.sort()

                if set(expected_slots) == set(current_slots):
                    return True

                self.print_debug_logs()
                print()
                print(
                    "slots list on %s is %s, expected %s"
                    % (self.datadir, current_slots, expected_slots)
                )
                return False

            except psycopg2.OperationalError:
                if dt.datetime.now() >= deadline:
                    raise
                print(
                    "transient connectivity error on %s, retrying has_needed_replication_slots ..."
                    % self.datadir
                )
                time.sleep(0.5)
                self._pgversion = None
                self._pgmajor = None

    def create_wait_until_metadata_sync(self):
        """
        Create an additional function for Citus that's needed for regression
        testing.
        """
        if self.role != Role.Coordinator:
            return False

        self.run_sql_query(
            """
        CREATE OR REPLACE FUNCTION
        public.wait_until_metadata_sync (timeout INTEGER DEFAULT 15000)
        RETURNS void
        LANGUAGE C STRICT AS 'citus';
        """
        )

        self.alter_system_set(
            gucs={
                "citus.metadata_sync_interval": 1000,
                "citus.metadata_sync_retry_interval": 200,
            }
        )


class MonitorNode(PGNode):
    def __init__(
        self,
        cluster,
        datadir,
        vnode,
        port,
        hostname,
        authMethod,
        sslMode=None,
        sslSelfSigned=None,
        sslCAFile=None,
        sslServerKey=None,
        sslServerCert=None,
    ):
        super().__init__(
            cluster,
            datadir,
            vnode,
            port,
            "autoctl_node",
            authMethod,
            "pg_auto_failover",
            Role.Monitor,
            sslMode,
            sslSelfSigned,
            sslCAFile,
            sslServerKey,
            sslServerCert,
        )

        # set the hostname, default to the ip address of the node
        if hostname:
            self.hostname = hostname
        else:
            self.hostname = str(self.vnode.address)

    def create(self, level="-v", run=False):
        """
        Initializes and runs the monitor process.
        """
        create_args = [
            "create",
            self.role.command(),
            level,
            "--pgdata",
            self.datadir,
            "--pgport",
            str(self.port),
            "--auth",
            self.authMethod,
            "--hostname",
            self.hostname,
        ]

        if self.sslMode:
            create_args += ["--ssl-mode", self.sslMode]

        if self.sslSelfSigned:
            create_args += ["--ssl-self-signed"]

        if self.sslCAFile:
            create_args += ["--ssl-ca-file", self.sslCAFile]

        if self.sslServerKey:
            create_args += ["--server-key", self.sslServerKey]

        if self.sslServerCert:
            create_args += ["--server-cert", self.sslServerCert]

        if not self.sslSelfSigned and not self.sslCAFile:
            create_args += ["--no-ssl"]

        if run:
            create_args += ["--run"]

        # when run is requested pg_autoctl does not terminate
        # therefore we do not wait for process to complete
        # we just record the process

        self.pg_autoctl = PGAutoCtl(self, create_args)
        if run:
            self.pg_autoctl.run()
        else:
            self.pg_autoctl.execute("create monitor")

    def run(self, env={}, name=None, host=None, port=None):
        """
        Runs "pg_autoctl run"
        """
        self.pg_autoctl = PGAutoCtl(self)
        self.pg_autoctl.run(level="-v")

        # avoid a race condition when trying to read the configuration file
        # before it's ready.
        time.sleep(2)

        # when on the monitor we always want Postgres to be running to continue
        self.wait_until_pg_is_running()

    def destroy(self):
        """
        Cleans up processes and files created for this monitor node.
        """
        if self.pg_autoctl:
            try:
                # see Datanode.destroy()'s use of SIGINT for the rationale;
                # the monitor doesn't run the keeper FSM so it isn't subject
                # to the same graceful-maintenance delay, but using the same
                # signal here keeps teardown intent consistent, and catching
                # the call keeps a slow/failed stop from aborting the rest of
                # Cluster.destroy() (in particular self.vlan.destroy()).
                out, err, ret = self.pg_autoctl.stop(signal.SIGINT)

                if ret != 0:
                    print()
                    print("Monitor logs:\n%s\n%s\n" % (out, err))
            except Exception as e:
                print(str(e))

        try:
            destroy = PGAutoCtl(self)
            destroy.execute(
                "pg_autoctl destroy monitor", "drop", "monitor", "--destroy"
            )
        except Exception as e:
            print(str(e))

        try:
            os.remove(self.config_file_path())
        except FileNotFoundError:
            pass

        try:
            os.remove(self.state_file_path())
        except FileNotFoundError:
            pass

        # Set self to None in cluster to avoid errors in future calls to
        # cluster.destroy()
        self.cluster.monitor = None

    def create_formation(
        self, formation_name, kind="pgsql", secondary=None, dbname=None
    ):
        """
        Create a formation that the monitor controls

        :param formation_name: identifier used to address the formation
        :param ha: boolean whether or not to run the formation with high availability
        :param kind: identifier to signal what kind of formation to run
        :param dbname: name of the database to use in the formation
        :return: None
        """
        formation_command = [
            shutil.which("pg_autoctl"),
            "create",
            "formation",
            "--pgdata",
            self.datadir,
            "--formation",
            formation_name,
            "--kind",
            kind,
        ]

        if dbname is not None:
            formation_command += ["--dbname", dbname]

        # pass true or false to --enable-secondary or --disable-secondary,
        # only when ha is actually set by the user
        if secondary is not None:
            if secondary:
                formation_command += ["--enable-secondary"]
            else:
                formation_command += ["--disable-secondary"]

        self.vnode.run_and_wait(formation_command, name="create formation")

    def enable(self, feature, formation="default"):
        """
        Enable a feature on a formation

        :param feature: instance of Feature enum indicating which feature to enable
        :param formation: name of the formation to enable the feature on
        :return: None
        """
        command = PGAutoCtl(self)
        command.execute(
            "enable %s" % feature.command(),
            "enable",
            feature.command(),
            "--formation",
            formation,
        )

    def disable(self, feature, formation="default"):
        """
        Disable a feature on a formation

        :param feature: instance of Feature enum indicating which feature to disable
        :param formation: name of the formation to disable the feature on
        :return: None
        """
        command = PGAutoCtl(self)
        command.execute(
            "disable %s" % feature.command(),
            "disable",
            feature.command(),
            "--formation",
            formation,
        )

    def failover(self, formation="default", group=0):
        """
        performs manual failover for given formation and group id
        """
        # Wait until the monitor sees a stable primary (reportedstate ==
        # goalstate, both in a CanInitiateFailover state) before calling
        # perform_failover.  Without this, perform_failover can race with the
        # last node_active round-trip and fail with "couldn't find the primary
        # node" even though wait_until_state("primary") already returned.
        wait_until = dt.datetime.now() + dt.timedelta(seconds=STATE_CHANGE_TIMEOUT)
        while dt.datetime.now() < wait_until:
            rows = self.run_sql_query(
                """
                SELECT count(*) FROM pgautofailover.node
                 WHERE formationid = %s
                   AND groupid = %s
                   AND reportedstate IN ('primary', 'single', 'join_primary')
                   AND goalstate = reportedstate
                """,
                formation,
                group,
            )
            if rows[0][0] > 0:
                break
            time.sleep(POLLING_INTERVAL)

        failover_command_text = (
            "select * from pgautofailover.perform_failover('%s', %s)"
            % (formation, group)
        )
        failover_command = [
            shutil.which("psql"),
            "-d",
            self.database,
            "-c",
            failover_command_text,
        ]
        self.vnode.run_and_wait(failover_command, name="manual failover")

    def print_state(self, formation="default"):
        print("pg_autoctl show state --pgdata %s" % self.datadir)

        command = PGAutoCtl(self)
        out, err, ret = command.execute(
            "show state", "show", "state", "--formation", formation
        )
        print("%s" % out)

    def get_other_nodes(self, nodeid):
        """
        Returns the list of the other nodes in the same formation/group.
        """
        query = "select * from pgautofailover.get_other_nodes(%s)"
        return self.run_sql_query(query, nodeid)

    def check_ssl(self, ssl, sslmode):
        """
        Checks if ssl settings match how the node is set up
        """
        return super().check_ssl(ssl, sslmode, monitor=True)

    def get_events(self):
        """
        Returns the current list of events from the monitor.
        """
        last_events_query = (
            "select eventtime, nodename, "
            "reportedstate, goalstate, "
            "reportedrepstate, reportedtli, reportedlsn, description "
            "from pgautofailover.last_events('default', count => 20)"
        )

        if self.pg_is_running():
            return self.run_sql_query(last_events_query)

    def run_sql_query(self, query, *args):
        """
        Run a SQL query on the monitor. When exception OperationalError is
        raised, it might be a SEGFAULT on the Postgres side of things,
        within the pgautofailover extension. To help debug, then print the
        Postgres logs.
        """
        try:
            return super().run_sql_query(query, *args)
        except psycopg2.OperationalError:
            # Did we SEGFAULT? let's see the Postgres logs.
            pglogs = self.get_postgres_logs()
            print(f"POSTGRES LOGS FOR {self.datadir}:\n{pglogs}\n")
            raise


class PGAutoCtl:
    def __init__(self, pgnode, argv=None):
        self.vnode = pgnode.vnode
        self.datadir = pgnode.datadir
        self.pgnode = pgnode

        self.command = None
        self.program = shutil.which("pg_autoctl")

        if self.program is None:
            pg_config = shutil.which("pg_config")

            if pg_config is None:
                raise Exception(
                    "Failed to find pg_config in %s" % os.environ["PATH"]
                )
            else:
                # run pg_config --bindir
                p = subprocess.run(
                    [pg_config, "--bindir"], text=True, capture_output=True
                )
                bindir = p.stdout.splitlines()[0]
                self.program = os.path.join(bindir, "pg_autoctl")

        self.run_proc = None
        self.last_returncode = None
        self.out = ""
        self.err = ""
        self.cmd = ""

        # Only set by run(): stdout/stderr log files for the long-running
        # background process it starts, in place of pipes (see its
        # docstring). None for anything started via execute() instead.
        self.stdout_path = None
        self.stderr_path = None
        self.stdout_file = None
        self.stderr_file = None

        if argv:
            self.command = [self.program] + argv

    def run(self, level="-vv", name=None, host=None, port=None):
        """
        Runs our command in the background, returns immediately.

        The command could be `pg_autoctl run`, or another command.
        We could be given a full `pg_autoctl create postgres --run` command.
        """
        if not self.command:
            self.command = [
                self.program,
                "run",
                "--pgdata",
                self.datadir,
                level,
            ]

        if name:
            self.command += ["--name", name]

        if host:
            self.command += ["--hostname", host]

        if port:
            self.command += ["--pgport", port]

        self.cmd = " ".join(self.command)

        if self.run_proc:
            self.run_proc.release()

        # In case a previous run() was never followed by a communicate()
        # call (e.g. release()'d directly), don't leak its file handles.
        if self.stdout_file is not None:
            self.stdout_file.close()

        if self.stderr_file is not None:
            self.stderr_file.close()

        # `pg_autoctl run` is a long-running background process: this method
        # returns immediately, and nothing calls communicate() on it again
        # until much later (stop()/fail()), if ever. Redirecting its stdout/
        # stderr to files rather than pipes means its own (possibly `-vv`)
        # logging can never fill an undrained pipe buffer and deadlock it --
        # see run_unmanaged()'s docstring in network.py.
        self.stdout_path = self.datadir.rstrip("/") + ".stdout.log"
        self.stderr_path = self.datadir.rstrip("/") + ".stderr.log"

        # The parent directory only exists already by accident, when some
        # earlier step in the same test (e.g. creating the monitor) happened
        # to create it first: nothing guarantees it in monitor-disabled
        # tests, where this can be the very first path created under
        # datadir's parent. Chmod it wide open: pytest itself runs as root
        # (via plain `sudo`), so a bare os.makedirs() here leaves the
        # directory root-owned and mode 0755 -- but pg_autoctl itself runs as
        # the unprivileged "docker" user (via `sudo -u docker`), and needs to
        # create its own subdirectories under the same parent (e.g. its
        # backup directory), which then fails with "Permission denied".
        parent_dir = os.path.dirname(self.stdout_path)
        os.makedirs(parent_dir, exist_ok=True)
        os.chmod(parent_dir, 0o777)

        self.stdout_file = open(self.stdout_path, "w")
        self.stderr_file = open(self.stderr_path, "w")

        self.run_proc = self.vnode.run_unmanaged(
            self.command, stdout=self.stdout_file, stderr=self.stderr_file
        )

    def execute(self, name, *args, timeout=COMMAND_TIMEOUT):
        """
        Execute a single pg_autoctl command, wait for its completion.
        """
        self.set_command(*args)
        self.cmd = " ".join(self.command)

        with self.vnode.run(self.command) as proc:
            try:
                out, err = self.pgnode.cluster.communicate(proc, timeout)

            except subprocess.TimeoutExpired:
                string_command = " ".join(self.command)
                self.pgnode.print_debug_logs()
                raise Exception(
                    f"{name} timed out after {timeout} seconds.\n{string_command}\n",
                )

            self.last_returncode = proc.returncode
            if proc.returncode > 0:
                raise CalledProcessError(proc.returncode, self.cmd, out, err)

            return out, err, proc.returncode

    def stop(self, sig=signal.SIGTERM):
        """
        Kills the keeper by sending "sig" to keeper's process group.

        SIGTERM triggers pg_autoctl's graceful shutdown path (see
        keeper_graceful_shutdown() in service_keeper.c): for a primary, this
        now requests maintenance so a standby can take over immediately,
        driving the FSM through prepare_maintenance -> maintenance; for
        anything else (or if maintenance isn't possible, e.g. no candidate
        is available), it falls back to reporting node state to the
        monitor for up to 30s while PostgreSQL stops. SIGINT/SIGQUIT skip
        all of that and exit immediately -- pass one of those to simulate a
        true hard crash instead of a graceful, operator-initiated stop.
        """
        if self.run_proc and self.run_proc.pid:
            try:
                os.kill(self.run_proc.pid, sig)

                return self.pgnode.cluster.communicate(self, COMMAND_TIMEOUT)

            except ProcessLookupError as e:
                self.run_proc = None
                print(
                    "Failed to terminate pg_autoctl for %s: %s"
                    % (self.datadir, e)
                )
                return None, None, -1
        else:
            return None, None, 0

    def communicate(self, timeout=COMMAND_TIMEOUT):
        """
        Read all data from the Unix PIPE, or from the stdout/stderr log
        files when run() redirected them there instead (see run()'s
        docstring for why).

        This call is idempotent. If it is called a second time after an earlier
        successful call, then it returns the results from when the process
        exited originally.
        """
        if not self.run_proc:
            return self.out, self.err

        self.out, self.err = self.run_proc.communicate(timeout=timeout)

        # run() redirects to files rather than pipes for a process it
        # doesn't communicate() with right away (see its docstring): in that
        # case the line above returns (None, None), since Popen only ever
        # captures output itself when it owns the pipes. Read it back from
        # the files instead, now that the process has exited.
        if self.stdout_file is not None:
            self.stdout_file.close()
            self.stdout_file = None
            with open(self.stdout_path, "r") as f:
                self.out = f.read()

        if self.stderr_file is not None:
            self.stderr_file.close()
            self.stderr_file = None
            with open(self.stderr_path, "r") as f:
                self.err = f.read()

        # The process exited, so let's clean this process up. Calling
        # communicate again would otherwise cause an "Invalid file object"
        # error.
        ret = self.run_proc.returncode
        self.run_proc.release()
        self.run_proc = None

        return self.out, self.err, ret

    def consume_output(self, secs):
        """
        Read available lines from the process for some given seconds
        """
        try:
            self.out, self.err, ret = self.communicate(timeout=secs)
        except subprocess.TimeoutExpired:
            # all good, we'll comme back
            pass

        return self.out, self.err

    def set_command(self, *args):
        """
        Build the process command line, or use the one given at init time.
        """
        if self.command:
            return self.command

        pgdata = ["--pgdata", self.datadir]
        self.command = [self.program]

        # add pgdata in the command BEFORE any -- arguments
        for arg in args:
            if arg == "--":
                self.command += pgdata
            self.command += [arg]

        # when no -- argument is used, append --pgdata option at the end
        if "--pgdata" not in self.command:
            self.command += pgdata

        return self.command

    def sighup(self):
        """
        Send a SIGHUP signal to the pg_autoctl process
        """
        if self.run_proc and self.run_proc.pid:
            os.kill(self.run_proc.pid, signal.SIGHUP)

        else:
            print("pg_autoctl process for %s is not running" % self.datadir)


def sudo_mkdir_p(directory):
    """
    Runs the command: sudo mkdir -p directory
    """
    p = subprocess.Popen(
        [
            "sudo",
            "-E",
            "-u",
            os.getenv("USER"),
            "env",
            "PATH=" + os.getenv("PATH"),
            "mkdir",
            "-p",
            directory,
        ]
    )
    assert p.wait(timeout=COMMAND_TIMEOUT) == 0
