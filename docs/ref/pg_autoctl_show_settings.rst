.. _pg_autoctl_show_settings:

pg_autoctl show settings
========================

pg_autoctl show settings - Print replication settings for a formation from
the monitor

Synopsis
--------

This command allows to review all the replication settings of a given
formation (defaults to `'default'` as usual)::

  usage: pg_autoctl show settings  [ --pgdata ] [ --json ] [ --formation ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --json        output data in the JSON format
  --formation   pg_auto_failover formation

Description
-----------

See also :ref:`pg_autoctl_get_formation_settings` which is a synonym.

The output contains setting and values that apply at different contexts, as
shown here with a formation of four nodes, where ``node_4`` is not
participating in the replication quorum and also not a candidate for
failover::

  $ pg_autoctl show settings
     Context |    Name |                   Setting | Value
   ----------+---------+---------------------------+-------------------------------------------------------------
   formation | default |      number_sync_standbys | 1
     primary |  node_1 | synchronous_standby_names | 'ANY 1 (pgautofailover_standby_3, pgautofailover_standby_2)'
        node |  node_1 |        replication quorum | true
        node |  node_2 |        replication quorum | true
        node |  node_3 |        replication quorum | true
        node |  node_4 |        replication quorum | false
        node |  node_1 |        candidate priority | 50
        node |  node_2 |        candidate priority | 50
        node |  node_3 |        candidate priority | 50
        node |  node_4 |        candidate priority | 0

Three replication settings context are listed:

  1. The `"formation"` context contains a single entry, the value of
     ``number_sync_standbys`` for the target formation.

  2. The `"primary"` context contains one entry per group of Postgres nodes
     in the formation, and shows the current value of the
     ``synchronous_standby_names`` Postgres setting as computed by the
     monitor. It should match what's currently set on the primary node
     unless while applying a change, as shown by the primary being in the
     APPLY_SETTING state.

  3. The `"node"` context contains two entry per nodes, one line shows the
     replication quorum setting of nodes, and another line shows the
     candidate priority of nodes.

This command gives an overview of all the settings that apply to the current
formation.

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

  Show the current replication settings for the given formation. Defaults to
  the ``default`` formation.

--json

  Output a JSON formated data instead of a table formatted list.

Examples
--------

::

   $ pg_autoctl show settings
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
