.. _pg_autoctl_drop_node:

pg_autoctl drop node
====================

pg_autoctl drop node - Drop a node from the pg_auto_failover monitor

Synopsis
--------

This command drops a Postgres node from the pg_auto_failover monitor::

  usage: pg_autoctl drop node [ [ [ --pgdata ] [ --destroy ] ] | [ --monitor [ [ --hostname --pgport ] | [ --formation --name ] ] ] ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --formation   pg_auto_failover formation
  --name        drop the node with the given node name
  --hostname    drop the node with given hostname and pgport
  --pgport      drop the node with given hostname and pgport
  --destroy     also destroy Postgres database
  --force       force dropping the node from the monitor
  --wait        how many seconds to wait, default to 60

Description
-----------

Two modes of operations are implemented in the ``pg_autoctl drop node``
command.

When removing a node that still exists, it is possible to use ``pg_autoctl
drop node --destroy`` to remove the node both from the monitor and also
delete the local Postgres instance entirely.

When removing a node that doesn't exist physically anymore, or when the VM
that used to host the node has been lost entirely, use either the pair of
options ``--hostname`` and ``--pgport`` or the pair of options
``--formation`` and ``--name`` to match the node registration record on the
monitor database, and get it removed from the known list of nodes on the
monitor.

Then option ``--force`` can be used when the target node to remove does not
exist anymore. When a node has been lost entirely, it's not going to be able
to finish the procedure itself, and it is then possible to instruct the
monitor of the situation.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--hostname

  Hostname of the Postgres node to remove from the monitor. Use either
  ``--name`` or ``--hostname --pgport``, but not both.

--pgport

  Port of the Postgres node to remove from the monitor. Use either
  ``--name`` or ``--hostname --pgport``, but not both.

--name

  Name of the node to remove from the monitor. Use either ``--name`` or
  ``--hostname --pgport``, but not both.

--destroy

  By default the ``pg_autoctl drop monitor`` commands does not remove the
  Postgres database for the monitor. When using ``--destroy``, the Postgres
  installation is also deleted.

--force

  By default a node is expected to reach the assigned state DROPPED when it
  is removed from the monitor, and has the opportunity to implement clean-up
  actions. When the target node to remove is not available anymore, it is
  possible to use the option ``--force`` to immediately remove the node from
  the monitor.

--wait

  How many seconds to wait for the node to be dropped entirely. The command
  stops when the target node is not to be found on the monitor anymore, or
  when the timeout has elapsed, whichever comes first. The value 0 (zero)
  disables the timeout and disables waiting entirely, making the command
  async.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

PG_AUTOCTL_NODE_NAME

  Node name to register to the monitor, can be used instead of the
  ``--name`` option.

PG_AUTOCTL_REPLICATION_QUORUM

  Can be used instead of the ``--replication-quorum`` option.

PG_AUTOCTL_CANDIDATE_PRIORITY

  Can be used instead of the ``--candidate-priority`` option.

PG_CONFIG

  Can be set to the absolute path to the `pg_config`__ Postgres tool. This
  is mostly used in the context of building extensions, though it can be a
  useful way to select a Postgres version when several are installed on the
  same system.

  __ https://www.postgresql.org/docs/current/app-pgconfig.html

PATH

  Used the usual way mostly. Some entries that are searched in the PATH by
  the ``pg_autoctl`` command are expected to be found only once, to avoid
  mistakes with Postgres major versions.

PGHOST, PGPORT, PGDATABASE, PGUSER, PGCONNECT_TIMEOUT, ...

  See the `Postgres docs about Environment Variables`__ for details.
  
  __ https://www.postgresql.org/docs/current/libpq-envars.html

TMPDIR

  The pgcopydb command creates all its work files and directories in
  ``${TMPDIR}/pgcopydb``, and defaults to ``/tmp/pgcopydb``.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

Examples
--------

::

   $ pg_autoctl drop node --destroy --pgdata ./node3
   17:52:21 54201 INFO  Reaching assigned state "secondary"
   17:52:21 54201 INFO  Removing node with name "node3" in formation "default" from the monitor
   17:52:21 54201 WARN  Postgres is not running and we are in state secondary
   17:52:21 54201 WARN  Failed to update the keeper's state from the local PostgreSQL instance, see above for details.
   17:52:21 54201 INFO  Calling node_active for node default/4/0 with current state: PostgreSQL is running is false, sync_state is "", latest WAL LSN is 0/0.
   17:52:21 54201 INFO  FSM transition to "dropped": This node is being dropped from the monitor
   17:52:21 54201 INFO  Transition complete: current state is now "dropped"
   17:52:21 54201 INFO  This node with id 4 in formation "default" and group 0 has been dropped from the monitor
   17:52:21 54201 INFO  Stopping PostgreSQL at "/Users/dim/dev/MS/pg_auto_failover/tmux/node3"
   17:52:21 54201 INFO  /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl --pgdata /Users/dim/dev/MS/pg_auto_failover/tmux/node3 --wait stop --mode fast
   17:52:21 54201 INFO  /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl status -D /Users/dim/dev/MS/pg_auto_failover/tmux/node3 [3]
   17:52:21 54201 INFO  pg_ctl: no server running
   17:52:21 54201 INFO  pg_ctl stop failed, but PostgreSQL is not running anyway
   17:52:21 54201 INFO  Removing "/Users/dim/dev/MS/pg_auto_failover/tmux/node3"
   17:52:21 54201 INFO  Removing "/Users/dim/dev/MS/pg_auto_failover/tmux/config/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node3/pg_autoctl.cfg"
