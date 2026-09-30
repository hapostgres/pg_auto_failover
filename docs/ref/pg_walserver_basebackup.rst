.. _pg_walserver_basebackup:

pg_walserver basebackup
========================

pg_walserver basebackup - Take a base backup of a route's upstream

Synopsis
--------

::

  pg_walserver basebackup --pgdata <path> [--config <path>] --cluster <name>
      [--path <dir>] [--upstream <conninfo> | --host <host> [--port <port>] [--user <name>]]
      [--keep-count <N>] [--keep-age <interval>] [--dry-run] [--force]

Takes a real base backup from a route's upstream into
``<path>/basebackups/<label>/``, then updates
``<path>/basebackups/.latest`` once the backup is verified complete,
using a ``pg_basebackup`` client picked to match the upstream's own
recorded Postgres version (written by :ref:`pg_walserver_fetch_systemid`/
``pg_walserver cluster register``), not just whatever happens to be
first on ``$PATH`` --
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

  This instance's own data root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--cluster

  The cluster name to back up, looked up in the config file.

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

  archive$ PGPASSWORD=s3kr3t pg_walserver basebackup --cluster bbdemo \
      --upstream "host=primary port=5534 user=archiver_repl sslmode=require"
  21:15:04 2592271 INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  21:15:04 2592271 INFO  Taking a base backup of primary:5534 into "/var/lib/archiver/bbdemo/basebackups/basebackup-20260928T211504Z"
  21:15:04 2592271 INFO   /usr/lib/postgresql/17/bin/pg_basebackup -w -d 'application_name=pg_walserver_basebackup host=primary port=5534 user=archiver_repl sslmode=require' --pgdata /var/lib/archiver/bbdemo/basebackups/basebackup-20260928T211504Z -U archiver_repl --verbose --progress --wal-method=stream --checkpoint=fast --label basebackup-20260928T211504Z
  21:15:04 2592271 INFO  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  21:15:04 2592271 INFO  pg_basebackup: checkpoint completed
  21:15:04 2592271 INFO  pg_basebackup: write-ahead log start point: 0/29000028 on timeline 1
  21:15:04 2592271 INFO  pg_basebackup: starting background WAL receiver
  21:15:04 2592271 INFO  pg_basebackup: created temporary replication slot "pg_basebackup_2592275"
  21:15:04 2592271 INFO  41457/41457 kB (100%), 0/1 tablespace (...260928T211504Z/global/pg_control)
  21:15:04 2592271 INFO  41457/41457 kB (100%), 1/1 tablespace
  21:15:04 2592271 INFO  pg_basebackup: write-ahead log end point: 0/29000120
  21:15:04 2592271 INFO  pg_basebackup: waiting for background process to finish streaming ...
  21:15:04 2592271 INFO  pg_basebackup: syncing data to disk ...
  21:15:05 2592271 INFO  pg_basebackup: renaming backup_manifest.tmp to backup_manifest
  21:15:05 2592271 INFO  pg_basebackup: base backup completed
  21:15:05 2592271 INFO  Base backup "basebackup-20260928T211504Z" is now the latest for "/var/lib/archiver/bbdemo"

Carrying its own WAL, concurrently, rather than depending on
``restore_command`` to fetch any segment written during the backup
itself, is why ``pg_basebackup`` is always invoked with
``--wal-method=stream``.

With ``--keep-count``/``--keep-age``, the same command also prunes
right after -- a route with real captured WAL (``mycluster``, its
embedded receivewal worker running throughout this session's own
testing) genuinely has plenty to prune::

  archive$ PGPASSWORD=s3kr3t pg_walserver basebackup --cluster mycluster \
      --upstream "host=primary port=5534 user=archiver_repl sslmode=require" --keep-count 1
  21:15:26 2592866 INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  21:15:26 2592866 INFO  Taking a base backup of primary:5534 into "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T211526Z"
  21:15:26 2592866 INFO   /usr/lib/postgresql/17/bin/pg_basebackup -w -d 'application_name=pg_walserver_basebackup host=primary port=5534 user=archiver_repl sslmode=require' --pgdata /var/lib/archiver/mycluster/basebackups/basebackup-20260928T211526Z -U archiver_repl --verbose --progress --wal-method=stream --checkpoint=fast --label basebackup-20260928T211526Z
  21:15:26 2592866 INFO  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  21:15:26 2592866 INFO  pg_basebackup: checkpoint completed
  21:15:26 2592866 INFO  pg_basebackup: write-ahead log start point: 0/2C000028 on timeline 1
  21:15:26 2592866 INFO  pg_basebackup: starting background WAL receiver
  21:15:26 2592866 INFO  pg_basebackup: created temporary replication slot "pg_basebackup_2592870"
  21:15:26 2592866 INFO  41457/41457 kB (100%), 0/1 tablespace (...260928T211526Z/global/pg_control)
  21:15:26 2592866 INFO  41457/41457 kB (100%), 1/1 tablespace
  21:15:26 2592866 INFO  pg_basebackup: write-ahead log end point: 0/2C000120
  21:15:26 2592866 INFO  pg_basebackup: waiting for background process to finish streaming ...
  21:15:26 2592866 INFO  pg_basebackup: syncing data to disk ...
  21:15:27 2592866 INFO  pg_basebackup: renaming backup_manifest.tmp to backup_manifest
  21:15:27 2592866 INFO  pg_basebackup: base backup completed
  21:15:27 2592866 INFO  Base backup "basebackup-20260928T211526Z" is now the latest for "/var/lib/archiver/mycluster"
  21:15:27 2592866 INFO  archive-cleanup: --keep-count 1 -- retaining WAL from "00000001000000000000002C" onward
  21:15:27 2592866 INFO  archive-cleanup: removing backup "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T204233Z": past the --keep-count cutoff
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000017": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001B": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000020": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001A": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001D": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001F": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000029": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000014": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000022": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000016": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000019": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000015": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001E": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000001C": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000025": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000028": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000024": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000027": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000023": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000021": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000002A": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000026": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000018": older than the retention cutoff ("00000001000000000000002C")
  21:15:27 2592866 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/00000001000000000000002B": older than the retention cutoff ("00000001000000000000002C")

A cleanup refusal never costs the backup that was just taken --
``bbdemo`` was registered with ``--no-receivewal`` and nothing has ever
pushed WAL into it via ``archive-wal``, so it has no WAL at all: the
continuity check can never be satisfied for it, the whole cleanup pass
refuses, and the fresh backup stays::

  archive$ PGPASSWORD=s3kr3t pg_walserver basebackup --cluster bbdemo \
      --upstream "host=primary port=5534 user=archiver_repl sslmode=require" --keep-count 1
  21:15:13 2592535 INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  21:15:13 2592535 INFO  Taking a base backup of primary:5534 into "/var/lib/archiver/bbdemo/basebackups/basebackup-20260928T211513Z"
  21:15:13 2592535 INFO   /usr/lib/postgresql/17/bin/pg_basebackup -w -d 'application_name=pg_walserver_basebackup host=primary port=5534 user=archiver_repl sslmode=require' --pgdata /var/lib/archiver/bbdemo/basebackups/basebackup-20260928T211513Z -U archiver_repl --verbose --progress --wal-method=stream --checkpoint=fast --label basebackup-20260928T211513Z
  21:15:13 2592535 INFO  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  21:15:13 2592535 INFO  pg_basebackup: checkpoint completed
  21:15:13 2592535 INFO  pg_basebackup: write-ahead log start point: 0/2A000028 on timeline 1
  21:15:13 2592535 INFO  pg_basebackup: starting background WAL receiver
  21:15:13 2592535 INFO  pg_basebackup: created temporary replication slot "pg_basebackup_2592542"
  21:15:13 2592535 INFO  41457/41457 kB (100%), 0/1 tablespace (...260928T211513Z/global/pg_control)
  21:15:13 2592535 INFO  41457/41457 kB (100%), 1/1 tablespace
  21:15:13 2592535 INFO  pg_basebackup: write-ahead log end point: 0/2A000120
  21:15:13 2592535 INFO  pg_basebackup: waiting for background process to finish streaming ...
  21:15:14 2592535 INFO  pg_basebackup: syncing data to disk ...
  21:15:14 2592535 INFO  pg_basebackup: renaming backup_manifest.tmp to backup_manifest
  21:15:14 2592535 INFO  pg_basebackup: base backup completed
  21:15:14 2592535 INFO  Base backup "basebackup-20260928T211513Z" is now the latest for "/var/lib/archiver/bbdemo"
  21:15:14 2592535 INFO  archive-cleanup: --keep-count 1 -- retaining WAL from "00000001000000000000002A" onward
  21:15:14 2592535 ERROR archive-cleanup: WAL continuity check failed for kept backup "/var/lib/archiver/bbdemo/basebackups/basebackup-20260928T211513Z": its own required starting WAL segment "00000001000000000000002A" is missing, and no WAL segment at all is present under "/var/lib/archiver/bbdemo" to compare against
  21:15:14 2592535 FATAL archive-cleanup: refusing to remove anything: one or more kept backups would be left without a complete, gap-free WAL sequence -- see the specific problem(s) logged above. This is a whole-operation refusal, nothing has been deleted. Pass --force only once you have independently verified it is safe to proceed (e.g. an independent backup, or an accepted/expected gap) -- a default, unattended cron job should never blindly pass --force
  21:15:14 2592535 ERROR basebackup: the new base backup succeeded and has been kept, but the retention cleanup pass that followed it did not complete -- see the error(s) logged above

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_archive_cleanup`
