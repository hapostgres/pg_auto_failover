.. _archiving_architecture:

Archiving in Detail
=====================

:ref:`archiving_and_disaster_recovery` introduces the archiver at a glance,
and :ref:`archiving_operations` walks through the day-to-day commands for
registering one and attaching a base-backup policy. This page goes one
level deeper: what actually moves over the network and onto disk while an
archiver is running, and what processes are involved -- the level of
detail worth having before sizing storage, deciding where an archiver
should sit on your network, or reasoning about how much of a given
topology (a Citus formation, several independent formations) is actually
covered.

Data flow
---------

An archiver does three things, and none of them ever route through the
monitor -- WAL and base backups always flow directly between the archiver
and whichever node it's talking to, with the monitor only ever seeing
small status reports (what's been captured, what's been backed up, how
much disk is left), never the data itself:

1. **It streams WAL continuously** from whichever node is currently the
   group's primary, over an ordinary PostgreSQL physical replication
   connection -- the same kind of connection a standby uses, protected by
   its own dedicated replication slot so that nothing already captured is
   ever lost, even across a connection that drops and stays down for a
   while. If the primary changes, the archiver notices and reconnects to
   the new one on its own; no operator action is needed.
2. **It produces base backups on a schedule**, either as a real
   ``pg_basebackup`` taken directly from a live node, or entirely on its
   own: replaying already-captured WAL against a local copy of the last
   base backup until that copy reaches a consistent, promotable state,
   and backing up that instead. The second mode never touches the
   primary or any standby at all -- useful when you want frequent base
   backups without adding load to production.
3. **It hands both back out** on request: a real ``pg_basebackup``
   command, a real standby's own ``primary_conninfo``, or this project's
   own restore tooling can all connect to an archiver directly and get
   what they ask for, with no special client needed -- see `What you can
   point at an archiver`_ below.

Storage
-------

Everything an archiver holds lives under one local directory -- the path
given as ``--pgdata`` when the archiver was created. Despite the flag's
name, this is never a real Postgres data directory (there is no
``initdb``, nothing ever starts Postgres against it directly); it's a
cache root::

  /var/lib/pgaf/archiver1/
  ├── 000000010000000000000041
  ├── 000000010000000000000042
  ├── 000000010000000000000043.partial
  ├── archiver-position
  ├── archiver-routes.ini
  └── basebackups/
      ├── basebackup-20260803T020000Z/
      ├── basebackup-20260804T020000Z/
      └── basebackup-20260805T020000Z/

- WAL segments sit directly in this directory, named exactly the way
  Postgres itself names them. The most recently-started one carries a
  ``.partial`` suffix until it's complete -- archiving doesn't wait for a
  segment to fill up before it counts: whatever has already been flushed
  into that ``.partial`` file is captured too.
- Each retained base backup is its own subdirectory under
  ``basebackups/``, in the same layout an ordinary ``pg_basebackup`` run
  by hand would produce. You could point ``postgres -D`` straight at one
  of them and it would start -- that's exactly what disaster recovery
  relies on.
- ``archiver-position`` and ``archiver-routes.ini`` are small internal
  bookkeeping files: coordinates and status, never a copy of any actual
  data. Safe to ignore day to day, and not something that needs backing
  up itself -- both are regenerated automatically on the archiver's own
  next tick.

Sizing disk for an archiver comes down to two mostly-independent numbers:

- **Base backups**: roughly the policy's ``maxcount`` times the size of
  one backup, since retention prunes anything beyond that count (or
  older than ``maxage``, whichever comes first) right after each new one
  lands. See :ref:`archiving_operations` for how to set these.
- **WAL**: however much WAL has accumulated since your *oldest
  still-retained* base backup -- once a base backup is pruned, the WAL
  segments only it still needed are pruned right along with it. A longer
  retention window keeps more history recoverable, at the cost of more
  WAL kept around to cover it.

Network exposure
-----------------

An archiver listens on a TCP port (``6543`` by default) speaking a subset
of the PostgreSQL replication protocol, authenticated the same trust-based
way every node's own replication connections already are in a
pg_auto_failover cluster (there is no password or TLS on this connection
in the current release). Treat it the same way you'd treat any other
node's own replication port: reachable from wherever you expect to run
``pg_basebackup``, point a standby's ``primary_conninfo`` at it, or run a
restore from, and firewalled off from everywhere else.

Process model
--------------

Once started (``pg_autoctl archiver run``, or ``pg_autoctl node run``
against a ``kind = archiver`` node specification), an archiver supervises
exactly two long-running processes, which in turn each run one more of
their own -- four processes total, all on the one host, none of them
sharing memory. They hand off exactly two small files (`Storage`_ above)
and nothing else:

.. figure:: ./tikz/arch-archiver-internals.svg
   :alt: pg_autoctl archiver run supervises two processes, capture and serve; capture runs pg_receivewal and writes archiver-position, serve reads archiver-position and writes archiver-routes.ini, then runs pg_walsender, which reads the WAL cache and routes file and serves pg_basebackup, streaming standbys, and restore_command fetches

   Two supervised processes per archiver, talking to each other only
   through two small files on disk

::

  pg_autoctl archiver run
  ├── capture   -- keeps WAL streaming alive, reports progress to the monitor
  │   └── pg_receivewal
  └── serve     -- keeps the archiver reachable over the network
      └── pg_walsender --port 6543 --routes archiver-routes.ini

If either child stops unexpectedly, the parent notices on its next tick
and restarts it -- an archiver recovering from a crashed
``pg_receivewal`` or ``pg_walsender`` needs no operator action, the same
way a keeper recovers a crashed Postgres.

More or fewer standby nodes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This process tree doesn't change shape based on how many standby nodes
are in the group. WAL capture always talks to whichever node is currently
primary, never to a standby directly, so a two-node group and a
five-node group look identical from the archiver's side. The only place
standby count matters at all is when a base backup is sourced live: with
more healthy standbys available, there are more candidates to pick from
before falling back to the primary -- everything else about the archiver
is unaffected.

Several formations
^^^^^^^^^^^^^^^^^^^

An archiver is attached to one formation at a time. Covering several
formations -- each, say, with its own group of two or three standby
nodes -- means registering one archiver per formation, each with its own
``--pgdata`` directory and its own identity, whether that's several
archiver processes on one host or spread across several hosts:

::

  host archiver-a                      host archiver-b
  (attached to formation "default")    (attached to formation "billing")

  pg_autoctl archiver run              pg_autoctl archiver run
  ├── capture -> pg_receivewal         ├── capture -> pg_receivewal
  └── serve   -> pg_walsender          └── serve   -> pg_walsender

Each of these process trees is entirely independent -- separate storage
directory, separate WAL stream, separate base-backup schedule, no shared
state of any kind. Losing one has no effect on the others.

A Citus formation
^^^^^^^^^^^^^^^^^^

A Citus formation is really several node groups under one name: the
coordinator's own group, plus one group per worker. Today, registering an
archiver against a Citus formation covers the coordinator's group only --
worker groups don't yet get their own WAL capture or base backups from
that same archiver. If disaster recovery coverage for worker data matters
to you today, plan around this limitation; formation-wide coverage across
every worker group from a single archiver is on the roadmap but not yet
available.

What you can point at an archiver
------------------------------------

An archiver's serving side understands enough of the real PostgreSQL
replication protocol that ordinary, unmodified tools can talk to it
directly -- nothing here needs a custom client. The commands below are
what those tools actually send; useful to know if you're connecting by
hand with ``psql "... replication=database"`` to check on an archiver, or
deciding what else could talk to one.

``IDENTIFY_SYSTEM``

  The first thing any of these tools asks: which system and timeline the
  archiver is tracking, and how far it's captured so far.

``BASE_BACKUP``

  Streams the archiver's most recent base backup, in the same plain tar
  format a real ``pg_basebackup --format=plain`` produces. Point a real,
  unmodified ``pg_basebackup`` at an archiver and it works exactly as it
  would against a live node -- this is what ``pg_autoctl create postgres
  --from-archiver`` uses to bootstrap a brand new node straight from an
  archiver's cache instead of a live primary or secondary.

``START_REPLICATION``

  Streams WAL from a given position onward, the same way a live primary
  would. This is what lets a real standby's own ``primary_conninfo``
  point at an archiver instead of a live node, and what a multi-standby
  failover election falls back on to fetch WAL a promoted candidate is
  still missing, straight from the archiver's own cache, when no live
  node has it anymore.

``TIMELINE_HISTORY``

  Returns the timeline history for a given timeline -- needed by any
  streaming client following a timeline change, such as after a
  failover.

``CREATE_REPLICATION_SLOT`` / ``READ_REPLICATION_SLOT``

  Basic physical replication slot support, for tools that expect to
  manage their own slot against whatever they're streaming from.

Fetching a single WAL file

  A small side channel used by this project's own ``restore_command``
  tooling: ask for one file by name, get its exact bytes back. This is
  what makes an archiver usable as a ``restore_command`` target on its
  own, without needing a full streaming connection just to recover one
  missing segment.

See also
--------

- :ref:`archiving_and_disaster_recovery` -- what an archiver is and
  where it fits among the other architectures
- :ref:`archiving_operations` -- registering an archiver, attaching a
  base-backup policy, rebuilding a node from one
- :ref:`archiving_fault_tolerance` -- what changes about fault tolerance
  once an archiver is in the picture
- :ref:`failover_state_machine` -- the ``archiving`` state's own
  transitions
