.. _pg_autoctl_show_uri:

pg_autoctl show uri
===================

pg_autoctl show uri - Show the postgres uri to use to connect to
pg_auto_failover nodes

Synopsis
--------

This command outputs the monitor or the coordinator Postgres URI to use from
an application to connect to Postgres::

  usage: pg_autoctl show uri  [ --pgdata --monitor --formation --json ]

    --pgdata      path to data directory
    --monitor     monitor uri
    --formation   show the coordinator uri of given formation
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

  When ``--formation`` is used, lists the Postgres URIs of all known
  formations on the monitor.

--json

  Output a JSON formated data instead of a table formatted list.

Examples
--------

::

   $ pg_autoctl show uri
           Type |    Name | Connection String
   -------------+---------+-------------------------------
        monitor | monitor | postgres://autoctl_node@localhost:5500/pg_auto_failover
      formation | default | postgres://localhost:5502,localhost:5503,localhost:5501/demo?target_session_attrs=read-write&sslmode=prefer

   $ pg_autoctl show uri --formation monitor
   postgres://autoctl_node@localhost:5500/pg_auto_failover

   $ pg_autoctl show uri --formation default
   postgres://localhost:5503,localhost:5502,localhost:5501/demo?target_session_attrs=read-write&sslmode=prefer

   $ pg_autoctl show uri --json
   [
    {
        "uri": "postgres://autoctl_node@localhost:5500/pg_auto_failover",
        "name": "monitor",
        "type": "monitor"
    },
    {
        "uri": "postgres://localhost:5503,localhost:5502,localhost:5501/demo?target_session_attrs=read-write&sslmode=prefer",
        "name": "default",
        "type": "formation"
    }
   ]


Multi-hosts Postgres connection strings
---------------------------------------

PostgreSQL since version 10 includes support for multiple hosts in its
connection driver ``libpq``, with the special ``target_session_attrs``
connection property.

This multi-hosts connection string facility allows applications to keep
using the same stable connection string over server-side failovers. That's
why ``pg_autoctl show uri`` uses that format.
