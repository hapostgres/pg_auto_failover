.. _pg_autoctl_enable_monitor:

pg_autoctl enable monitor
==========================

pg_autoctl enable monitor - Enable a monitor for this node to be orchestrated from

Synopsis
--------

It is possible to disable the pg_auto_failover monitor and enable it again
online in a running pg_autoctl Postgres node. The main use-cases where this
operation is useful is when the monitor node has to be replaced, either
after a full crash of the previous monitor node, of for migrating to a new
monitor node (hardware replacement, region or zone migration, etc).

::

   usage: pg_autoctl enable monitor  [ --pgdata --allow-failover ] postgres://autoctl_node@new.monitor.add.ress/pg_auto_failover

  --pgdata      path to data directory

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.


Examples
--------

::

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000760 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000760 |    read-only |           secondary |           secondary


   $ pg_autoctl enable monitor --pgdata node3 'postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=require'
   12:42:07 43834 INFO  Registered node 3 (localhost:5503) with name "node3" in formation "default", group 0, state "wait_standby"
   12:42:07 43834 INFO  Successfully registered to the monitor with nodeId 3
   12:42:08 43834 INFO  Still waiting for the monitor to drive us to state "catchingup"
   12:42:08 43834 WARN  Please make sure that the primary node is currently running `pg_autoctl run` and contacting the monitor.

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000810 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000810 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 | 0/4000810 |    read-only |           secondary |           secondary
