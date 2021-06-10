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

Examples
--------

::

   $ pg_autoctl drop node --destroy --pgdata ./node3
   17:49:42 12504 INFO  Removing local node from the pg_auto_failover monitor.
   17:49:42 12504 INFO  Removing local node state file: "/Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node3/pg_autoctl.state"
   17:49:42 12504 INFO  Removing local node init state file: "/Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node3/pg_autoctl.init"
   17:49:42 12504 INFO  Removed pg_autoctl node at "/Users/dim/dev/MS/pg_auto_failover/tmux/node3" from the monitor and removed the state file "/Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node3/pg_autoctl.state"
   17:49:42 12504 INFO  Stopping PostgreSQL at "/Users/dim/dev/MS/pg_auto_failover/tmux/node3"
   17:49:42 12504 INFO  /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl --pgdata /Users/dim/dev/MS/pg_auto_failover/tmux/node3 --wait stop --mode fast
   17:49:42 12504 INFO  /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl status -D /Users/dim/dev/MS/pg_auto_failover/tmux/node3 [3]
   17:49:42 12504 INFO  pg_ctl: no server running
   17:49:42 12504 INFO  pg_ctl stop failed, but PostgreSQL is not running anyway
   17:49:42 12504 INFO  Removing "/Users/dim/dev/MS/pg_auto_failover/tmux/node3"
   17:49:42 12504 INFO  Removing "/Users/dim/dev/MS/pg_auto_failover/tmux/config/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node3/pg_autoctl.cfg"
