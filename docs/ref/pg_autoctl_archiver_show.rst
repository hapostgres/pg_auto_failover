.. _pg_autoctl_archiver_show:

pg_autoctl archiver show
==========================

pg_autoctl archiver show - Show base backups, captured WAL, and state for an archiver

Synopsis
--------

``pg_autoctl archiver show`` exposes what an archiver has captured and
who it is, as plain one-shot listings. ``basebackup`` and ``wal`` are
monitor-side, per-(formation, group) views, same self-contained
``--monitor``-only shape as :ref:`pg_autoctl_archiver_formation`.
``state`` is local-node-aware instead, reading ``--pgdata`` the same way
``pg_autoctl archiver serve`` does.

::

  usage: pg_autoctl archiver show basebackup  --monitor --formation --group [ --json ]

    --monitor    monitor uri to connect to
    --formation  formation to list base backups for
    --group      group to list base backups for
    --json       output data in the JSON format

  usage: pg_autoctl archiver show wal  --monitor --formation --group [ --json ]

    --monitor    monitor uri to connect to
    --formation  formation to list captured WAL for
    --group      group to list captured WAL for
    --json       output data in the JSON format

  usage: pg_autoctl archiver show state  [ --pgdata ] [ --json ]

    --pgdata  path to the archiver's local data/cache directory
    --json    output data in the JSON format

Description
-----------

``basebackup`` lists every complete base backup recorded for a
(formation, group), newest first: when it started, its label, and its
storage location -- the same inventory ``service_archiver_basebackup.c``'s
own retention pass already walks internally to decide what
``maxcount``/``maxage`` keeps.

``wal`` lists every captured WAL segment for a (formation, group), newest
first. A segment landing on more than one archiver (``archiver_quorum >
1``) is shown as one row, with how many and which archivers hold it,
rather than a duplicate row per archiver -- the question this answers is
"is this segment safe", not "what does each archiver individually have".

``state`` prints this archiver's own identity (name, hostname, archiver
id) and every formation/group membership it currently holds, with each
membership's reported and goal FSM state -- the same call
(``pgautofailover.list_archiver_memberships()``) the archiver's own
reconciler makes at startup and periodically thereafter to discover what
it's responsible for. This is the archiver-specific counterpart to
:ref:`pg_autoctl_show_state`, and in fact *is* that command when run
against an archiver's own configuration file: ``pg_autoctl show state``
detects an archiver's ``--pgdata`` and renders this exact view instead of
its usual single-formation/group node table, which doesn't apply to an
archiver (it can be attached to more than one formation at once).

Options
-------

--monitor

  PostgreSQL URI used to connect to the monitor (``basebackup``/``wal``
  only).

--pgdata

  Path to the archiver's local cache directory (``state`` only) -- the
  same directory given as ``--pgdata`` to ``pg_autoctl create archiver``.
  Defaults to the environment variable ``PGDATA``.

--formation

  Formation to list base backups or captured WAL for.

--group

  Group, within ``--formation``, to list base backups or captured WAL
  for.

--json

  Output data in the JSON format.

See Also
--------

:ref:`pg_autoctl_archiver_formation` attaches or detaches an archiver
from a formation.

:ref:`pg_autoctl_show_state` is the ordinary-node counterpart this
command's ``state`` subcommand is aliased from.
