.. _pg_walserver_ls:

pg_walserver ls
================

pg_walserver ls - List pg_walserver's own on-disk footprint under --pgdata

Synopsis
--------

::

  pg_walserver ls --pgdata <path> [--config]

Lists pg_walserver's own on-disk footprint under ``--pgdata``, one row
per well-known bookkeeping file, whether it exists, its size, and its
last-modified time. This is pg_walserver's own configuration
footprint, never the archived WAL or base backup data itself -- see
:ref:`pg_walserver_list` for that.

Two tiers. Confirming a config/credential/certificate file
(``pg_walserver.ini``, ``pg_walserver_hba.conf``, ``pg_walserver_passwd``,
``server.crt``, ``server.key``, ``ca.crt``) still exists tells an
operator little day to day: these are written once, by hand or by
``setup``/``create-cert``, and rarely change. They're omitted by
default; pass ``--config`` to include them. The runtime tier
(``pg_walserver.pid``, the ps state file) is what ``serve`` itself
keeps current while running, and always shows.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

--config

  Also list the config/credential/certificate files, omitted by
  default.

Examples
--------

Default output, runtime tier only::

  archive$ pg_walserver ls --pgdata /var/lib/archiver
  FILE                     EXISTS  SIZE       MODIFIED
  ----------------------------------------------------------------
  pg_walserver.pid         yes     8.0B       2026-09-28T19:26:42Z
  pg_walserver_ps.status   yes     293.0B     2026-09-28T19:26:58Z

  (config/credential/certificate files omitted; pass --config to include them)

With ``--config``, both tiers::

  archive$ pg_walserver ls --pgdata /var/lib/archiver --config
  FILE                     EXISTS  SIZE       MODIFIED
  ----------------------------------------------------------------
  pg_walserver.pid         yes     8.0B       2026-09-28T19:26:42Z
  pg_walserver_ps.status   yes     293.0B     2026-09-28T19:26:58Z
  pg_walserver.ini         yes     257.0B     2026-09-28T19:26:33Z
  pg_walserver_hba.conf    yes     60.0B      2026-09-28T19:26:42Z
  pg_walserver_passwd      yes     148.0B     2026-09-28T19:26:42Z
  server.crt               yes     4.0KB      2026-09-28T19:26:42Z
  server.key               yes     1.7KB      2026-09-28T19:26:42Z
  ca.crt                   no      -          -

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
