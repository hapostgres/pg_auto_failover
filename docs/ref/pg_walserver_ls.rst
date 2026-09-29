.. _pg_walserver_ls:

pg_walserver ls
================

pg_walserver ls - Per-cluster storage summary: base backups, WAL, disk usage

Synopsis
--------

::

  pg_walserver ls --pgdata <path> [--config-file <path>] [--config]

Prints one row per configured route: how many base backups it holds
and their combined real size on disk, how many WAL segments it has
captured/archived and their combined size, and when its most recent
base backup was taken. Real PostgreSQL archiving practice treats an
archive's disk footprint as something that needs active watching -- a
WAL archive grows without bound until something prunes it (see
:ref:`pg_walserver_archive_cleanup`), and how far behind the most
recent base backup is directly bounds how long a restore's WAL replay
takes -- and those are exactly the two questions this command answers
at a glance. This is never the archived WAL or base backup data's own
full inventory -- see :ref:`pg_walserver_list` for every individual
file, one row each.

``--config`` shows a different, unrelated view instead: pg_walserver's
own config/credential/certificate files (``pg_walserver.ini``,
``pg_walserver_hba.conf``, ``pg_walserver_passwd``, ``server.crt``,
``server.key``, ``ca.crt``) -- written once, by hand or by
``setup``/``create-cert``, rarely worth checking again, so never the
default.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``); also
  where ``--config``'s own ``pg_walserver.ini`` row checks, when it
  differs from the default.

--config

  List the config/credential/certificate files instead of the
  per-cluster storage summary.

Examples
--------

Default output, the per-cluster storage summary::

  archive$ pg_walserver ls
  CLUSTER              BACKUPS  BACKUP SIZE  WAL FILES  WAL SIZE   TOTAL SIZE  LAST BACKUP
  ------------------------------------------------------------------------------------------------------
  mycluster            1        52.7MB       5+1        96.0MB     148.7MB     2026-09-28T20:42:33Z
  another              2        105.3MB      0          0.0B       105.3MB     2026-09-28T20:43:37Z

  (config/credential/certificate files omitted; pass --config to list those instead)

``5+1`` under WAL FILES means 5 complete segments plus 1 still being
written (a ``.partial`` file) -- ``another`` has none at all: it was
set up with ``--no-receivewal`` and fed only by ``archive-wal`` pushes,
none of which have happened yet.

With ``--config``, the config/credential/certificate file tier::

  archive$ pg_walserver ls --config
  FILE                     EXISTS  SIZE       MODIFIED
  ----------------------------------------------------------------
  pg_walserver.ini         yes     247.0B     2026-09-28T20:43:35Z
  pg_walserver_hba.conf    yes     117.0B     2026-09-28T20:43:35Z
  pg_walserver_passwd      yes     148.0B     2026-09-28T20:42:28Z
  server.crt               yes     4.0KB      2026-09-28T20:42:28Z
  server.key               yes     1.7KB      2026-09-28T20:42:28Z
  ca.crt                   no      -          -

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
* :ref:`pg_walserver_archive_cleanup`
