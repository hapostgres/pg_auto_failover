.. _pg_autoctl_drop_formation:

pg_autoctl drop formation
=========================

pg_autoctl drop formation - Drop a formation on the pg_auto_failover monitor

Synopsis
--------

This command drops an existing formation on the monitor::

  usage: pg_autoctl drop formation  [ --pgdata --formation ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --formation   name of the formation to drop

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

--formation

  Name of the formation to drop from the monitor.
