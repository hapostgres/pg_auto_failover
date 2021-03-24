.. _pg_autoctl_get_formation_settings:

pg_autoctl get formation settings
=================================

pg_autoctl get formation settings - get replication settings for a formation from the monitor

Synopsis
--------

This command prints a ``pg_autoctl`` replication settings::

  usage: pg_autoctl get formation settings  [ --pgdata ] [ --json ] [ --formation ]

  --pgdata      path to data directory
  --json        output data in the JSON format
  --formation   pg_auto_failover formation

Description
-----------

See also :ref:`pg_autoctl_show_settings` which is a synonym.

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

Examples
--------

::

   $ pg_autoctl get formation settings
     Context |    Name |                   Setting | Value
   ----------+---------+---------------------------+-------------------------------------------------------------
   formation | default |      number_sync_standbys | 1
     primary |   node1 | synchronous_standby_names | 'ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)'
        node |   node1 |        candidate priority | 50
        node |   node2 |        candidate priority | 50
        node |   node3 |        candidate priority | 50
        node |   node1 |        replication quorum | true
        node |   node2 |        replication quorum | true
        node |   node3 |        replication quorum | true

   $ pg_autoctl get formation settings --json
   {
       "nodes": [
           {
               "value": "true",
               "context": "node",
               "node_id": 1,
               "setting": "replication quorum",
               "group_id": 0,
               "nodename": "node1"
           },
           {
               "value": "true",
               "context": "node",
               "node_id": 2,
               "setting": "replication quorum",
               "group_id": 0,
               "nodename": "node2"
           },
           {
               "value": "true",
               "context": "node",
               "node_id": 3,
               "setting": "replication quorum",
               "group_id": 0,
               "nodename": "node3"
           },
           {
               "value": "50",
               "context": "node",
               "node_id": 1,
               "setting": "candidate priority",
               "group_id": 0,
               "nodename": "node1"
           },
           {
               "value": "50",
               "context": "node",
               "node_id": 2,
               "setting": "candidate priority",
               "group_id": 0,
               "nodename": "node2"
           },
           {
               "value": "50",
               "context": "node",
               "node_id": 3,
               "setting": "candidate priority",
               "group_id": 0,
               "nodename": "node3"
           }
       ],
       "primary": [
           {
               "value": "'ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)'",
               "context": "primary",
               "node_id": 1,
               "setting": "synchronous_standby_names",
               "group_id": 0,
               "nodename": "node1"
           }
       ],
       "formation": {
           "value": "1",
           "context": "formation",
           "node_id": null,
           "setting": "number_sync_standbys",
           "group_id": null,
           "nodename": "default"
       }
   }
