.. _pg_walserver_stop:

pg_walserver stop
===================

pg_walserver stop - Stop a running pg_walserver cleanly

Synopsis
--------

Sends ``SIGTERM`` to the running ``pg_walserver serve`` instance whose
pid is recorded in ``<pgdata>/pg_walserver.pid``, the same shape as
``pg_ctl stop``::

  $ pg_walserver stop --pgdata /var/lib/archiver

Exits 0 once the signal was delivered. Exits nonzero, with a clear
error, if the pidfile is missing, stale, or unreadable -- the same
"no running instance" case :ref:`pg_walserver_status` reports cleanly,
never fatal there, but a real error here since a caller of ``stop``
explicitly expects something to be running.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.
  The pidfile read is ``<pgdata>/pg_walserver.pid``.

Examples
--------

::

  archive$ pg_walserver stop
  11:00:15 56 INFO  Sent SIGTERM to pg_walserver pid 27

Calling it again once the instance is already down::

  archive$ pg_walserver stop
  11:00:16 68 FATAL Failed to stop pg_walserver: no running instance found at "/var/lib/postgres/ws/pg_walserver.pid" (missing, stale, or unreadable pidfile)

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_reload`
