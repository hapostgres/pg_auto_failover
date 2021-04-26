.. _pg_autoctl_perform_promotion:

pg_autoctl perform promotion
============================

pg_autoctl perform promotion - Perform a failover that promotes a target node

Synopsis
--------

This command starts a Postgres failover orchestration from the
pg_auto_promotion monitor and targets given node::

  usage: pg_autoctl perform promotion  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --formation   formation to target, defaults to 'default'
  --name        node name to target, defaults to current node
  --wait        how many seconds to wait, default to 60

Description
-----------

The pg_auto_promotion monitor can be used to orchestrate a manual promotion,
sometimes also known as a switchover. When doing so, split-brain are
prevented thanks to intermediary states being used in the Finite State
Machine.

The ``pg_autoctl perform promotion`` command waits until the promotion is
known complete on the monitor, or until the hard-coded 60s timeout has
passed.

The promotion orchestration is done in the background by the monitor, so even
if the ``pg_autoctl perform promotion`` stops on the timeout, the promotion
orchestration continues at the monitor.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--formation

  Formation to target for the operation. Defaults to ``default``.

--name

  Name of the node that should be elected as the new primary node.

--wait

  How many seconds to wait for notifications about the promotion. The
  command stops when the promotion is finished (a node is primary), or when
  the timeout has elapsed, whichever comes first. The value 0 (zero)
  disables the timeout and allows the command to wait forever.

Examples
--------

::

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000F88 |    read-only |           secondary |           secondary
   node2 |     2 | localhost:5502 | 0/4000F88 |   read-write |             primary |             primary
   node3 |     3 | localhost:5503 | 0/4000F88 |    read-only |           secondary |           secondary


   $ pg_autoctl perform promotion --name node1
   13:08:13 15297 INFO  Listening monitor notifications about state changes in formation "default" and group 0
   13:08:13 15297 INFO  Following table displays times when notifications are received
       Time |  Name |  Node |      Host:Port |       Current State |      Assigned State
   ---------+-------+-------+----------------+---------------------+--------------------
   13:08:13 | node1 |   0/1 | localhost:5501 |           secondary |           secondary
   13:08:13 | node2 |   0/2 | localhost:5502 |             primary |            draining
   13:08:13 | node2 |   0/2 | localhost:5502 |            draining |            draining
   13:08:13 | node1 |   0/1 | localhost:5501 |           secondary |          report_lsn
   13:08:13 | node3 |   0/3 | localhost:5503 |           secondary |          report_lsn
   13:08:19 | node3 |   0/3 | localhost:5503 |          report_lsn |          report_lsn
   13:08:19 | node1 |   0/1 | localhost:5501 |          report_lsn |          report_lsn
   13:08:19 | node1 |   0/1 | localhost:5501 |          report_lsn |   prepare_promotion
   13:08:19 | node1 |   0/1 | localhost:5501 |   prepare_promotion |   prepare_promotion
   13:08:19 | node1 |   0/1 | localhost:5501 |   prepare_promotion |    stop_replication
   13:08:19 | node2 |   0/2 | localhost:5502 |            draining |      demote_timeout
   13:08:19 | node3 |   0/3 | localhost:5503 |          report_lsn |      join_secondary
   13:08:19 | node2 |   0/2 | localhost:5502 |      demote_timeout |      demote_timeout
   13:08:19 | node3 |   0/3 | localhost:5503 |      join_secondary |      join_secondary
   13:08:20 | node1 |   0/1 | localhost:5501 |    stop_replication |    stop_replication
   13:08:20 | node1 |   0/1 | localhost:5501 |    stop_replication |        wait_primary
   13:08:20 | node2 |   0/2 | localhost:5502 |      demote_timeout |             demoted
   13:08:20 | node1 |   0/1 | localhost:5501 |        wait_primary |        wait_primary
   13:08:20 | node3 |   0/3 | localhost:5503 |      join_secondary |           secondary
   13:08:20 | node2 |   0/2 | localhost:5502 |             demoted |             demoted
   13:08:20 | node2 |   0/2 | localhost:5502 |             demoted |          catchingup
   13:08:21 | node3 |   0/3 | localhost:5503 |           secondary |           secondary
   13:08:21 | node1 |   0/1 | localhost:5501 |        wait_primary |             primary
   13:08:21 | node2 |   0/2 | localhost:5502 |          catchingup |          catchingup
   13:08:21 | node1 |   0/1 | localhost:5501 |             primary |             primary

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/40012F0 |   read-write |             primary |             primary
   node2 |     2 | localhost:5502 | 0/40012F0 |    read-only |           secondary |           secondary
   node3 |     3 | localhost:5503 | 0/40012F0 |    read-only |           secondary |           secondary
