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

  archive$ pg_walserver status
  pg_walserver: running (pid 2584314, uptime 0h00m14s)
    clusters:  2 configured, 2 with a base backup
    receivewal workers: 1/1 running
      mycluster            lsn 0/19000168 (timeline 1, 3s ago)
    bootstrap backups pending: 0

Nothing running::

  archive$ pg_walserver status --pgdata /nonexistent
  pg_walserver: not running (--pgdata "/nonexistent")

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_ps`
* :ref:`pg_walserver_list`
