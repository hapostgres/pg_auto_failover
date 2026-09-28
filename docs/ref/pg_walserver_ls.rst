.. _pg_walserver_ls:

pg_walserver ls
================

pg_walserver ls - List pg_walserver's own on-disk footprint under --pgdata

Synopsis
--------

::

  pg_walserver ls --pgdata <path>

Lists pg_walserver's own on-disk footprint under ``--pgdata``: one row per
well-known bookkeeping file (``pg_walserver.ini``,
``pg_walserver_hba.conf``, ``pg_walserver_passwd``, ``server.crt``,
``server.key``, ``ca.crt``, ``pg_walserver.pid``), whether it exists, its
size, and its last-modified time. This is pg_walserver's own
configuration footprint, never the archived WAL or base backup data
itself -- see :ref:`pg_walserver_list` for that.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

Examples
--------

::

  archive$ pg_walserver ls --pgdata /var/lib/archiver
  FILE                     EXISTS  SIZE       MODIFIED
  ----------------------------------------------------------------
  pg_walserver.ini         yes     486.0B     2026-09-28T13:45:24Z
  pg_walserver_hba.conf    yes     118.0B     2026-09-28T13:45:24Z
  pg_walserver_passwd      yes     148.0B     2026-09-28T13:41:17Z
  server.crt               yes     4.1KB      2026-09-28T13:42:45Z
  server.key               yes     1.7KB      2026-09-28T13:42:45Z
  ca.crt                   no      -          -
  pg_walserver.pid         yes     7.0B       2026-09-28T13:41:17Z

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
