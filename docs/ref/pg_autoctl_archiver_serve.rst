.. _pg_autoctl_archiver_serve:

pg_autoctl archiver serve
===========================

pg_autoctl archiver serve - Start serving this archiver's captured WAL and base backups

Synopsis
--------

The command ``pg_autoctl archiver serve`` exec's and supervises ``pg_
walsender``, the archiver's own server-side implementation of a subset of
the PostgreSQL replication protocol, so that ``pg_basebackup``, a real
standby's ``primary_conninfo``, and ``restore_command`` fetches can all
reach this archiver's captured WAL and base backups directly. See
:ref:`archiving_architecture` for what it serves and how.

::

  usage: pg_autoctl archiver serve  [ --pgdata --port ]

    --pgdata   path to the archiver's local data/cache directory
    --port     port for pg_walsender to listen on (default: 6543)

Description
-----------

This is a narrower command than the normal way an archiver is run: it
starts only the serving half (``pg_walsender``, and this process's own
supervision of its liveness), not the WAL-capture side (the
``reconciler`` and its per-membership ``capture`` children). The normal,
complete way to run an archiver is ``pg_autoctl run`` (or ``pg_autoctl
node run`` against a ``kind = archiver`` node specification, or
``pg_autoctl create archiver --run``) -- see :ref:`pg_autoctl_run` and
:ref:`archiving_architecture`'s own "Process model" section for what that
starts. ``pg_autoctl archiver serve`` exists mainly for testing or
splitting the serving side onto its own process independently of that
normal supervision tree.

This command never connects to the monitor: everything ``pg_walsender``
needs (which base backup is current, this group's system identifier, the
current WAL position, and the formation/group-to-storage-path mapping)
is read straight from local files this archiver's own capture and
reconciler processes maintain -- see :ref:`archiving_architecture`'s
"Keeping local files current" section. This command keeps serving
already-captured data through a monitor outage with nothing in its own
startup or steady-state operation depending on the monitor being
reachable.

Options
-------

The following options are available to ``pg_autoctl archiver serve``:

--pgdata

  Path to the archiver's local cache directory for captured WAL segments
  and base backups -- the same directory given as ``--pgdata`` to
  ``pg_autoctl create archiver``. Defaults to the environment variable
  ``PGDATA``.

--port

  Port for ``pg_walsender`` to listen on. Defaults to ``6543``.

See Also
--------

:ref:`pg_autoctl_run` is the normal way to run a complete archiver
(capture and serve together).

:ref:`archiving_architecture` covers the full process model and what
``pg_walsender`` actually serves.
