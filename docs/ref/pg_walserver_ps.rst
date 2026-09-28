.. _pg_walserver_ps:

pg_walserver ps
================

pg_walserver ps - Show serve's own process-level status (pid, capturers, bootstrap jobs)

Synopsis
--------

::

  pg_walserver ps --pgdata <path>

Shows ``serve``'s own process-level status: its pid, every supervised
embedded pull capturer child (pid, cluster, running or stopped, uptime,
restart count), and any in-flight one-time bootstrap base backup job.
Reads the same pidfile :ref:`pg_walserver_reload` does to decide
whether ``serve`` is running at all; with none running, prints a clean
message and exits 0, never an error.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

Examples
--------

With ``serve`` running, one route with an embedded pull capturer::

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver serve: pid 25671, running, uptime 0h04m31s

  KIND     CLUSTER              PID      STATUS    UPTIME       RESTARTS
  ----------------------------------------------------------------------
  capture  mycluster            25673    running   0h04m31s     0

Nothing running::

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver: not running (--pgdata "/var/lib/archiver")

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
