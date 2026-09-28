.. _pg_walserver_archive_cleanup:

pg_walserver archive-cleanup
=============================

pg_walserver archive-cleanup - Remove WAL/base backups this route no longer needs to keep

Synopsis
--------

::

  pg_walserver archive-cleanup --cluster <name> --pgdata <path> | --path <dir>
      [--keep-count <N>] [--keep-age <interval>] [--dry-run] [--force]

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

Before deleting anything, a pre-flight check verifies that every
backup the retention math decides to keep still has a genuinely
unbroken WAL sequence covering it -- from its own required starting
segment through to either the next newer kept backup's own start, or
the newest WAL segment on disk for the most recent one (legitimate
timeline switches, e.g. after a promotion, are followed through their
own ``.history`` files, not mistaken for a gap). A pre-existing hole
in the archive -- from an outage, a disk issue, anything unrelated to
this tool -- can leave a kept backup unusable for PITR beyond that
point; finding one refuses the *entire* operation by default (nothing
deleted at all, not even the otherwise-safe parts), naming the exact
missing segment. This is deliberate: a cron job wired to this command,
unattended, must never silently let a base backup become unusable, or
silently open a hole in the ability to do point-in-time recovery.
``--force`` bypasses this specific refusal (never the count/age
retention math itself) for an operator who has independently confirmed
proceeding is safe -- a default unattended cron job should never pass
it blindly.

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

  Print what would be removed without removing anything (the
  WAL-continuity check still runs, and still reports any problem it
  finds, even though nothing is ever deleted in dry-run mode either
  way).

--force

  Bypass the WAL-continuity refusal specifically, proceeding with the
  deletion anyway. Never bypassed by ``--dry-run``, and never affects
  the ``--keep-count``/``--keep-age`` retention math itself. A default
  unattended cron job should never pass this blindly.

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

A real gap in the archive -- one WAL segment a kept backup still needs,
missing -- refuses the whole operation rather than deleting around it::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster --keep-count 2
  ERROR archive-cleanup: WAL continuity check failed for kept backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T192713Z"
        (requires WAL from "000000010000000000000006" onward): missing
        WAL segment "000000010000000000000007" (needed between
        "000000010000000000000006" and "000000010000000000000008")

``--force`` proceeds despite it, once an operator has independently
confirmed that's safe::

  archive$ pg_walserver archive-cleanup --path /var/lib/archiver/mycluster --keep-count 2 --force
  ERROR archive-cleanup: WAL continuity check failed for kept backup
        "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T192713Z"
        (requires WAL from "000000010000000000000006" onward): missing
        WAL segment "000000010000000000000007" (needed between
        "000000010000000000000006" and "000000010000000000000008")
  WARN  archive-cleanup: proceeding despite the WAL continuity
        problem(s) above because --force was given

Wired into cron, keeping at least a week of history and at least 3 base
backups::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver archive-cleanup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_basebackup`
