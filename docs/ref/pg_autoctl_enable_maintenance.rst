.. _pg_autoctl_enable_maintenance:

pg_autoctl enable maintenance
=============================

pg_autoctl enable maintenance - Enable Postgres maintenance mode on this node

Synopsis
--------

A pg_auto_failover can be put to a maintenance state. The Postgres node is
then still registered to the monitor, and is known to be unreliable until
maintenance is disabled. A node in the maintenance state is not a candidate
for promotion.

Typical use of the maintenance state include Operating System or Postgres
reboot, e.g. when applying security upgrades.

::

   usage: pg_autoctl enable maintenance  [ --pgdata --allow-failover ]

  --pgdata      path to data directory

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--formation

  Target formation where to enable secondary feature.

Examples
--------

::

   pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000760 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000760 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 | 0/4000760 |    read-only |           secondary |           secondary

   $ pg_autoctl enable maintenance --pgdata node3
   12:06:12 47086 INFO  Listening monitor notifications about state changes in formation "default" and group 0
   12:06:12 47086 INFO  Following table displays times when notifications are received
       Time |  Name |  Node |      Host:Port |       Current State |      Assigned State
   ---------+-------+-------+----------------+---------------------+--------------------
   12:06:12 | node1 |     1 | localhost:5501 |             primary |        join_primary
   12:06:12 | node3 |     3 | localhost:5503 |           secondary |    wait_maintenance
   12:06:12 | node3 |     3 | localhost:5503 |    wait_maintenance |    wait_maintenance
   12:06:12 | node1 |     1 | localhost:5501 |        join_primary |        join_primary
   12:06:12 | node3 |     3 | localhost:5503 |    wait_maintenance |         maintenance
   12:06:12 | node1 |     1 | localhost:5501 |        join_primary |             primary
   12:06:13 | node3 |     3 | localhost:5503 |         maintenance |         maintenance

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000810 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/4000810 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 | 0/4000810 |         none |         maintenance |         maintenance
