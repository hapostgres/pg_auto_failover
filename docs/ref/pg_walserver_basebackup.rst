.. _pg_walserver_basebackup:

pg_walserver basebackup
========================

pg_walserver basebackup - Take a base backup of a route's upstream

Synopsis
--------

::

  pg_walserver basebackup --pgdata <path> --cluster <name>
      [--path <dir>] [--upstream <conninfo> | --host <host> [--port <port>] [--user <name>]]
      [--keep-count <N>] [--keep-age <interval>] [--dry-run] [--force]

Takes a real base backup from a route's upstream into
``<path>/basebackups/<label>/``, then updates
``<path>/basebackups/.latest`` once the backup is verified complete,
using a ``pg_basebackup`` client picked to match the upstream's own
recorded Postgres version (written by :ref:`pg_walserver_fetch_systemid`/
``setup``), not just whatever happens to be first on ``$PATH`` --
PostgreSQL's own compatibility contract only ever guarantees a client
working with a server of the *same or older* major version, never a
newer one. ``pg_walserver serve`` already takes a route's first base
backup automatically; run this by hand, or from a cron job, whenever a
fresh one is wanted afterward.

With ``--keep-count``/``--keep-age``, one cron line can both take a
new backup and immediately prune what the retention policy no longer
needs, instead of scheduling ``basebackup`` and
:ref:`pg_walserver_archive_cleanup` separately -- see that page below
for the full retention behavior this composes with, unchanged.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  The cluster name to back up, looked up in ``pg_walserver.ini``.

--path

  The route's own directory. Overrides the route's own ``path``
  property.

--upstream

  A libpq connection string to connect with. Overrides the route's own
  ``upstream`` property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--keep-count

  After taking the backup, also run :ref:`pg_walserver_archive_cleanup`'s
  own retention pass, keeping at least this many of the most recent base
  backups. Optional; with neither ``--keep-count`` nor ``--keep-age``, no
  cleanup is attempted.

--keep-age

  ... keeping every base backup taken within this long
  (``72h``/``30d``/``4w``/``3m``); the more conservative of
  ``--keep-count``/``--keep-age`` wins when both are given.

--dry-run

  With ``--keep-count``/``--keep-age``, report what the cleanup pass
  would remove without removing anything. The backup itself is always
  taken for real.

--force

  With ``--keep-count``/``--keep-age``, bypass the cleanup pass's
  WAL-continuity refusal (same meaning as ``archive-cleanup``'s own
  ``--force``). Never bypasses the backup itself, and never turns a
  cleanup refusal into a lost backup: the backup just taken is always
  kept regardless of whether its own follow-on cleanup pass could
  safely proceed.

Examples
--------

::

  archive$ pg_walserver basebackup --pgdata /var/lib/archiver --cluster mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require"
  INFO  Taking a base backup of primary:5432 into
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T134237Z"
  INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known
        Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  INFO   /usr/lib/postgresql/17/bin/pg_basebackup -w -d
         'application_name=pg_walserver-basebackup host=primary port=5432
         user=archiver_repl sslmode=require' --pgdata
         /var/lib/archiver/mycluster/basebackups/basebackup-20260928T134237Z
         -U archiver_repl --verbose --progress --wal-method=stream
         --checkpoint=fast --label basebackup-20260928T134237Z
  INFO  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  INFO  pg_basebackup: checkpoint completed
  INFO  pg_basebackup: write-ahead log start point: 0/4000028 on timeline 1
  INFO  pg_basebackup: starting background WAL receiver
  INFO  pg_basebackup: created temporary replication slot "pg_basebackup_745442"
  INFO  23712/23712 kB (100%), 1/1 tablespace
  INFO  pg_basebackup: write-ahead log end point: 0/4000120
  INFO  pg_basebackup: waiting for background process to finish streaming ...
  INFO  pg_basebackup: syncing data to disk ...
  INFO  pg_basebackup: base backup completed
  INFO  Base backup "basebackup-20260928T134237Z" is now the latest for
        "/var/lib/archiver/mycluster"

Carrying its own WAL, concurrently, rather than depending on
``restore_command`` to fetch any segment written during the backup
itself, is why ``pg_basebackup`` is always invoked with
``--wal-method=stream``.

With ``--keep-count``/``--keep-age``, the same command also prunes
right after::

  archive$ pg_walserver basebackup --pgdata /var/lib/archiver --cluster mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require" --keep-count 1
  INFO  Base backup "basebackup-20260928T192713Z" is now the latest for
        "/var/lib/archiver/mycluster"
  INFO  archive-cleanup: --keep-count 1 -- retaining WAL from
        "000000010000000000000006" onward
  INFO  archive-cleanup: removing backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T192643Z":
        past the --keep-count cutoff
  INFO  archive-cleanup: removing
        "/var/lib/archiver/mycluster/000000010000000000000001": older than
        the retention cutoff ("000000010000000000000006")

A cleanup refusal (its WAL-continuity pre-flight found a real gap, and
``--force`` wasn't given) never costs the backup that was just taken::

  archive$ pg_walserver basebackup --pgdata /var/lib/archiver --cluster mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require" --keep-count 3
  INFO  Base backup "basebackup-20260928T192800Z" is now the latest for
        "/var/lib/archiver/mycluster"
  ERROR archive-cleanup: WAL continuity check failed for kept backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T192713Z"
        (requires WAL from "000000010000000000000006" onward): missing
        WAL segment "000000010000000000000007" (needed between
        "000000010000000000000006" and "000000010000000000000008")
  ERROR basebackup: the new base backup succeeded and has been kept, but
        the retention cleanup pass that followed it did not complete --
        see the error(s) logged above

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_archive_cleanup`
