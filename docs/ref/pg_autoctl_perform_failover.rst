.. _pg_autoctl_perform_failover:

pg_autoctl perform failover
===========================

pg_autoctl perform failover - Perform a failover for given formation and group

Synopsis
--------

This command starts a Postgres failover orchestration from the
pg_auto_failover monitor::

  usage: pg_autoctl perform failover  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --formation   formation to target, defaults to 'default'
  --group       group to target, defaults to 0

Description
-----------

The pg_auto_failover monitor can be used to orchestrate a manual failover,
sometimes also known as a switchover. When doing so, split-brain are
prevented thanks to intermediary states being used in the Finite State
Machine.

The ``pg_autoctl perform failover`` command waits until the failover is
known complete on the monitor, or until the hard-coded 60s timeout has
passed.

The failover orchestration is done in the background by the monitor, so even
if the ``pg_autoctl perform failover`` stops on the timeout, the failover
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

--group

  Postgres group to target for the operation. Defaults to ``0``, only Citus
  formations may have more than one group.

Examples
--------

::

   $ pg_autoctl perform failover
   12:57:30 3635 INFO  Listening monitor notifications about state changes in formation "default" and group 0
   12:57:30 3635 INFO  Following table displays times when notifications are received
       Time |  Name |  Node |      Host:Port |       Current State |      Assigned State
   ---------+-------+-------+----------------+---------------------+--------------------
   12:57:30 | node1 |     1 | localhost:5501 |             primary |            draining
   12:57:30 | node1 |     1 | localhost:5501 |            draining |            draining
   12:57:30 | node2 |     2 | localhost:5502 |           secondary |          report_lsn
   12:57:30 | node3 |     3 | localhost:5503 |           secondary |          report_lsn
   12:57:36 | node3 |     3 | localhost:5503 |          report_lsn |          report_lsn
   12:57:36 | node2 |     2 | localhost:5502 |          report_lsn |          report_lsn
   12:57:36 | node2 |     2 | localhost:5502 |          report_lsn |   prepare_promotion
   12:57:36 | node2 |     2 | localhost:5502 |   prepare_promotion |   prepare_promotion
   12:57:36 | node2 |     2 | localhost:5502 |   prepare_promotion |    stop_replication
   12:57:36 | node1 |     1 | localhost:5501 |            draining |      demote_timeout
   12:57:36 | node3 |     3 | localhost:5503 |          report_lsn |      join_secondary
   12:57:36 | node1 |     1 | localhost:5501 |      demote_timeout |      demote_timeout
   12:57:36 | node3 |     3 | localhost:5503 |      join_secondary |      join_secondary
   12:57:37 | node2 |     2 | localhost:5502 |    stop_replication |    stop_replication
   12:57:37 | node2 |     2 | localhost:5502 |    stop_replication |        wait_primary
   12:57:37 | node1 |     1 | localhost:5501 |      demote_timeout |             demoted
   12:57:37 | node1 |     1 | localhost:5501 |             demoted |             demoted
   12:57:37 | node2 |     2 | localhost:5502 |        wait_primary |        wait_primary
   12:57:37 | node3 |     3 | localhost:5503 |      join_secondary |           secondary
   12:57:37 | node1 |     1 | localhost:5501 |             demoted |          catchingup
   12:57:38 | node3 |     3 | localhost:5503 |           secondary |           secondary
   12:57:38 | node2 |     2 | localhost:5502 |        wait_primary |             primary
   12:57:38 | node1 |     1 | localhost:5501 |          catchingup |          catchingup
   12:57:38 | node2 |     2 | localhost:5502 |             primary |             primary

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 | 0/4000F50 |    read-only |           secondary |           secondary
   node2 |     2 | localhost:5502 | 0/4000F50 |   read-write |             primary |             primary
   node3 |     3 | localhost:5503 | 0/4000F50 |    read-only |           secondary |           secondary
