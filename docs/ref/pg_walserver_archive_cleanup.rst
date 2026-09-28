.. _pg_walserver_archive_cleanup:

pg_walserver archive-cleanup
=============================

pg_walserver archive-cleanup - Remove WAL/base backups this route no longer needs to keep

Synopsis
--------

::

  pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path <dir>
      [--keep-count <N>] [--keep-age <interval>] [--dry-run]

Removes WAL segments (and ``.partial``/``.backup`` files) and base
backups a route no longer needs to keep. Retention is infinite by
default: at least one of ``--keep-count``/``--keep-age`` is required,
and ``archive-cleanup`` is never run automatically by ``pg_walserver``
itself -- an operator wires it into cron, the same way
``pg_archivecleanup`` itself is normally wired into
``archive_cleanup_command`` or a cron job, never run on its own. When
both flags are given, whichever keeps more wins: a backup or WAL
segment is removed only once both constraints independently agree it
may go. The backup ``basebackups/.latest`` points to, and every WAL
segment it requires, are never removed.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  The cluster name to clean up, looked up in ``pg_walserver.ini``.

--path

  The route's own directory. Overrides the route's own ``path``
  property.

--keep-count

  Keep at least this many of the most recent base backups.

--keep-age

  Keep anything from the last ``<N><unit>``: ``h`` (hours), ``d``
  (days), ``w`` (weeks), or ``m`` (calendar months, real calendar
  arithmetic, not a 30-day approximation). The suffix is required; a
  bare number is rejected.

--dry-run, -n

  Print what would be removed without removing anything.

Examples
--------

A dry run against a route with two base backups on disk, keeping only
the most recent one::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster \
      --keep-count 1 --dry-run
  INFO  archive-cleanup: --keep-count 1 -- retaining WAL from
        "000000010000000000000004" onward
  INFO  archive-cleanup: [dry run] would remove backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T134117Z":
        past the --keep-count cutoff
  INFO  archive-cleanup: [dry run] would remove
        "/var/lib/archiver/mycluster/000000010000000000000003": older
        than the retention cutoff ("000000010000000000000004")
  INFO  archive-cleanup: [dry run] would remove
        "/var/lib/archiver/mycluster/000000010000000000000002": older
        than the retention cutoff ("000000010000000000000004")
  INFO  archive-cleanup: [dry run] would remove
        "/var/lib/archiver/mycluster/000000010000000000000001": older
        than the retention cutoff ("000000010000000000000004")

The same command, without ``--dry-run``, actually removing them::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster --keep-count 1
  INFO  archive-cleanup: --keep-count 1 -- retaining WAL from
        "000000010000000000000004" onward
  INFO  archive-cleanup: removing backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T134117Z":
        past the --keep-count cutoff
  INFO  archive-cleanup: removing
        "/var/lib/archiver/mycluster/000000010000000000000003": older
        than the retention cutoff ("000000010000000000000004")
  INFO  archive-cleanup: removing
        "/var/lib/archiver/mycluster/000000010000000000000002": older
        than the retention cutoff ("000000010000000000000004")
  INFO  archive-cleanup: removing
        "/var/lib/archiver/mycluster/000000010000000000000001": older
        than the retention cutoff ("000000010000000000000004")

Running it with neither flag is refused outright, rather than deleting
everything::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster
  ERROR archive-cleanup requires --keep-count and/or --keep-age --
        retention is infinite by default, and running with neither
        would mean "delete everything", which this tool refuses to do
        implicitly

A malformed ``--keep-age`` is rejected the same way::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster --keep-age 7
  ERROR Invalid --keep-age value "7": expected a number followed by one
        of "h" (hours), "d" (days), "w" (weeks), or "m" (calendar
        months), e.g. "72h", "30d", "4w", "3m" -- an explicit suffix is
        required, there is no bare-number default

Wired into cron, keeping at least a week of history and at least 3 base
backups::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver archive-cleanup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_basebackup`
