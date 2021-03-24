.. _pg_autoctl_set_node_replication_quorum:

pg_autoctl set node replication-quorum
======================================

pg_autoctl set replication-quorum - set replication-quorum property from the monitor

Synopsis
--------

This command sets ``pg_autoctl`` replication quorum for a given node::

  usage: pg_autoctl set node replication-quorum  [ --pgdata ] [ --json ] [ --formation ] [ --name ] <true|false>

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

   $ pg_autoctl set node replication-quorum --name node1 false
   12:49:37 94092 INFO  Waiting for the settings to have been applied to the monitor and primary node
   12:49:37 94092 INFO  New state is reported by node 1 "node1" (localhost:5501): "apply_settings"
   12:49:37 94092 INFO  Setting goal state of node 1 "node1" (localhost:5501) to primary after it applied replication properties change.
   12:49:37 94092 INFO  New state is reported by node 1 "node1" (localhost:5501): "primary"
   false

   $ pg_autoctl set node replication-quorum --name node1 true --json
   12:49:42 94199 INFO  Waiting for the settings to have been applied to the monitor and primary node
   12:49:42 94199 INFO  New state is reported by node 1 "node1" (localhost:5501): "apply_settings"
   12:49:42 94199 INFO  Setting goal state of node 1 "node1" (localhost:5501) to primary after it applied replication properties change.
   12:49:43 94199 INFO  New state is reported by node 1 "node1" (localhost:5501): "primary"
   {
       "replication-quorum": true
   }
