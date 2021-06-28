.. _pg_autoctl_set_node_candidate_priority:

pg_autoctl set node candidate-priority
======================================

pg_autoctl set candidate-priority - set candidate-priority property from the monitor

Synopsis
--------

This command sets the ``pg_autoctl`` candidate priority for a given node::

  usage: pg_autoctl set node candidate-priority  [ --pgdata ] [ --json ] [ --formation ] [ --name ] <priority: 0..100>

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

   $ pg_autoctl set node candidate-priority --name node1 65
   12:47:59 92326 INFO  Waiting for the settings to have been applied to the monitor and primary node
   12:47:59 92326 INFO  New state is reported by node 1 "node1" (localhost:5501): "apply_settings"
   12:47:59 92326 INFO  Setting goal state of node 1 "node1" (localhost:5501) to primary after it applied replication properties change.
   12:47:59 92326 INFO  New state is reported by node 1 "node1" (localhost:5501): "primary"
   65

   $ pg_autoctl set node candidate-priority --name node1 50 --json
   12:48:05 92450 INFO  Waiting for the settings to have been applied to the monitor and primary node
   12:48:05 92450 INFO  New state is reported by node 1 "node1" (localhost:5501): "apply_settings"
   12:48:05 92450 INFO  Setting goal state of node 1 "node1" (localhost:5501) to primary after it applied replication properties change.
   12:48:05 92450 INFO  New state is reported by node 1 "node1" (localhost:5501): "primary"
   {
       "candidate-priority": 50
   }
