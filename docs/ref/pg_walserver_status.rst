.. _pg_walserver_status:

pg_walserver status
====================

pg_walserver status - Show a short pg_walserver status dashboard

Synopsis
--------

::

  pg_walserver status --pgdata <path>

Prints a short, scannable dashboard: running or not (a real liveness
check, not just "the pidfile exists"), pid, how many clusters are
configured and how many already have a base backup, how many embedded
receivewal workers are running versus configured (with each running
worker's own current receiving LSN, when one has been observed -- see
:ref:`pg_walserver_ps`), and how many bootstrap backups are still
pending.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

Examples
--------

::

  archive$ pg_walserver status --pgdata /var/lib/archiver
  pg_walserver: running (pid 25671, uptime 0h04m31s)
    clusters:  2 configured, 2 with a base backup
    receivewal workers: 1/1 running
      mycluster            lsn 0/04000060 (timeline 1, 1s ago)
    bootstrap backups pending: 0

Nothing running::

  archive$ pg_walserver status --pgdata /var/lib/archiver
  pg_walserver: not running (--pgdata "/var/lib/archiver")

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_ps`
* :ref:`pg_walserver_list`
