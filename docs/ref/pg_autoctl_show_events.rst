.. _pg_autoctl_show_events:

pg_autoctl show events
======================

pg_autoctl show events - Prints monitor's state of nodes in a given formation and group

Synopsis
--------

This command outputs the events that the pg_auto_failover events records
about state changes of the pg_auto_failover nodes managed by the monitor::

  usage: pg_autoctl show events  [ --pgdata --formation --group --count ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --formation   formation to query, defaults to 'default'
  --group       group to query formation, defaults to all
  --count       how many events to fetch, defaults to 10
  --watch       display an auto-updating dashboard
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

--formation

  List the events recorded for nodes in the given formation. Defaults to
  ``default``.

--count

  By default only the last 10 events are printed.

--watch

  Take control of the terminal and display the current state of the system
  and the last events from the monitor. The display is updated automatically
  every 500 milliseconds (half a second) and reacts properly to window size
  change.

  Depending on the terminal window size, a different set of columns is
  visible in the state part of the output.

--json

  Output a JSON formated data instead of a table formatted list.

Examples
--------

::

   $ pg_autoctl show events --count 2 --json
   [
    {
        "nodeid": 1,
        "eventid": 15,
        "groupid": 0,
        "nodehost": "localhost",
        "nodename": "node1",
        "nodeport": 5501,
        "eventtime": "2021-03-18T12:32:36.103467+01:00",
        "goalstate": "primary",
        "description": "Setting goal state of node 1 \"node1\" (localhost:5501) to primary now that at least one secondary candidate node is healthy.",
        "formationid": "default",
        "reportedlsn": "0/4000060",
        "reportedstate": "wait_primary",
        "reportedrepstate": "async",
        "candidatepriority": 50,
        "replicationquorum": true
    },
    {
        "nodeid": 1,
        "eventid": 16,
        "groupid": 0,
        "nodehost": "localhost",
        "nodename": "node1",
        "nodeport": 5501,
        "eventtime": "2021-03-18T12:32:36.215494+01:00",
        "goalstate": "primary",
        "description": "New state is reported by node 1 \"node1\" (localhost:5501): \"primary\"",
        "formationid": "default",
        "reportedlsn": "0/4000110",
        "reportedstate": "primary",
        "reportedrepstate": "quorum",
        "candidatepriority": 50,
        "replicationquorum": true
    }
   ]
