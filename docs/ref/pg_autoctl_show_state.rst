.. _pg_autoctl_show_state:

pg_autoctl show state
=====================

pg_autoctl show state - Prints monitor's state of nodes in a given formation and group

Synopsis
--------

This command outputs the current state of the formation and groups
registered to the pg_auto_failover monitor::

  usage: pg_autoctl show state  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --formation   formation to query, defaults to 'default'
  --group       group to query formation, defaults to all
  --local       show local data, do not connect to the monitor
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

--group

  Limit output to a single group in the formation. Default to including all
  groups registered in the target formation.

--local

  Print the local state information without connecting to the monitor.

--watch

  Take control of the terminal and display the current state of the system
  and the last events from the monitor. The display is updated automatically
  every 500 milliseconds (half a second) and reacts properly to window size
  change.

  Depending on the terminal window size, a different set of columns is
  visible in the state part of the output. See :ref:`pg_autoctl_watch`.

--json

  Output a JSON formated data instead of a table formatted list.

Description
-----------

The ``pg_autoctl show state`` output includes the following columns:

  - Name

	Name of the node.

  - Node

	Node information. When the formation has a single group (group zero),
	then this column only contains the nodeId.

	Only Citus formations allow several groups. When using a Citus formation
	the Node column contains the groupId and the nodeId, separated by a
	colon, such as ``0:1`` for the first coordinator node.

  - Host:Port

	Hostname and port number used to connect to the node.

  - TLI: LSN

	Timeline identifier (TLI) and Postgres Log Sequence Number (LSN).

	The LSN is the current position in the Postgres WAL stream. This is a
	hexadecimal number. See `pg_lsn`__ for more information.

	__ https://www.postgresql.org/docs/current/datatype-pg-lsn.html

	The current `timeline`__ is incremented each time a failover happens, or
	when doing Point In Time Recovery. A node can only reach the secondary
	state when it is on the same timeline as its primary node.

	__ https://www.postgresql.org/docs/current/continuous-archiving.html#BACKUP-TIMELINES

  - Connection

	This output field contains two bits of information. First, the Postgres
	connection type that the node provides, either ``read-write`` or
	``read-only``. Then the mark ``!`` is added when the monitor has failed
	to connect to this node, and ``?`` when the monitor didn't connect to
	the node yet.

  - Current State

	The current FSM state as reported to the monitor by the pg_autoctl
	process running on the Postgres node.

  - Assigned State

	The assigned FSM state on the monitor. When the assigned state is not
	the same as the reported start, then the pg_autoctl process running on
	the Postgres node might have not retrieved the assigned state yet, or
	might still be implementing the FSM transition from the current state to
	the assigned state.

Examples
--------

::

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       TLI: LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+----------------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 |   1: 0/4000678 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 |   1: 0/4000678 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 |   1: 0/4000678 |    read-only |           secondary |           secondary

   $ pg_autoctl show state --local
    Name |  Node |      Host:Port |       TLI: LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+----------------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 |   1: 0/4000678 | read-write ? |             primary |             primary

   $ pg_autoctl show state --json
   [
       {
           "health": 1,
           "node_id": 1,
           "group_id": 0,
           "nodehost": "localhost",
           "nodename": "node1",
           "nodeport": 5501,
           "reported_lsn": "0/4000678",
           "reported_tli": 1,
           "formation_kind": "pgsql",
           "candidate_priority": 50,
           "replication_quorum": true,
           "current_group_state": "primary",
           "assigned_group_state": "primary"
       },
       {
           "health": 1,
           "node_id": 2,
           "group_id": 0,
           "nodehost": "localhost",
           "nodename": "node2",
           "nodeport": 5502,
           "reported_lsn": "0/4000678",
           "reported_tli": 1,
           "formation_kind": "pgsql",
           "candidate_priority": 50,
           "replication_quorum": true,
           "current_group_state": "secondary",
           "assigned_group_state": "secondary"
       },
       {
           "health": 1,
           "node_id": 3,
           "group_id": 0,
           "nodehost": "localhost",
           "nodename": "node3",
           "nodeport": 5503,
           "reported_lsn": "0/4000678",
           "reported_tli": 1,
           "formation_kind": "pgsql",
           "candidate_priority": 50,
           "replication_quorum": true,
           "current_group_state": "secondary",
           "assigned_group_state": "secondary"
       }
   ]
