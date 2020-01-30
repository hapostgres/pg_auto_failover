import os
import os.path
import signal
import shutil
import time
import network
import psycopg2
import subprocess
from enum import Enum

COMMAND_TIMEOUT = 60
STATE_CHANGE_TIMEOUT = 90

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
        self.vlan = network.VirtualLAN(networkNamePrefix, networkSubnet)
        self.monitor = None
        self.datanodes = []

    def create_monitor(self, datadir, port=5432, nodename=None, authMethod=None):
        """
        Initializes the monitor and returns an instance of MonitorNode.
        """
        if self.monitor is not None:
            raise Exception("Monitor has already been created.")
        vnode = self.vlan.create_node()
        self.monitor = MonitorNode(datadir, vnode, port, nodename, authMethod)
        self.monitor.create()
        return self.monitor

    # TODO group should auto sense for normal operations and passed to the
    # create cli as an argument when explicitly set by the test
    def create_datanode(self, datadir, port=5432, group=0,
                        listen_flag=False, role=Role.Postgres,
                        formation=None, authMethod=None):
        """
        Initializes a data node and returns an instance of DataNode. This will
        do the "keeper init" and "pg_autoctl run" commands.
        """
        vnode = self.vlan.create_node()
        nodeid = len(self.datanodes) + 1
        datanode = DataNode(datadir, vnode, port,
                            os.getenv("USER"), authMethod, "postgres",
                            self.monitor, nodeid, group, listen_flag,
                            role, formation)
        self.datanodes.append(datanode)
        return datanode

    def destroy(self):
        """
        Cleanup whatever was created for this Cluster.
        """
        for datanode in self.datanodes:
            datanode.destroy()
        if self.monitor:
            self.monitor.destroy()
        self.vlan.destroy()


class PGNode:
    """
    Common stuff between MonitorNode and DataNode.
    """
    def __init__(self, datadir, vnode, port, username, authMethod,
                 database, role):
        self.datadir = datadir
        self.vnode = vnode
        self.port = port
        self.username = username
        self.authMethod = authMethod
        self.database = database
        self.role = role
        self.pg_autoctl = None
        self.authenticatedUsers = {}


    def connection_string(self):
        """
        Returns a connection string which can be used to connect to this postgres
        node.
        """
        if (self.authMethod and self.username in self.authenticatedUsers):
            return ("postgres://%s:%s@%s:%d/%s" %
                    (self.username,
                     self.authenticatedUsers[self.username],
                     self.vnode.address,
                     self.port,
                     self.database))

        return ("postgres://%s@%s:%d/%s" % (self.username, self.vnode.address,
                                           self.port, self.database))

    def run(self, env={}):
        """
        Runs "pg_autoctl run"
        """
        self.pg_autoctl = PGAutoCtl(self.vnode, self.datadir)
        self.pg_autoctl.run()

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
        stop_command = [shutil.which('pg_ctl'), '-D', self.datadir,
                        '--wait', '--mode', 'fast', 'stop']
        stop_proc = self.vnode.run(stop_command)
        out, err = stop_proc.communicate(timeout=COMMAND_TIMEOUT)
        if stop_proc.returncode > 0:
            print("stopping postgres for '%s' failed, out: %s\n, err: %s"
                  %(self.vnode.address, out, err))
            return False
        elif stop_proc.returncode is None:
            print("stopping postgres for '%s' timed out")
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
                pg_status = p.readlines()[7]
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
        for i in range(timeout):
            time.sleep(1)

            if self.pg_is_running():
                return True

        else:
            print("Postgres is still not running in %s after %d attempts" %
                  (self.datadir, timeout))
            return False

    def fail(self):
        """
        Simulates a data node failure by terminating the keeper and stopping
        postgres.
        """
        self.stop_pg_autoctl()
        self.stop_postgres()

    def destroy(self):
        """
        Cleans up processes and files created for this data node.
        """
        self.stop_pg_autoctl()

        try:
            destroy = PGAutoCtl(self.vnode, self.datadir)
            destroy.execute("pg_autoctl destroy", 'drop', 'node', '--destroy')
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


class DataNode(PGNode):
    def __init__(self, datadir, vnode, port,
                 username, authMethod, database, monitor,
                 nodeid, group, listen_flag, role, formation):
        super().__init__(datadir, vnode, port,
                         username, authMethod, database, role)
        self.monitor = monitor
        self.nodeid = nodeid
        self.group = group
        self.listen_flag = listen_flag
        self.formation = formation

    def create(self, run=False):
        """
        Runs "pg_autoctl create"
        """
        pghost = 'localhost'

        if self.listen_flag:
            pghost = str(self.vnode.address)

        # don't pass --nodename to Postgres nodes in order to exercise the
        # automatic detection of the nodename.
        create_args = ['create', self.role.command(),
                       '--pgdata', self.datadir,
                       '--pghost', pghost,
                       '--pgport', str(self.port),
                       '--pgctl', shutil.which('pg_ctl'),
                       '--monitor', self.monitor.connection_string()]

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
        else:
            self.pg_autoctl.execute("pg_autoctl create")


    def wait_until_state(self, target_state,
                         timeout=STATE_CHANGE_TIMEOUT, sleep_time=1):
        """
        Waits until this data node reaches the target state, and then returns
        True. If this doesn't happen until "timeout" seconds, returns False.
        """
        prev_state = None
        for i in range(timeout):
            # ensure we read from the pg_autoctl process pipe
            if self.pg_autoctl and self.pg_autoctl.run_proc:
                self.pg_autoctl.consume_output(sleep_time)
            else:
                time.sleep(sleep_time)

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

        else:
            print("%s didn't reach %s after %d attempts" %
                (self.datadir, target_state, timeout))

            events = self.get_events_str()
            pglogs = self.get_postgres_logs()

            if self.pg_autoctl and self.pg_autoctl.run_proc:
                out, err = self.stop_pg_autoctl()
                raise Exception("%s failed to reach %s after %d attempts: " \
                                "\n%s\n%s\n%s\n%s" %
                                (self.datadir, target_state, timeout,
                                 out, err, events, pglogs))
            else:
                raise Exception("%s failed to reach %s after %d attempts:\n%s" %
                                (self.datadir, target_state, timeout, events))
            return False

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
        last_events_query = "select nodeid, nodename, " \
            "reportedstate, goalstate, " \
            "reportedrepstate, reportedlsn, description " \
            "from pgautofailover.last_events('default', count => 20)"
        return self.monitor.run_sql_query(last_events_query)


    def get_events_str(self):
        return "\n".join(
            ["%s:%-14s %17s/%-17s %7s %10s %s" % ("id", "nodename",
                                                  "state", "goal state",
                                                  "repl st", "lsn", "event")]
            +
            ["%2d:%-14s %17s/%-17s %7s %10s %s" % (id, n, rs, gs, reps, lsn, desc)
             for id, n, rs, gs, reps, lsn, desc in self.get_events()])

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
            if command.run_proc and command.run_proc.returncode == 1:
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
        """
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
    def __init__(self, datadir, vnode, port, nodename, authMethod):

        super().__init__(datadir, vnode, port,
                         "autoctl_node", authMethod,
                         "pg_auto_failover", Role.Monitor)

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
                       '--nodename', self.nodename]

        if self.authMethod:
            create_args += ['--auth', self.authMethod]

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

        # pass true or false to --enable-secondary or --disable-secondary,
        # only when ha is actually set by the user
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
                raise Exception("%s failed, out: %s\n, err: %s" %
                                (name, self.out, self.err))
            return self.out, self.err

        except subprocess.TimeoutExpired:
            # we already spent our allocated waiting time, just kill the process
            self.run_proc.kill()
            self.run_proc.wait()
            self.run_proc.release()

            self.run_proc = None

            raise Exception("%s timed out after %d seconds." %
                            (name, COMMAND_TIMEOUT))

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

    def consume_output(self, secs=1):
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
