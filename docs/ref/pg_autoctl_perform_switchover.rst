.. _pg_autoctl_perform_switchover:

pg_autoctl perform switchover
=============================

pg_autoctl perform switchover - Perform a switchover for given formation and group

Synopsis
--------

This command starts a Postgres switchover orchestration from the
pg_auto_switchover monitor::

  usage: pg_autoctl perform switchover  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --formation   formation to target, defaults to 'default'
  --group       group to target, defaults to 0

Description
-----------

The pg_auto_switchover monitor can be used to orchestrate a manual switchover,
sometimes also known as a switchover. When doing so, split-brain are
prevented thanks to intermediary states being used in the Finite State
Machine.

The ``pg_autoctl perform switchover`` command waits until the switchover is
known complete on the monitor, or until the hard-coded 60s timeout has
passed.

The switchover orchestration is done in the background by the monitor, so even
if the ``pg_autoctl perform switchover`` stops on the timeout, the switchover
orchestration continues at the monitor.

See also :ref:`pg_autoctl_perform_failover`, a synonym for this command.

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
