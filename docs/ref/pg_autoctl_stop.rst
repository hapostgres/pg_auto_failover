.. _pg_autoctl_stop:

pg_autoctl stop
===============

pg_autoctl stop - signal the pg_autoctl service for it to stop

Synopsis
--------

This commands stops the processes needed to run a monitor node or a keeper
node, depending on the configuration file that belongs to the ``--pgdata``
option or ``PGDATA`` environment variable.

::

  usage: pg_autoctl stop  [ --pgdata --fast --immediate ]

  --pgdata      path to data directory
  --fast        fast shutdown mode for the keeper
  --immediate   immediate shutdown mode for the keeper

Description
-----------

The ``pg_autoctl stop`` commands finds the PID of the running service for
the given ``--pgdata``, and if the process is still running, sends a
``SIGTERM`` signal to the process.

When ``pg_autoclt`` receives a shutdown signal a shutdown sequence is
triggered. Depending on the signal received, an operation that has been
started (such as a state transition) is either run to completion, stopped as
the next opportunity, or stopped immediately even when in the middle of the
transition.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--fast

  Fast Shutdown mode for ``pg_autoctl``. Sends the ``SIGINT`` signal to the
  running service, which is the same as using ``C-c`` on an interactive
  process running as a foreground shell job.

--immediate

  Immediate Shutdown mode for ``pg_autoctl``. Sends the ``SIGQUIT`` signal
  to the running service.
