.. _pg_walserver_ls:

pg_walserver ls
================

pg_walserver ls - Per-cluster storage summary: base backups, WAL, disk usage

Synopsis
--------

::

  pg_walserver ls --pgdata <path> [--config <path>] [--all]

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

``--all`` shows a different, unrelated view instead: pg_walserver's
own config/credential/certificate files (``pg_walserver.ini``,
``pg_walserver_hba.conf``, ``pg_walserver_passwd``, ``server.crt``,
``server.key``, ``ca.crt``) -- written once, by hand or by
``setup``/``create-cert``, rarely worth checking again, so never the
default.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``); also
  where ``--all``'s own ``pg_walserver.ini`` row checks, when it
  differs from the default.

--all, -a

  List the config/credential/certificate files instead of the
  per-cluster storage summary.

Examples
--------

Default output, the per-cluster storage summary::

  archive$ pg_walserver ls
  CLUSTER              BACKUPS  BACKUP SIZE  WAL FILES  WAL SIZE   TOTAL SIZE  LAST BACKUP
  -------------------- -------- ------------ ---------- ---------- ----------- -----------
  mycluster            1        38 MB        4+1        80 MB      118 MB      2026-09-30T14:56:19Z

  (config/credential/certificate files omitted; pass --all to list those instead)

``4+1`` under WAL FILES means 4 complete segments plus 1 still being
written (a ``.partial`` file). A route set up with ``--no-receivewal``
and never yet pushed to by ``archive-wal`` would show ``0`` there
instead, with ``BACKUP SIZE``/``TOTAL SIZE`` still real if it has taken
at least one base backup.

Sizes are formatted by this project's own shared
``pretty_print_bytes()`` (``src/bin/common/system_utils.c``, also used
by ``pg_autoctl``): an integer, a space, then a scaled unit
(``B``/``kB``/``MB``/``GB``/...), switching once a value reaches 10240
of the current unit, not 1024 -- the same "one recognizable size class
at a time" convention ``pg_autoctl`` itself already uses elsewhere.

With ``--all``, the config/credential/certificate file tier::

  archive$ pg_walserver ls --all
  FILE                     EXISTS  SIZE       MODIFIED
  ------------------------ ------- ---------- --------
  pg_walserver.ini         yes     257 B      2026-09-30T14:56:19Z
  pg_walserver_hba.conf    yes     1520 B     2026-09-30T14:56:05Z
  pg_walserver_passwd      yes     148 B      2026-09-30T14:56:05Z
  server.crt               yes     4134 B     2026-09-30T14:56:00Z
  server.key               yes     1704 B     2026-09-30T14:56:00Z
  ca.crt                   no      -          -

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
* :ref:`pg_walserver_archive_cleanup`
