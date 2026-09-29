.. _pg_walserver_status:

pg_walserver status
====================

pg_walserver status - Show a short pg_walserver status dashboard

Synopsis
--------

::

  pg_walserver status --pgdata <path> [--config-file <path>]

Prints a short, scannable *process* dashboard, and only that: running
or not (a real liveness check, not just "the pidfile exists"), pid,
uptime, how many embedded receivewal workers are running versus
configured, and how many bootstrap backups are still pending.
Deliberately never the archive data those processes maintain (which
cluster has a base backup, WAL range, per-worker LSN) -- that is
:ref:`pg_walserver_ps`'s (per-worker LSN) and :ref:`pg_walserver_list`'s
(storage-level facts) own job instead; ``status`` answers "is this
process, and its own child processes, alive and healthy", nothing more.

Options
-------

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

Examples
--------

::

  archive$ pg_walserver status
  pg_walserver: running (pid 27, uptime 0h00m03s)
    receivewal workers: 1/1 running
    bootstrap backups pending: 0

Nothing running::

  archive$ pg_walserver status --pgdata /nonexistent
  pg_walserver: not running (--pgdata "/nonexistent")

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_ps`
* :ref:`pg_walserver_stop`
* :ref:`pg_walserver_list`
