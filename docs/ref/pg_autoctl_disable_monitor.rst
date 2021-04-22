.. _pg_autoctl_disable_monitor:

pg_autoctl disable monitor
==========================

pg_autoctl disable monitor - Disable the monitor for this node

Synopsis
--------

It is possible to disable the pg_auto_failover monitor and enable it again
online in a running pg_autoctl Postgres node. The main use-cases where this
operation is useful is when the monitor node has to be replaced, either
after a full crash of the previous monitor node, of for migrating to a new
monitor node (hardware replacement, region or zone migration, etc).

::

   usage: pg_autoctl disable monitor  [ --pgdata --force ]

  --pgdata      path to data directory
  --force       force unregistering from the monitor

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--force

  The ``--force`` covers the two following situations:

    1. By default, the command expects to be able to connect to the current
       monitor. When the current known monitor in the setup is not running
       anymore, use ``--force`` to skip this step.

    2. When ``pg_autoctl`` could connect to the monitor and the node is
       found there, this is normally an error that prevents from disabling
       the monitor. Using ``--force`` allows the command to drop the node
       from the monitor and continue with disabling the monitor.

Examples
--------

::

   $ pg_autoctl show state
       Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000148 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000148 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 | 0/4000148 |    read-only |           secondary |           secondary


   $ pg_autoctl disable monitor --pgdata node3
   12:41:21 43039 INFO  Found node 3 "node3" (localhost:5503) on the monitor
   12:41:21 43039 FATAL Use --force to remove the node from the monitor

   $ pg_autoctl disable monitor --pgdata node3 --force
   12:41:32 43219 INFO  Removing node 3 "node3" (localhost:5503) from monitor

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000760 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000760 |    read-only |           secondary |           secondary
