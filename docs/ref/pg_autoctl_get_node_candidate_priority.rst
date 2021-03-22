.. _pg_autoctl_get_node_candidate_priority:

pg_autoctl get node candidate-priority
======================================

pg_autoctl get candidate-priority - get candidate-priority property from the monitor

Synopsis
--------

This command prints ``pg_autoctl`` candidate priority for a given node::

  usage: pg_autoctl get node candidate-priority  [ --pgdata ] [ --json ] [ --formation ] [ --name ]

  --pgdata      path to data directory
  --formation   pg_auto_failover formation
  --name        pg_auto_failover node name
  --json        output data in the JSON format

Description
-----------

See also :ref:`pg_autoctl_show_settings` for the full list of replication
settings.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output JSON formated data.

--formation

  Show replication settings for given formation. Defaults to ``default``.

--name

  Show replication settings for given node, selected by name.

Examples
--------

::

   $ pg_autoctl get node candidate-priority --name node1
   50

   $ pg_autoctl get node candidate-priority --name node1 --json
   {
       "name": "node1",
       "candidate-priority": 50
   }
