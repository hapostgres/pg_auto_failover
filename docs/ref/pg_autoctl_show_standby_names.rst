.. _pg_autoctl_show_standby_names:

pg_autoctl show standby-names
=============================

pg_autoctl show standby-names - Prints synchronous_standby_names for a given group

Synopsis
--------

This command prints the current value for synchronous_standby_names for the
primary Postgres server of the target group (default ``0``) in the target
formation (default ``default``), as computed by the monitor::

  usage: pg_autoctl show standby-names  [ --pgdata ] --formation --group

    --pgdata      path to data directory
    --monitor     pg_auto_failover Monitor Postgres URL
    --formation   formation to query, defaults to 'default'
    --group       group to query formation, defaults to all
    --json        output data in the JSON format

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

  Defaults to the value of the environment variable ``PG_AUTOCTL_MONITOR``.

--formation

  Show the current ``synchronous_standby_names`` value for the given
  formation. Defaults to the ``default`` formation.

--group

  Show the current ``synchronous_standby_names`` value for the given group
  in the given formation. Defaults to group ``0``.

--json

  Output a JSON formated data instead of a table formatted list.

Examples
--------

::

   $ pg_autoctl show standby-names
   'ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)'

   $ pg_autoctl show standby-names --json
   {
       "formation": "default",
       "group": 0,
       "synchronous_standby_names": "ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)"
   }
