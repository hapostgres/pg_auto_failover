.. _pg_autoctl_perform_failover:

pg_autoctl perform failover
===========================

pg_autoctl perform failover - Perform a failover for given formation and group

Synopsis
--------

This command starts a Postgres failover orchestration from the
pg_auto_failover monitor::

  usage: pg_autoctl perform failover  [ --pgdata --formation --group ]

  --pgdata           path to data directory
  --formation        formation to target, defaults to 'default'
  --group            group to target, defaults to 0
  --wait             how many seconds to wait, default to 60
  --allow-data-loss  proceed even when quorum nodes have not reported their LSN

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

.. _perform_failover_allow_data_loss:

Recovering a failover stuck in ``report_lsn``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In a formation with ``number_sync_standbys >= 1``, a failover that loses the
primary and one quorum standby at the same time can get permanently stuck.
When the primary fails, the monitor drives all standby nodes into the
``report_lsn`` state so it can elect the most advanced one.  If a quorum
standby is unreachable, it never reports its LSN.  The monitor then refuses to
promote any remaining candidate — it cannot know whether the missing node
acknowledged the last synchronous commit and holds WAL that no surviving
standby has replicated yet.

This is the correct conservative default, controlled by the
``pgautofailover.guard_data_loss`` GUC (default ``true``).  When you have
determined that the missing node is permanently lost and you are willing to
accept the potential data loss, use ``--allow-data-loss`` to unblock the
election:

.. code-block:: bash

   pg_autoctl perform failover --allow-data-loss

The command opens a single transaction on the monitor, sets
``pgautofailover.guard_data_loss`` to ``false`` for that transaction only,
and calls ``perform_failover()``.  The monitor then selects the most advanced
surviving candidate and drives it toward ``prepare_promotion``.

.. warning::

   ``--allow-data-loss`` means exactly what it says.  If the missing quorum
   standby had acknowledged a synchronous commit that no surviving standby
   replicated, those transactions will be permanently lost once the new primary
   starts accepting writes.  Use this option only when the missing node cannot
   be recovered and the cluster being stuck is the worse outcome.

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

--wait

  How many seconds to wait for notifications about the promotion. The
  command stops when the promotion is finished (a node is primary), or when
  the timeout has elapsed, whichever comes first. The value 0 (zero)
  disables the timeout and allows the command to wait forever.

--allow-data-loss

  Disable the ``pgautofailover.guard_data_loss`` protection for this
  failover only.  When set, the monitor will promote the most advanced
  surviving candidate even if one or more quorum standby nodes have not yet
  reported their LSN position.  Committed transactions on the missing node
  may be permanently lost.  See :ref:`perform_failover_allow_data_loss`.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

PGHOST, PGPORT, PGDATABASE, PGUSER, PGCONNECT_TIMEOUT, ...

  See the `Postgres docs about Environment Variables`__ for details.
  
  __ https://www.postgresql.org/docs/current/libpq-envars.html

TMPDIR

  The pgcopydb command creates all its work files and directories in
  ``${TMPDIR}/pgcopydb``, and defaults to ``/tmp/pgcopydb``.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

  
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

Example: unblocking a stuck election with ``--allow-data-loss``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In this example ``node1`` (primary) and ``node3`` (a quorum standby) have
both failed.  ``node2`` reaches ``report_lsn`` and waits indefinitely because
``node3`` never reported its LSN::

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 |   0/0     |       none   |            draining |            draining
   node2 |     2 | localhost:5502 | 0/5001F00 |    read-only |          report_lsn |          report_lsn
   node3 |     3 | localhost:5503 |   0/0     |       none   |           secondary |          report_lsn

``node3`` is assigned ``report_lsn`` but its current state is still
``secondary`` — it has not reported and likely never will.  The election is
stuck.  After confirming ``node3`` is permanently lost::

   $ pg_autoctl perform failover --allow-data-loss
   11:24:05 INFO  Disabling guard_data_loss for this failover (--allow-data-loss)
   11:24:05 INFO  Listening monitor notifications about state changes in formation "default" and group 0
       Time |  Name |  Node |      Host:Port |       Current State |      Assigned State
   ---------+-------+-------+----------------+---------------------+--------------------
   11:24:06 | node2 |     2 | localhost:5502 |          report_lsn |   prepare_promotion
   11:24:06 | node2 |     2 | localhost:5502 |   prepare_promotion |   prepare_promotion
   11:24:06 | node2 |     2 | localhost:5502 |   prepare_promotion |    stop_replication
   11:24:07 | node2 |     2 | localhost:5502 |    stop_replication |        wait_primary
   11:24:07 | node2 |     2 | localhost:5502 |        wait_primary |        wait_primary

   $ pg_autoctl show state
    Name |  Node |      Host:Port |       LSN |   Connection |       Current State |      Assigned State
   ------+-------+----------------+-----------+--------------+---------------------+--------------------
   node1 |     1 | localhost:5501 |   0/0     |       none   |            draining |            draining
   node2 |     2 | localhost:5502 | 0/5001F00 |   read-write |        wait_primary |        wait_primary
   node3 |     3 | localhost:5503 |   0/0     |       none   |           secondary |          report_lsn

``node2`` is now in ``wait_primary``.  It will become ``primary`` as soon as
either ``node1`` or ``node3`` reconnects and joins as a standby.
