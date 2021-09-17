.. _pg_autoctl_watch:

pg_autoctl watch
======================

pg_autoctl watch - Display an auto-updating dashboard

Synopsis
--------

This command outputs the events that the pg_auto_failover events records
about state changes of the pg_auto_failover nodes managed by the monitor::

  usage: pg_autoctl watch  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --monitor     show the monitor uri
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

--formation

  List the events recorded for nodes in the given formation. Defaults to
  ``default``.

--group

  Limit output to a single group in the formation. Default to including all
  groups registered in the target formation.

Description
-----------

The ``pg_autoctl watch`` output can includes the following columns, and
picks which columns are part of the output depending on the terminal window
size. This choice is dynamic and changes if your terminal window size
changes:

  - Name

	Name of the node.

  - Node, or Id

	Node information. When the formation has a single group (group zero),
	then this column only contains the nodeId.

	Only Citus formations allow several groups. When using a Citus formation
	the Node column contains the groupId and the nodeId, separated by a
	colon, such as ``0:1`` for the first coordinator node.

  - Reported Lag, or Lag(R)

	Time interval between now and the last known time when a node has
	reported to the monitor, using the ``node_active`` protocol.

	This value is expected to stay under 2s or abouts, and is known to
	increment when either the ``pg_autoctl run`` service is not running, or
	when there is a network split.

  - Health Lag, or Lag(H)

	Time inverval between now and the last known time when the monitor could
	connect to a node's Postgres instance, via its health check mechanism.

	This value is expected to stay under 6s or abouts, and is known to
	increment when either the Postgres service is not running on the target
	node, or when there is a network split.

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

  - Reported State

	The current FSM state as reported to the monitor by the pg_autoctl
	process running on the Postgres node.

  - Assigned State

	The assigned FSM state on the monitor. When the assigned state is not
	the same as the reported start, then the pg_autoctl process running on
	the Postgres node might have not retrieved the assigned state yet, or
	might still be implementing the FSM transition from the current state to
	the assigned state.
