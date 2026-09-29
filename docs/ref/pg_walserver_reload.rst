.. _pg_walserver_reload:

pg_walserver reload
====================

pg_walserver reload - Ask a running pg_walserver to reload its configuration

Synopsis
--------

Sends ``SIGHUP`` to the running ``pg_walserver serve`` instance whose pid
is recorded in ``<pgdata>/pg_walserver.pid``, the same shape as
``pg_ctl reload``::

  $ pg_walserver reload --pgdata /var/lib/archiver

Exits 0 once the signal was delivered. Exits nonzero, with a clear error,
if the pidfile is missing, stale, or unreadable. See
:ref:`pg_walserver`'s "Configuration reload" for what a reload actually
does.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``. The
  pidfile read is ``<pgdata>/pg_walserver.pid``.

Examples
--------

::

  archive$ pg_walserver reload
  21:11:36 2585328 INFO  Sent SIGHUP to pg_walserver pid 2584314

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_register`
