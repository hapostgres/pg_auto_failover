.. _pg_autoctl_reload:

pg_autoctl reload
=================

pg_autoctl reload - signal the pg_autoctl for it to reload its configuration

Synopsis
--------

This commands signals a running ``pg_autoctl`` process to reload its
configuration from disk, and also signal the managed Postgres service to
reload its configuration.

::

  usage: pg_autoctl reload  [ --pgdata ] [ --json ]

  --pgdata      path to data directory

Description
-----------

The ``pg_autoctl reload`` commands finds the PID of the running service for
the given ``--pgdata``, and if the process is still running, sends a
``SIGHUP`` signal to the process.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.
