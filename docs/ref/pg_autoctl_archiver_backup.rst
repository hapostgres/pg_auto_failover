.. _pg_autoctl_archiver_backup:

pg_autoctl archiver backup
==========================

pg_autoctl archiver backup - Control base backup generation of a local archiver

pg_autoctl archiver backup now
------------------------------

Request an immediate base backup from a locally running archiver.

::

  usage: pg_autoctl archiver backup now  [ --pgdata ] [ --formation [ --group ] ]

    --pgdata     path to the archiver's local data/cache directory
    --formation  only request for this formation (default: all)
    --group      only request for this group (needs --formation)

The command atomically writes a trigger file named ``archiver-backup-now``
in the archiver's per-membership directory
(``<pgdata>/<formation>/<group>``). The archiver's base backup service
consumes it on its next tick: the policy frequency is bypassed for that
membership, the backup is generated following the usual source and
concurrency rules, and the trigger file is then deleted. If the archiver
is not running, the request stays pending until it starts.

The command only touches local files and never contacts the monitor, so it
**must be run on the archiver's own host**.

Options
-------

--pgdata

  Path to the archiver's local cache directory. Defaults to the
  environment variable ``PGDATA``.

--formation

  Restrict the request to this formation. By default every membership
  found under the archiver directory is targeted.

--group

  Restrict the request to this group of ``--formation``.
