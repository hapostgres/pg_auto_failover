import os
import os.path
import signal
import shutil
import time
import network
import psycopg2
import subprocess
import datetime as dt
from enum import Enum

COMMAND_TIMEOUT = 60
STATE_CHANGE_TIMEOUT = 90
PGVERSION = os.getenv("PGVERSION", "11")

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
    def __init__(self, networkNamePrefix="pgauto", networkSubnet="172.27.1.0/24"):
        """
        Initializes the environment, virtual network, and other local state
        necessary for operation of the Cluster.
        """
        os.environ["PG_REGRESS_SOCK_DIR"] = ''
        os.environ["PG_AUTOCTL_DEBUG"] = ''
        os.environ["PGHOST"] = 'localhost'
        self.networkSubnet = networkSubnet
        self.vlan = network.VirtualLAN(networkNamePrefix, networkSubnet)
        self.monitor = None
        self.datanodes = []

    def create_monitor(self, datadir, port=5432, nodename=None,
                       authMethod=None, sslMode=None, sslSelfSigned=False,
                       sslCAFile=None, sslServerKey=None, sslServerCert=None):
        """
        Initializes the monitor and returns an instance of MonitorNode.
        """
        if self.monitor is not None:
            raise Exception("Monitor has already been created.")
        vnode = self.vlan.create_node()
        self.monitor = MonitorNode(datadir, vnode, port, nodename,
                                   authMethod, sslMode, sslSelfSigned,
                                   sslCAFile=sslCAFile,
                                   sslServerKey=sslServerKey,
                                   sslServerCert=sslServerCert)
        self.monitor.create()
        return self.monitor

    # TODO group should auto sense for normal operations and passed to the
    # create cli as an argument when explicitly set by the test
    def create_datanode(self, datadir, port=5432, group=0,
                        listen_flag=False, role=Role.Postgres,
                        formation=None, authMethod=None,
                        sslMode=None, sslSelfSigned=False,
                        sslCAFile=None, sslServerKey=None, sslServerCert=None):
        """
        Initializes a data node and returns an instance of DataNode. This will
        do the "keeper init" and "pg_autoctl run" commands.
        """
        vnode = self.vlan.create_node()
        nodeid = len(self.datanodes) + 1

        datanode = DataNode(datadir, vnode, port,
                            os.getenv("USER"), authMethod, "postgres",
                            self.monitor, nodeid, group, listen_flag,
                            role, formation,
                            sslMode=sslMode,
                            sslSelfSigned=sslSelfSigned,
                            sslCAFile=sslCAFile,
                            sslServerKey=sslServerKey,
                            sslServerCert=sslServerCert)
        self.datanodes.append(datanode)
        return datanode

    def pg_createcluster(self, datadir, port=5432):
        """
        Initializes a postgresql node using pg_createcluster and returns
        directory path.
        """
        vnode = self.vlan.create_node()
        nodeid = len(self.datanodes) + 1

        create_command = ["sudo", shutil.which('pg_createcluster'),
                          "-U", os.getenv("USER"),
                          PGVERSION, datadir, '-p', str(port)]

        print("%s" % " ".join(create_command))

        create_proc = vnode.run(create_command)
        out, err = create_proc.communicate(timeout=COMMAND_TIMEOUT)
        if create_proc.returncode > 0:
            raise Exception("pg_createcluster failed, out: %s\n, err: %s" %
                            (out, err))

        abspath = os.path.join("/var/lib/postgresql/", PGVERSION, datadir)

        chmod_command = ["sudo", shutil.which('install'),
                         '-d', '-o', os.getenv("USER"),
                         "/var/lib/postgresql/%s/backup" % PGVERSION]

        print("%s" % " ".join(chmod_command))

        chmod_proc = vnode.run(chmod_command)
        out, err = chmod_proc.communicate(timeout=COMMAND_TIMEOUT)
        if chmod_proc.returncode > 0:
            raise Exception("chmod failed, out: %s\n, err: %s" %
                            (out, err))

        return abspath

    def destroy(self):
        """
        Cleanup whatever was created for this Cluster.
        """
        for datanode in self.datanodes:
            datanode.destroy()
        if self.monitor:
            self.monitor.destroy()
        self.vlan.destroy()
        time.sleep(5)


class PGNode:
    """
    Common stuff between MonitorNode and DataNode.
    """
    def __init__(self, datadir, vnode, port, username, authMethod,
                 database, role,
                 sslMode=None, sslSelfSigned=False,
                 sslCAFile=None, sslServerKey=None, sslServerCert=None):
        self.datadir = datadir
        self.vnode = vnode
        self.port = port
        self.username = username
        self.authMethod = authMethod or "trust"
        self.database = database
        self.role = role
        self.pg_autoctl = None
        self.authenticatedUsers = {}
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

        if (self.authMethod and self.username in self.authenticatedUsers):
            dsn = "postgres://%s:%s@%s:%d/%s" % \
                (self.username,
                 self.authenticatedUsers[self.username],
                 host,
                 self.port,
                 self.database)
        else:
            dsn = "postgres://%s@%s:%d/%s" % \
                (self.username,
                 host,
                 self.port,
                 self.database)

        if self.sslMode:
            # If a local CA is used, or even a self-signed certificate,
            # using verify-ca often provides enough protection.
            dsn += "?sslmode=%s" % self.sslMode

        return dsn

    def run(self, env={}):
        """
        Runs "pg_autoctl run"
        """
        self.pg_autoctl = PGAutoCtl(self.vnode, self.datadir)
        self.pg_autoctl.run()
        self.wait_until_pg_is_running()

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
        """
        Runs the given sql query with the given arguments in this postgres node
        and returns the results. Returns None if there are no results to fetch.
        """
        with psycopg2.connect(self.connection_string()) as conn:
            cur = conn.cursor()
            cur.execute(query, args)
            try:
                result = cur.fetchall()
                return result
            except psycopg2.ProgrammingError:
                return None

    def pg_config_get(self, setting):
        """
        Returns the current value of the given postgres setting"
        """
        return self.run_sql_query(f"SHOW {setting}")[0][0]

    def set_user_password(self, username, password):
        """
        Sets user passwords on the PGNode
        """
        alter_user_set_passwd_command = \
            "alter user %s with password \'%s\'" % (username, password)
        passwd_command = [shutil.which('psql'),
                          '-d', self.database,
                          '-c', alter_user_set_passwd_command]
        passwd_proc = self.vnode.run(passwd_command)
        wait_or_timeout_proc(passwd_proc,
                         name="user passwd",
                         timeout=COMMAND_TIMEOUT)
        self.authenticatedUsers[username] = password

    def stop_pg_autoctl(self):
        """
        Kills the keeper by sending a SIGTERM to keeper's process group.
        """
        if self.pg_autoctl:
            return self.pg_autoctl.stop()

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
            stop_command = [shutil.which('pg_ctl'), '-D', self.datadir,
                            '--wait', '--mode', 'fast', 'stop']
            stop_proc = self.vnode.run(stop_command)
            try:
                out, err = stop_proc.communicate(timeout=1)
                break
            except subprocess.TimeoutExpired:
                pass
        else:
            raise Exception("Postgres could not be stopped after 60 attempts")
        if stop_proc.returncode > 0:
            print("stopping postgres for '%s' failed, out: %s\n, err: %s"
                  %(self.vnode.address, out, err))
            return False
        elif stop_proc.returncode is None:
            print("stopping postgres for '%s' timed out")
            return False
        return True

    def reload_postgres(self):
        """
        Reload the postgres configuration by running:
          pg_ctl -D ${self.datadir} reload
        """
        reload_command = [shutil.which('pg_ctl'), '-D', self.datadir, 'reload']
        reload_proc = self.vnode.run(reload_command)
        out, err = reload_proc.communicate(timeout=COMMAND_TIMEOUT)
        if reload_proc.returncode > 0:
            print("reloading postgres for '%s' failed, out: %s\n, err: %s"
                  %(self.vnode.address, out, err))
            return False
        elif reload_proc.returncode is None:
            print("reloading postgres for '%s' timed out")
            return False
        return True

    def restart_postgres(self):
        """
        Restart the postgres configuration by running:
          pg_ctl -D ${self.datadir} restart
        """
        restart_command = [shutil.which('pg_ctl'), '-D', self.datadir, 'restart']
        restart_proc = self.vnode.run(restart_command)
        out, err = restart_proc.communicate(timeout=COMMAND_TIMEOUT)
        if restart_proc.returncode > 0:
            print("restarting postgres for '%s' failed, out: %s\n, err: %s"
                  %(self.vnode.address, out, err))
            return False
        elif restart_proc.returncode is None:
            print("restarting postgres for '%s' timed out")
            return False
        return True

    def pg_is_running(self, timeout=COMMAND_TIMEOUT):
        """
        Returns true when Postgres is running. We use pg_ctl status.
        """
        status_command = [shutil.which('pg_ctl'), '-D', self.datadir, 'status']
        status_proc = self.vnode.run(status_command)
        out, err = status_proc.communicate(timeout=timeout)
        if status_proc.returncode == 0:
            # pg_ctl status is happy to report 0 (Postgres is running) even
            # when it's still "starting" and thus not ready for queries.
            #
            # because our tests need to be able to send queries to Postgres,
            # the "starting" status is not good enough for us, we're only
            # happy with "ready".
            pidfile = os.path.join(self.datadir, 'postmaster.pid')
            with open(pidfile, "r") as p:
                pidlines = p.readlines()
                if len(pidlines) > 7:
                    pg_status = pidlines[7]
                    return pg_status.startswith("ready")
        elif status_proc.returncode > 0:
            # ignore `pg_ctl status` output, silently try again till timeout
            return False
        elif status_proc.returncode is None:
            print("pg_ctl status timed out after %ds" % timeout)
            return False

    def wait_until_pg_is_running(self, timeout=STATE_CHANGE_TIMEOUT):
        """
        Waits until the underlying Postgres process is running.
        """
        wait_until = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while wait_until > dt.datetime.now():
            if self.pg_is_running():
                return True
            time.sleep(0.1)

        print("Postgres is still not running in %s after %d seconds" %
              (self.datadir, timeout))
        return False

    def fail(self):
        """
        Simulates a data node failure by terminating the keeper and stopping
        postgres.
        """
        self.stop_pg_autoctl()
        self.stop_postgres()

    def config_file_path(self):
        """
        Returns the path of the config file for this data node.
        """
        # Config file is located at:
        # ~/.config/pg_autoctl/${PGDATA}/pg_autoctl.cfg
        home = os.getenv("HOME")
        pgdata = os.path.abspath(self.datadir)[1:] # Remove the starting '/'
        return os.path.join(home,
                            ".config/pg_autoctl",
                            pgdata,
                            "pg_autoctl.cfg")

    def state_file_path(self):
        """
        Returns the path of the state file for this data node.
        """
        # State file is located at:
        # ~/.local/share/pg_autoctl/${PGDATA}/pg_autoctl.state
        home = os.getenv("HOME")
        pgdata = os.path.abspath(self.datadir)[1:] # Remove the starting '/'
        return os.path.join(home,
                            ".local/share/pg_autoctl",
                            pgdata,
                            "pg_autoctl.state")

    def get_postgres_logs(self):
        ldir = os.path.join(self.datadir, "log")
        logfiles = os.listdir(ldir)
        logfiles.sort()

        logs = []
        for logfile in logfiles:
            logs += ["\n\n%s:\n" % logfile]
            logs += open(os.path.join(ldir, logfile)).readlines()

        # it's not really logs but we want to see that too
        for inc in ["recovery.conf",
                    "postgresql.auto.conf",
                    "postgresql-auto-failover.conf",
                    "postgresql-auto-failover-standby.conf"]:
            conf = os.path.join(self.datadir, inc)
            if os.path.isfile(conf):
                logs += ["\n\n%s:\n" % conf]
                logs += open(conf).readlines()

        return "".join(logs)

    def pgversion(self):
        """
        Query local Postgres for its version. Cache the result.
        """
        if self._pgversion:
            return self._pgversion

        # server_version_num is 110005 for 11.5
        self._pgversion = int(self.run_sql_query("show server_version_num")[0][0])
        self._pgmajor = self._pgversion // 10000

        return self._pgversion

    def pgmajor(self):
        if self._pgmajor:
            return self._pgmajor

        self.pgversion()
        return self._pgmajor

    def ifdown(self):
        """
        Set a configuration parameter to given value
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("config set %s" % setting,
                        'config', 'set', setting, value)
        return True

    def ifup(self):
        """
        Bring the network interface up for this node
        """
        self.vnode.ifup()

    def config_set(self, setting, value):
        """
        Set a configuration parameter to given value
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("config set %s" % setting,
                        'config', 'set', setting, value)
        return True

    def config_get(self, setting):
        """
        Set a configuration parameter to given value
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("config get %s" % setting,
                                   'config', 'get', setting)
        return out[:-1]

    def show_uri(self, json=False):
        """
        Runs pg_autoctl show uri
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        if json:
            out, err = command.execute("show uri", 'show', 'uri', '--json')
        else:
            out, err = command.execute("show uri", 'show', 'uri')
        return out


class DataNode(PGNode):
    def __init__(self, datadir, vnode, port,
                 username, authMethod, database, monitor,
                 nodeid, group, listen_flag, role, formation,
                 sslMode=None, sslSelfSigned=False,
                 sslCAFile=None, sslServerKey=None, sslServerCert=None):
        super().__init__(datadir, vnode, port,
                         username, authMethod, database, role,
                         sslMode=sslMode,
                         sslSelfSigned=sslSelfSigned,
                         sslCAFile=sslCAFile,
                         sslServerKey=sslServerKey,
                         sslServerCert=sslServerCert)
        self.monitor = monitor
        self.nodeid = nodeid
        self.group = group
        self.listen_flag = listen_flag
        self.formation = formation

    def create(self, run=False, level='-v'):
        """
        Runs "pg_autoctl create"
        """
        pghost = 'localhost'
        sockdir = os.environ["PG_REGRESS_SOCK_DIR"]

        if self.listen_flag:
            pghost = str(self.vnode.address)

        if sockdir and sockdir != "":
            pghost = sockdir

        # don't pass --nodename to Postgres nodes in order to exercise the
        # automatic detection of the nodename.
        create_args = ['create', self.role.command(), level,
                       '--pgdata', self.datadir,
                       '--pghost', pghost,
                       '--pgport', str(self.port),
                       '--pgctl', shutil.which('pg_ctl'),
                       '--auth', self.authMethod,
                       '--monitor', self.monitor.connection_string()]

        if self.sslMode:
            create_args += ['--ssl-mode', self.sslMode]

        if self.sslSelfSigned:
            create_args += ['--ssl-self-signed']

        if self.sslCAFile:
            create_args += ['--ssl-ca-file', self.sslCAFile]

        if self.sslServerKey:
            create_args += ['--server-key', self.sslServerKey]

        if self.sslServerCert:
            create_args += ['--server-cert', self.sslServerCert]

        if not self.sslSelfSigned and not self.sslCAFile:
            create_args += ['--no-ssl']

        if self.listen_flag:
            create_args += ['--listen', str(self.vnode.address)]

        if self.formation:
            create_args += ['--formation', self.formation]

        if run:
            create_args += ['--run']

        # when run is requested pg_autoctl does not terminate
        # therefore we do not wait for process to complete
        # we just record the process
        self.pg_autoctl = PGAutoCtl(self.vnode, self.datadir, create_args)
        if run:
            self.pg_autoctl.run()
            self.wait_until_pg_is_running()
        else:
            self.pg_autoctl.execute("pg_autoctl create")

    def destroy(self):
        """
        Cleans up processes and files created for this data node.
        """
        self.stop_pg_autoctl()

        try:
            destroy = PGAutoCtl(self.vnode, self.datadir)
            destroy.execute("pg_autoctl drop node --destroy",
                            'drop', 'node', '--destroy')
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

    def wait_until_state(self, target_state,
                         timeout=STATE_CHANGE_TIMEOUT,
                         sleep_time=0.1,
                         other_node=None):
        """
        Waits until this data node reaches the target state, and then returns
        True. If this doesn't happen until "timeout" seconds, returns False.
        """
        prev_state = None
        wait_until = dt.datetime.now() + dt.timedelta(seconds=timeout)
        while wait_until > dt.datetime.now():
            if other_node:
                other_node.sleep(sleep_time / 2)
                self.sleep(sleep_time / 2)
            else:
                self.sleep(sleep_time)

            current_state = self.get_state()

            # only log the state if it has changed
            if current_state != prev_state:
                if current_state == target_state:
                    print("state of %s is '%s', done waiting" %
                          (self.datadir, current_state))
                else:
                    print("state of %s is '%s', waiting for '%s' ..." %
                          (self.datadir, current_state, target_state))

            if current_state == target_state:
                return True

            prev_state = current_state

        print("%s didn't reach %s after %d seconds" %
              (self.datadir, target_state, timeout))

        error_msg = (f"{self.datadir} failed to reach {target_state} "
                     f"after {timeout} seconds\n")
        events = self.get_events_str()
        error_msg += f"MONITOR EVENTS:\n{events}\n"

        if self.running():
            out, err = self.stop_pg_autoctl()
            error_msg += f"STDOUT OF PG_AUTOCTL FOR MAIN NODE:\n{out}\n"
            error_msg += f"STDERR OF PG_AUTOCTL FOR MAIN NODE:\n{err}\n"

        pglogs = self.get_postgres_logs()
        error_msg += f"POSTGRES LOGS FOR MAIN NODE:\n{pglogs}\n"

        if other_node:
            if other_node.running():
                out, err = other_node.stop_pg_autoctl()
                error_msg += f"STDOUT OF PG_AUTOCTL FOR OTHER NODE:\n{out}\n"
                error_msg += f"STDERR OF PG_AUTOCTL FOR OTHER NODE:\n{err}\n"

            pglogs = other_node.get_postgres_logs()
            error_msg += f"POSTGRES LOGS FOR OTHER NODE:\n{pglogs}\n"

        raise Exception(error_msg)

    def get_state(self):
        """
        Returns the current state of the data node. This is done by querying the
        monitor node.
        """
        results = self.monitor.run_sql_query(
            """
SELECT reportedstate
  FROM pgautofailover.node
 WHERE nodeid=%s and groupid=%s
""",
            self.nodeid, self.group)
        if len(results) == 0:
            raise Exception("node %s in group %s not found on the monitor" %
                            (self.nodeid, self.group))
        else:
            return results[0][0]
        return results

    def get_events(self):
        """
        Returns the current list of events from the monitor.
        """
        last_events_query = "select eventtime, nodeid, nodename, " \
            "reportedstate, goalstate, " \
            "reportedrepstate, reportedlsn, description " \
            "from pgautofailover.last_events('default', count => 20)"
        return self.monitor.run_sql_query(last_events_query)


    def get_events_str(self):
        return "\n".join(
            ["%s %25s:%-14s %17s/%-17s %7s %10s %s" % ("eventtime", "id", "nodename",
                                                       "state", "goal state",
                                                       "repl st", "lsn", "event")]
            +
            ["%s %2d:%-14s %17s/%-17s %7s %10s %s" % result
             for result in self.get_events()])

    def enable_maintenance(self):
        """
        Enables maintenance on a pg_autoctl standby node

        :return:
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("enable maintenance", 'enable', 'maintenance')

    def disable_maintenance(self):
        """
        Disables maintenance on a pg_autoctl standby node

        :return:
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("disable maintenance", 'disable', 'maintenance')

    def drop(self):
        """
        Drops a pg_autoctl node from its formation

        :return:
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("drop node", 'drop', 'node')
        return True

    def set_candidate_priority(self, candidatePriority):
        """
            Sets candidate priority via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        try:
            command.execute("set canditate priority", 'set', 'node',
                            'candidate-priority', '--', str(candidatePriority))
        except Exception as e:
            if command.run_proc.returncode == 1:
                return False
            raise e
        return True

    def get_candidate_priority(self):
        """
            Gets candidate priority via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("get canditate priority",
                                   'get', 'node', 'candidate-priority')
        return int(out)

    def set_replication_quorum(self, replicationQuorum):
        """
            Sets replication quorum via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        try:
            command.execute("set replication quorum", 'set', 'node',
                            'replication-quorum', replicationQuorum)
        except Exception as e:
            if command.run_proc.returncode == 1:
                return False
            raise e
        return True

    def get_replication_quorum(self):
        """
            Gets replication quorum via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("get replication quorum",
                                   'get', 'node', 'replication-quorum')

        value = out.strip()

        if (value not in ['true', 'false']):
            raise Exception("Unknown replication quorum value %s" % value)

        return value == "true"

    def set_number_sync_standbys(self, numberSyncStandbys):
        """
            Sets number sync standbys via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        try:
            command.execute("set number sync standbys",
                            'set', 'formation',
                            'number-sync-standbys', str(numberSyncStandbys))
        except Exception as e:
            # either caught as a BAD ARG (1) or by the monitor (6)
            if command.run_proc.returncode in (1, 6):
                return False
            raise e
        return True

    def get_number_sync_standbys(self):
        """
            Gets number sync standbys  via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("get number sync standbys",
                                   'get', 'formation', 'number-sync-standbys')

        return int(out)

    def get_synchronous_standby_names(self):
        """
            Gets number sync standbys  via pg_autoctl
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("get synchronous_standby_names",
                                   'show', 'synchronous_standby_names')

        return out.strip()

    def list_replication_slot_names(self):
        """
            Returns a list of the replication slot names on the local Postgres.
        """
        query = "select slot_name from pg_replication_slots " \
            + "where slot_name ~ '^pgautofailover_standby_' " \
            + " and slot_type = 'physical'"

        result = self.run_sql_query(query)
        return [row[0] for row in result]

    def has_needed_replication_slots(self):
        """
        Each node is expected to maintain a slot for each of the other nodes
        the primary through streaming replication, the secondary(s) manually
        through calls to pg_replication_slot_advance() on the local Postgres.

        Postgres 10 lacks the function pg_replication_slot_advance() so when
        the local Postgres is version 10 we don't create any replication
        slot on the standby servers.
        """
        if self.pgmajor() == 10:
            return True

        print("has_needed_replication_slots: pgversion = %s, pgmajor = %s" %
              (self.pgversion(), self.pgmajor()))

        hostname = str(self.vnode.address)
        other_nodes = self.monitor.get_other_nodes(hostname, self.port)
        expected_slots = ['pgautofailover_standby_%s' % n[0] for n in other_nodes]
        current_slots = self.list_replication_slot_names()

        # just to make it easier to read through the print()ed list
        expected_slots.sort()
        current_slots.sort()

        if set(expected_slots) == set(current_slots):
            print("slots list on %s is %s, as expected" %
                  (self.datadir, current_slots))
        else:
            print("slots list on %s is %s, expected %s" %
                  (self.datadir, current_slots, expected_slots))

        return set(expected_slots) == set(current_slots)


class MonitorNode(PGNode):
    def __init__(self, datadir, vnode, port, nodename, authMethod,
                 sslMode=None, sslSelfSigned=None,
                 sslCAFile=None, sslServerKey=None, sslServerCert=None):

        super().__init__(datadir, vnode, port,
                         "autoctl_node", authMethod,
                         "pg_auto_failover", Role.Monitor,
                         sslMode, sslSelfSigned,
                         sslCAFile, sslServerKey, sslServerCert)

        # set the nodename, default to the ip address of the node
        if nodename:
            self.nodename = nodename
        else:
            self.nodename = str(self.vnode.address)


    def create(self, run = False):
        """
        Initializes and runs the monitor process.
        """
        create_args = ['create', self.role.command(), '-vv',
                       '--pgdata', self.datadir,
                       '--pgport', str(self.port),
                       '--auth', self.authMethod,
                       '--nodename', self.nodename]

        if self.sslMode:
            create_args += ['--ssl-mode', self.sslMode]

        if self.sslSelfSigned:
            create_args += ['--ssl-self-signed']

        if self.sslCAFile:
            create_args += ['--ssl-ca-file', self.sslCAFile]

        if self.sslServerKey:
            create_args += ['--server-key', self.sslServerKey]

        if self.sslServerCert:
            create_args += ['--server-cert', self.sslServerCert]

        if not self.sslSelfSigned and not self.sslCAFile:
            create_args += ['--no-ssl']

        if run:
            create_args += ['--run']

        # when run is requested pg_autoctl does not terminate
        # therefore we do not wait for process to complete
        # we just record the process

        self.pg_autoctl = PGAutoCtl(self.vnode, self.datadir, create_args)
        if run:
            self.pg_autoctl.run()
        else:
            self.pg_autoctl.execute("create monitor")

    def run(self, env={}):
        """
        Runs "pg_autoctl run"
        """
        self.pg_autoctl = PGAutoCtl(self.vnode, self.datadir)
        self.pg_autoctl.run(level='-v')

    def destroy(self):
        """
        Cleans up processes and files created for this monitor node.
        """
        if self.pg_autoctl:
            out, err = self.pg_autoctl.stop()

            if out or err:
                print()
                print("Monitor logs:\n%s\n%s\n" % (out, err))

        try:
            destroy = PGAutoCtl(self.vnode, self.datadir)
            destroy.execute("pg_autoctl node destroy",
                            'drop', 'monitor', '--destroy')
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

    def create_formation(self, formation_name,
                         kind="pgsql", secondary=None, dbname=None):
        """
        Create a formation that the monitor controls

        :param formation_name: identifier used to address the formation
        :param ha: boolean whether or not to run the formation with high availability
        :param kind: identifier to signal what kind of formation to run
        :param dbname: name of the database to use in the formation
        :return: None
        """
        formation_command = [shutil.which('pg_autoctl'), 'create', 'formation',
                             '--pgdata', self.datadir,
                             '--formation', formation_name,
                             '--kind', kind]

        if dbname is not None:
            formation_command += ['--dbname', dbname]

        # pass true or false to --enable-secondary or --disable-secondary, only when ha is
        # actually set by the user
        if secondary is not None:
            if secondary:
                formation_command += ['--enable-secondary']
            else:
                formation_command += ['--disable-secondary']

        formation_proc = self.vnode.run(formation_command)
        wait_or_timeout_proc(formation_proc,
                             name="create formation",
                             timeout=COMMAND_TIMEOUT)

    def enable(self, feature, formation='default'):
        """
        Enable a feature on a formation

        :param feature: instance of Feature enum indicating which feature to enable
        :param formation: name of the formation to enable the feature on
        :return: None
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("enable %s" % feature.command(),
                        'enable', feature.command(), '--formation', formation)

    def disable(self, feature, formation='default'):
        """
        Disable a feature on a formation

        :param feature: instance of Feature enum indicating which feature to disable
        :param formation: name of the formation to disable the feature on
        :return: None
        """
        command = PGAutoCtl(self.vnode, self.datadir)
        command.execute("disable %s" % feature.command(),
                        'disable', feature.command(), '--formation', formation)

    def failover(self, formation='default', group=0):
        """
        performs manual failover for given formation and group id
        """
        failover_commmand_text = \
            "select * from pgautofailover.perform_failover('%s', %s)" % \
            (formation, group)
        failover_command = [shutil.which('psql'),
                            '-d', self.database,
                            '-c', failover_commmand_text]
        failover_proc = self.vnode.run(failover_command)
        wait_or_timeout_proc(failover_proc,
                         name="manual failover",
                         timeout=COMMAND_TIMEOUT)


    def print_state(self, formation="default"):
        print("pg_autoctl show state --pgdata %s" % self.datadir)

        command = PGAutoCtl(self.vnode, self.datadir)
        out, err = command.execute("show state", 'show', 'state')
        print("%s" % out)

    def get_other_nodes(self, host, port):
        """
        Returns the list of the other nodes in the same formation/group.
        """
        query = "select * from pgautofailover.get_other_nodes(%s, %s)"
        return self.run_sql_query(query, host, port)


class PGAutoCtl():
    def __init__(self, vnode, datadir, argv=None):
        self.vnode = vnode
        self.datadir = datadir

        self.program = shutil.which('pg_autoctl')
        self.command = None

        self.run_proc = None
        self.out = ""
        self.err = ""

        if argv:
            self.command = [self.program] + argv

    def run(self, level='-vv'):
        """
        Runs our command in the background, returns immediately.

        The command could be `pg_autoctl run`, or another command.
        We could be given a full `pg_autoctl create postgres --run` command.
        """
        if not self.command:
            self.command = [self.program, 'run', '--pgdata', self.datadir, level]

        self.run_proc = self.vnode.run(self.command)
        print("pg_autoctl run [%d]" % self.run_proc.pid)

    def execute(self, name, *args):
        """
        Execute a single pg_autoctl command, wait for its completion.
        """
        self.set_command(*args)
        self.run_proc = self.vnode.run(self.command)

        try:
            # wait until process is done, still applying COMMAND_TIMEOUT
            self.communicate(timeout=COMMAND_TIMEOUT)

            if self.run_proc.returncode > 0:
                raise Exception("%s failed\n%s\n%s\n%s" %
                                (name,
                                 " ".join(self.command),
                                 self.out,
                                 self.err))
            return self.out, self.err

        except subprocess.TimeoutExpired:
            # we already spent our allocated waiting time, just kill the process
            self.run_proc.kill()
            self.run_proc.wait()
            self.run_proc.release()

            self.run_proc = None

            raise Exception("%s timed out after %d seconds.\n%s\n%s\n%s"%
                            (name,
                             COMMAND_TIMEOUT,
                             " ".join(self.command),
                             self.out,
                             self.err))

        return self.out, self.err

    def stop(self):
        """
        Kills the keeper by sending a SIGTERM to keeper's process group.
        """
        if self.run_proc and self.run_proc.pid:
            print("Terminating pg_autoctl process for %s [%d]" %
                  (self.datadir, self.run_proc.pid))

            try:
                pgid = os.getpgid(self.run_proc.pid)
                os.killpg(pgid, signal.SIGQUIT)

                self.communicate()
                self.run_proc.wait()
                self.run_proc.release()

                self.run_proc = None

                return self.out, self.err

            except ProcessLookupError as e:
                self.run_proc = None
                print("Failed to terminate pg_autoctl for %s: %s" %
                      (self.datadir, e))
                return None, None
        else:
            print("pg_autoctl process for %s is not running" % self.datadir)
            return None, None

    def communicate(self, timeout=COMMAND_TIMEOUT):
        """
        Read all data from the Unix PIPE
        """
        self.out, self.err = self.run_proc.communicate(timeout=timeout)

        return self.out, self.err

    def consume_output(self, secs):
        """
        Read available lines from the process for some given seconds
        """
        try:
            self.out, self.err = self.run_proc.communicate(timeout=secs)
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

        pgdata = ['--pgdata', self.datadir]
        self.command = [self.program]

        # add pgdata in the command BEFORE any -- arguments
        for arg in args:
            if arg == '--':
                self.command += pgdata
            self.command += [arg]

        # when no -- argument is used, append --pgdata option at the end
        if '--pgdata' not in self.command:
            self.command += pgdata

        return self.command


def wait_or_timeout_proc(proc, name, timeout):
    """
    Waits for command to exit successfully. If it exits with error or it timeouts,
    raises an execption with stdout and stderr streams of the process.
    """
    try:
        out, err = proc.communicate(timeout=COMMAND_TIMEOUT)
        if proc.returncode > 0:
            raise Exception("%s failed, out: %s\n, err: %s" % (name, out, err))
        return out, err
    except subprocess.TimeoutExpired:
        proc.kill()
        out, err = proc.communicate()
        raise Exception("%s timed out after %d seconds. out: %s\n, err: %s" \
                        % (name, timeout, out, err))
