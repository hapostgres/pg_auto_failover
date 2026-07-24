.. _pg_autoctl_get_node_region:

pg_autoctl get node region
===========================

pg_autoctl get region - get region property from the monitor

Synopsis
--------

This command prints the ``pg_autoctl`` region label for a given node::

  usage: pg_autoctl get node region  [ --pgdata ] [ --json ] [ --formation ] [ --name ]

  --pgdata      path to data directory
  --formation   pg_auto_failover formation
  --name        pg_auto_failover node name
  --json        output data in the JSON format

Description
-----------

See also :ref:`pg_autoctl_set_node_region` to change the value, and
:ref:`pg_autoctl_show_settings` for the full list of replication settings.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output JSON formatted data.

--formation

  Show replication settings for given formation. Defaults to ``default``.

--name

  Show replication settings for given node, selected by name.

Examples
--------

::

   $ pg_autoctl get node region --name node1
   dc1

   $ pg_autoctl get node region --name node1 --json
   {
       "name": "node1",
       "region": "dc1"
   }
