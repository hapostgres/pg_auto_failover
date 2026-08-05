.. _archiving_internals:

Archiving Architecture
=======================

This page is the technical reference for how the :ref:`archiving_architecture`
feature is actually built: the two processes behind every archiver, when and
how ``pg_receivewal`` and ``pg_basebackup`` run, and ``pg_walsender``, the
project's own server-side implementation of enough of the PostgreSQL
replication protocol to serve captured WAL and base backups back out to a
real Postgres instance. It is meant to be the single place to read before
touching any of this code, and the place later milestones (warm standby,
point-in-time recovery, cloud storage push -- see `Extension points for
future milestones`_ below) extend rather than replace.

For the operator-facing view -- registering an archiver, attaching a
base-backup policy, rebuilding a node from one -- see :ref:`archiving_operations`.
For where an archiver fits in the wider fault-tolerance picture, see
:ref:`archiving_fault_tolerance`. For the ``archiving`` state's exact FSM
transitions, see the :ref:`archiving_state` entry in :ref:`failover_state_machine`.

Two processes, one archiver
----------------------------

``pg_autoctl archiver run`` (or ``pg_autoctl node run`` against a
``kind = archiver`` node spec) supervises exactly two long-running child
processes, registered with this project's ordinary ``supervisor.c``
machinery the same way any other node kind's services are:

- **capture** (``service_archiver_loop()``, ``service_archiver.c``) --
  keeps ``pg_receivewal`` running against the group's current primary and
  reports progress to the monitor, the node-active loop every other node
  kind also runs, just with an archiver's own body.
- **serve** (``service_archiver_serve_loop()``, ``service_archiver_serve.c``)
  -- keeps ``pg_walsender`` running and refreshes the small routes file it
  reads its configuration from.

.. figure:: ./tikz/arch-archiver-internals.svg
   :alt: pg_autoctl archiver run forks two processes, capture and serve; capture execs pg_receivewal and writes archiver-position, serve reads archiver-position and writes archiver-routes.ini, then execs pg_walsender, which reads the WAL cache and routes file and serves pg_basebackup, streaming standbys, and restore_command fetches

   Two forked processes per archiver, talking to each other only through
   two small files on disk

Each is its own ``fork()`` of the ``pg_autoctl`` process (no further
``exec()`` at that level -- they keep running as the same binary, in the
same fork-without-exec style ``pg_walsender``'s own accept loop uses one
level down), each with its own independent connection to the monitor,
since a libpq connection is not fork-safe to share. That split is also why
they never touch each other's memory: the only two channels between them
are two small files, both written atomically (write to a ``.tmp`` path,
then ``rename()``) so a concurrent reader never observes a partial write:

``archiver-position``
  A single line holding the archiver's real, currently-captured WAL
  position (see `Tracking the real captured position`_ below), written by
  **capture** on every tick and read by **serve** on every routes-file
  refresh. Lives next to the archiver's own ``pg_autoctl.cfg``
  (``path_in_same_directory(config->pathnames.config, "archiver-position",
  ...)``).

``archiver-routes.ini``
  The file ``pg_walsender`` itself reads its whole configuration from --
  see `Routing and auth: the routes file`_ below. Lives in the same
  directory, written by **serve**.

WAL capture
-----------

Reusing the standby replication slot mechanism
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An archiver's WAL capture rides on a mechanism that already existed for
ordinary standbys, with no primary-side special-casing at all.
``keeper_create_and_drop_replication_slots()`` (``keeper.c``) runs on every
primary-role node, every tick, and calls
``postgres_replication_slot_create_and_drop()`` for every *other* node in
the group returned by ``AutoFailoverOtherNodesList()``
(``src/monitor/node_metadata.c``) -- a list with no ``hasPgData`` filter,
so an archiver's row comes back exactly like a real standby's. The primary
ends up creating and maintaining a slot named
``pgautofailover_standby_<archiver's node id>`` for the archiver the same
way it would for a secondary, using the same
``REPLICATION_SLOT_NAME_DEFAULT`` prefix (``"pgautofailover_standby"``,
``defaults.h``) -- nothing in the primary's own slot-maintenance code
needs to know an archiver exists.

That slot is what makes capture reliable in the first place: a replication
slot pins ``restart_lsn`` at *creation time* and the server retains WAL
back to it regardless of how many times the consumer disconnects and
reconnects. Without one, a ``pg_receivewal`` that loses the startup
HBA-propagation race restarts from the server's then-current position
instead of resuming, silently and permanently skipping whatever WAL
existed in the gap -- a real, observed failure mode this project's own
early testing hit before the slot was wired in.

Starting ``pg_receivewal``
^^^^^^^^^^^^^^^^^^^^^^^^^^^

``service_archiver_start_pgreceivewal()`` (``service_archiver.c``) locates
the real, unmodified ``pg_receivewal`` binary next to ``pg_ctl``
(``path_in_same_directory``), then runs ``fork()``/``execv()`` on it
directly -- a real exec, not a fork-without-exec like the two supervised
services above::

  pg_receivewal -w -d "host=<primary> port=<port> user=streaming_pgautofailover application_name=<name>" \
    -D <archiver's pgdata> --no-sync -S pgautofailover_standby_<node id>

An archiver's own "pgdata" is not a real ``PGDATA`` -- there is no
``initdb``'d cluster underneath it, no Postgres instance ever started
against it. It is repurposed as the archiver's local WAL-cache root:
segments land there directly, complete ones named the usual 24 hex-digit
way, the one currently being written suffixed ``.partial`` (matching real
Postgres's own pre-allocation-to-full-segment-size behavior,
``XLogFileInitInternal``). The same directory later holds a
``basebackups/`` subdirectory once base backups start landing (see
`Base backup generation`_ below).

The child's pid is tracked in a file-scope variable and reaped every tick
via ``waitpid(WNOHANG)``; a dead child is simply restarted on the next
tick capture notices it.

Tracking the real captured position
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``service_archiver_update_current_lsn()`` scans the WAL cache directory
every tick for the newest complete segment and the newest ``.partial``
one. When a ``.partial`` segment is the real frontier (newer than or equal
to the newest complete one), its trailing run of zero bytes -- the
unwritten tail of Postgres's own full-size pre-allocation -- is trimmed
off by reading the whole file and walking backwards from the end, so the
reported position reflects real, captured content, not the segment's
nominal full size. This is what makes an archiving node a real, rankable
candidate in ``pgautofailover.get_most_advanced_standby()`` during a
failover election: that query has no kind-based exclusion, it just needs a
node reporting something other than ``"0/0"``.

That position is computed once, in the **capture** process, and is the
single source of truth for "how far has this archiver actually captured"
-- consumed directly by the node-active report to the monitor, and written
to the ``archiver-position`` file (`Two processes, one archiver`_ above)
for **serve** to pick up, rather than each side independently re-deriving
it by scanning WAL file content on its own.

Continuity across a failover
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

An archiving node's own FSM is deliberately small -- see the :ref:`archiving_state`
state reference entry in :ref:`failover_state_machine` for the exact three
transitions (``wait_standby`` → ``archiving``, ``archiving`` →
``report_lsn``, ``report_lsn`` → ``archiving``). On ``ARCHIVING`` →
``REPORT_LSN`` (the group's primary becomes unreachable and a failover
starts), the current implementation stops ``pg_receivewal`` outright via
``fsm_archiver_report_lsn()`` rather than leaving it running against the
soon-to-be-former primary. Once a new primary is confirmed and the node is
assigned back to ``ARCHIVING``, ``pg_receivewal`` is started again,
pointed at the new primary, with the same slot name (still keyed on this
archiver's own node id, so the primary-side slot survives the handover
untouched).

Base backup generation
-----------------------

The ``basebackup_policy`` table
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Every base backup an archiver produces is scheduled and sourced according
to a policy row in ``pgautofailover.basebackup_policy``
(``src/monitor/pgautofailover.sql``)::

  CREATE TABLE pgautofailover.basebackup_policy (
      basebackuppolicyid   bigserial PRIMARY KEY,
      policyname           text UNIQUE,
      source     pgautofailover.basebackup_source     NOT NULL DEFAULT 'replay',
      replaymode pgautofailover.basebackup_replay_mode DEFAULT 'volatile',
      cache      pgautofailover.basebackup_cache       NOT NULL DEFAULT 'local',
      frequency  interval NOT NULL DEFAULT '24 hours',
      maxcount   int      NOT NULL DEFAULT 3,
      maxage     interval NOT NULL DEFAULT '3 days',
      onpromotion bool    NOT NULL DEFAULT true,
      concurrency int     NOT NULL DEFAULT 1,
      CHECK (source <> 'replay' OR replaymode IS NOT NULL),
      CHECK (concurrency >= 1)
  );

A formation (or a specific group, as an override) attaches a policy via
``pgautofailover.set_archiver_policy()``, the same function both
``pg_autoctl create archiver --basebackup-policy`` and
``pg_autoctl set basebackup-policy`` ultimately call. Resolution is a
three-tier fallback, plpgsql rather than one ``UNION ALL`` query since
branch evaluation order there isn't guaranteed:
``get_archiver_policy(formationid, groupid)`` looks for an exact
``(formation, group)`` override first, then a formation-wide row
(``groupid IS NULL``), then falls back to the schema's own built-in
``'default'`` policy row (nightly-equivalent: 24h frequency, 3 kept, 3
days max age). ``get_basebackup_policy_for_group()`` wraps that with the
join against ``basebackup_policy`` itself, flattening the ``interval``
columns to plain integer seconds (``extract(epoch FROM ...)::int``) so the
C side does cheap ``time_t`` arithmetic instead of parsing intervals.

``concurrency`` is schema-complete and read on the C side, but not yet
enforced -- a single archiver only ever runs one backup job at a time
regardless of its value, a correct simplification for as long as an
archiver belongs to a single ``(formation, group)`` membership (see
`Extension points for future milestones`_).

Scheduling: when ``pg_basebackup`` runs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``service_archiver_maybe_generate_basebackup()``
(``service_archiver_basebackup.c``) runs every tick of the capture loop,
alongside the WAL-report cycle. It resolves the group's policy, lists
existing backups (newest first), and decides:

- **Bootstrap**: a group with zero backups gets one immediately, and it is
  **always** sourced ``live`` regardless of the policy's configured
  ``source`` -- there is nothing to replay from yet.
- **Forced by promotion**: if ``onpromotion`` is set and the group's
  primary has changed since the last tick (tracked via a file-scope
  "last known primary" value, seeded lazily so a process's very first
  tick never misfires), a backup is forced regardless of ``frequency``.
- **Due by frequency**: otherwise, a backup is due once
  ``now - <newest backup's start time> >= policy.frequency``.

Once a backup is due, generation forks a one-shot child (tracked via its
own pid, reaped the same way ``pg_receivewal``'s is) that runs to
completion and exits -- deliberately not exec'd, so it never blocks the
capture loop's own per-tick node-active/WAL-report cycle for however long
the backup takes.

Live source
^^^^^^^^^^^^

``select_basebackup_source()`` picks the first healthy non-primary node in
the group, falling back to the primary if there is none (rows with
``port == 0`` -- the ARCHIVING-row sentinel -- are skipped, an archiver
never backs up from another archiver). ``run_pg_basebackup()`` then runs
the real, unmodified binary::

  pg_basebackup -h <host> -p <port> -U streaming_pgautofailover -D <backup dir> \
    --format=plain --wal-method=none --checkpoint=fast --label <label> --no-password

``--wal-method=none`` is deliberate: consistency for the backup is already
supplied either by the archiver's own already-running ``pg_receivewal``
capture (live mode) or by the replay pipeline's own paused/promoted
staging instance (replay mode below) -- a second, redundant WAL stream
alongside the base backup itself would be wasted work.

Replay / volatile source
^^^^^^^^^^^^^^^^^^^^^^^^^^

When ``source = 'replay'`` and this isn't the bootstrap backup, the
archiver produces its next base backup entirely locally, with no network
round trip to any live node at all:

1. ``copy_directory_tree()`` seeds a throwaway ``<pgdata>/replay-staging``
   directory from the last retained backup.
2. ``write_replay_recovery_config()`` drops an empty ``recovery.signal``
   into it (required so a copied ``backup_label`` triggers archive
   recovery rather than an ordinary crash-recovery restart) and appends to
   ``postgresql.auto.conf``::

     restore_command = 'cp "<walcache dir>/%f" "%p"'
     ssl = off

   There is deliberately no ``recovery_target_lsn``: a complete WAL
   segment's own boundary is not a real record boundary to pause at (it
   would error out, "recovery ended before configured recovery target was
   reached"), so this replays *everything* locally available and lets
   Postgres self-promote once ``restore_command`` runs out of segments to
   fetch. ``ssl = off`` matters because the copied config still carries
   the *source* node's own ``ssl_cert_file``/``ssl_key_file`` settings,
   meaningless here since the archiver has no Postgres SSL certificates of
   its own.
3. ``start_staging_postgres()`` execs the real ``postgres`` binary
   directly against the staging directory, loopback-only, on a fixed port
   (``PG_AUTOCTL_ARCHIVER_REPLAY_PORT``, 6899)::

     postgres -D <staging dir> -p 6899 -h 127.0.0.1

4. ``wait_for_replay_promotion()`` polls ``SELECT pg_is_in_recovery()``
   over a real libpq connection until it returns ``false`` (promoted) or a
   60-second timeout elapses.
5. ``run_pg_basebackup()`` runs again, this time against
   ``127.0.0.1:6899``, producing the actual retained backup.
6. The staging instance is stopped (``SIGTERM`` + ``waitpid``) and
   ``<pgdata>/replay-staging`` is removed unconditionally, success or
   failure -- this is the ``volatile`` contract: nothing survives between
   cycles, and every cycle replays the whole gap since the last retained
   backup again. A ``persistent`` mode that keeps the staging instance
   resident between cycles is a real, designed extension point, gated on
   Milestone 6 (see `Extension points for future milestones`_).

Either path finishes the same way: ``report_basebackup()`` reads
``backup_label`` from the finished directory for the real start
LSN/timeline, reports the backup as started, queries the source's own
current WAL position for the end LSN, then reports it complete with its
on-disk size (``directory_size()``, a best-effort ``nftw()`` walk).

Retention
^^^^^^^^^^

After every successful backup, ``apply_basebackup_retention()`` lists the
group's complete backups newest-first and prunes independently on two
conditions: past ``maxcount`` (index ``>= policy.maxCount`` in the
newest-first list), or past ``maxage`` (``now - startedAt > policy.maxAgeSeconds``).
A pruned backup's directory is removed (``rmtree()``) and its row is
marked deleted (``monitor_report_basebackup_deleted()``), which cascades
on the monitor side to prune any WAL segments no remaining backup still
needs. Retention is best-effort: one failure to remove a given backup
doesn't stop the rest from being evaluated.

``pg_walsender``: serving archived data
------------------------------------------

Why a custom implementation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

No frontend-linkable server-side replication-protocol library exists
anywhere in PostgreSQL -- the real ``walsender.c`` and its supporting code
are backend-only, tied to shared memory, ``palloc``, and ``ereport``.
``pg_walsender`` is a genuine, from-scratch reimplementation of just
enough of that protocol to serve a real ``pg_basebackup``, a real
streaming standby, or this project's own ``restore_command`` fetch client
-- built against the real Postgres source as a design reference (same
PostgreSQL License), not linked against it. It is a fully standalone
binary: its own ``Makefile`` links only ``src/bin/common/`` and the
project's own logging library, explicitly **not** any ``pg_autoctl/*.c``
-- the same shape as ``pgaftest``'s own binary.

The one piece of real Postgres source actually vendored in is
``vendor/tar.c`` (from ``src/port/tar.c``, for ustar header/checksum
construction used by ``BASE_BACKUP``'s tar streaming) -- not
``xlogreader.c``: ``START_REPLICATION`` streams raw WAL bytes read
straight from files under the WAL cache directory, the same way real
``walsender.c``'s own ``WalSndSegmentOpen`` just computes a path from
timeline and segment number and opens it. Streaming already-captured WAL
verbatim needs no WAL *record* decoding at all, so there was never a
reason to vendor a WAL record reader for this.

Process model
^^^^^^^^^^^^^^

``pg_walsender``'s own accept loop (``accept_loop.c``) is a plain
``socket()``/``bind()``/``listen()`` server on the configured port,
``SIGCHLD`` ignored for auto-reaping, looping on ``select()`` with a
short timeout so shutdown signals are never blocked on a pending
``accept()``. Each accepted connection is handled by a **forked, not
exec'd** child -- matching real Postgres's own ``BackendMain()`` model --
which runs startup negotiation, routes-based auth, then a simple-query
command loop until the client disconnects.

Routing and auth: the routes file
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``pg_walsender`` never talks to the monitor directly and carries no
``PGDATA`` of its own to keep a real ``pg_hba.conf`` in. Instead, the
**serve** process periodically (every 30 ticks, roughly 30 seconds, plus
immediately on ``SIGHUP`` or at startup) regenerates a small routes file,
one INI section per ``(formation, group)``, which ``pg_walsender``
re-reads on every new connection::

  [default/0]
  walcache = /var/lib/pgaf/archiver1
  basebackup = /var/lib/pgaf/archiver1/basebackups/basebackup-20260804T230000Z
  position = 0/3000148
  systemid = 7234659871234567890
  timeline = 1

``walcache``/``basebackup`` come from local config and the monitor's own
``get_latest_basebackup_info(preferredSource="live")`` -- only ``live``
backups are advertised this way, since a ``replay`` backup can land on a
later timeline than what's in the WAL cache, which a real
``pg_basebackup``'s background WAL-streaming thread would reject.
``position`` is the same canonical value `Tracking the real captured
position`_ above computes, read back from the ``archiver-position`` file
-- the reason ``BASE_BACKUP``'s own end-of-backup position (below) is
correct rather than a stale re-send of the start position. ``systemid``
and ``timeline`` come from the monitor. An ``allowed_hosts`` key is fully
parsed and enforced by the auth path (comma-separated hosts, checked
against the connecting peer's address) but is not yet populated by the
writer, so routes are unrestricted by host in this milestone.

Authentication is trust-equivalent, matching this project's existing
convention of installing ``host(ssl) replication ... trust`` rules into
every real node's own ``pg_hba.conf``: the startup packet's ``user`` must
equal ``streaming_pgautofailover`` (``PG_AUTOCTL_REPLICA_USERNAME``), and
if the resolved route carries an ``allowed_hosts`` list, the peer address
must match it. There is no password or SCRAM support in this milestone --
nothing in the project has needed it yet.

Wire commands
^^^^^^^^^^^^^^

Commands are parsed from the simple-query text
(``src/bin/pg_walsender/repl_command.c``) and dispatched to one file each:

.. list-table::
   :header-rows: 1
   :widths: 28 20 52

   * - Command
     - File
     - What it does
   * - ``IDENTIFY_SYSTEM``
     - ``cmd_identify_system.c``
     - Returns ``(systemid, timeline, xlogpos, dbname)`` from the route,
       falling back to scanning the WAL cache for ``xlogpos`` if the route
       doesn't carry a position yet.
   * - ``SHOW <name>``
     - ``cmd_show.c``
     - Fixed answers for ``wal_segment_size`` and ``data_directory_mode``;
       anything else is a clean error.
   * - ``BASE_BACKUP [options]``
     - ``cmd_base_backup.c``
     - A full plain-format streaming base backup over the real
       ``pg_basebackup`` wire sequence (``tar_stream.c`` streams
       ``basebackup`` as a single-file tar). ``WAL``, ``MANIFEST``,
       ``COMPRESSION``, and non-``client`` ``TARGET`` are rejected as
       unsupported. The end-of-backup position sent at the end is the
       route's own ``position`` when present, falling back to an
       independently re-derived one (``.partial``-aware) only if it's
       missing -- see `Routing and auth: the routes file`_ above for why
       this matters.
   * - ``TIMELINE_HISTORY <tli>``
     - ``cmd_timeline_history.c``
     - Reads ``<walcache>/<tli>.history`` and returns its content; a
       missing file is a hard error, matching real ``walsender.c``.
   * - ``CREATE_REPLICATION_SLOT <name> ... PHYSICAL``
     - ``cmd_replication_slot.c``
     - Physical slots only. Writes a small marker file recording
       ``restart_lsn``; no retention is enforced against it yet.
   * - ``READ_REPLICATION_SLOT <name>``
     - ``cmd_replication_slot.c``
     - Reads that marker file back; a missing slot is one all-NULL row,
       matching real Postgres rather than erroring.
   * - ``START_REPLICATION [SLOT <name>] PHYSICAL <lsn> [TIMELINE <tli>]``
     - ``cmd_start_replication.c``
     - Streams raw WAL bytes from the WAL cache as ``CopyData`` messages
       with periodic keepalives. Distinguishes "not captured *yet*" (wait
       and poll) from "predates everything this archiver ever captured"
       (a hard, immediate error) -- see :ref:`archiving_fault_tolerance`
       and this file's own history for why that distinction exists: a
       real, permanent gap this project's testing hit before the
       replication-slot fix in `Reusing the standby replication slot
       mechanism`_ made it unreachable in practice.
   * - ``fetch/<key>`` (dbname prefix, not a ``repl_command``)
     - ``cmd_fetch_file.c``
     - A separate one-shot side channel for ``restore_command``: after
       auth, the client sends one filename line, the server streams that
       file's raw bytes back as a single ``CopyData`` message, then
       closes. Reuses the same startup/auth machinery as the normal
       command loop, just routed differently by ``dbname``.

How real consumers connect
----------------------------

Bootstrapping a node from an archiver
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``pg_autoctl create postgres --from-archiver`` sets
``KeeperConfig.fromArchiver``, which ``fsm_init_standby()`` checks before
picking a source: instead of the group's live primary, it resolves the
group's archiver via the monitor (``keeper_get_archiver_node()``) and uses
it as the ``pg_basebackup``/streaming source, with an empty slot name --
``pg_walsender``'s ``CREATE_REPLICATION_SLOT`` has no real retention
enforcement yet (`Wire commands`_ above), so there is nothing for the
ordinary standby-init path to verify a slot against. Past that one lookup,
initialization runs completely unmodified: ``pg_walsender`` speaks enough
of the real protocol that a genuine ``pg_basebackup``/streaming standby
needs no archiver-aware code at all beyond finding the archiver in the
first place.

The monitor never stores an archiver's real serve port -- that's
host-local information, same reasoning as the routes file living on disk
rather than in the database -- so the lookup resolves the archiver's
advertised ``port == 0`` sentinel to the well-known
``PG_AUTOCTL_ARCHIVER_SERVE_PORT`` (6543) instead.

``FAST_FORWARD`` from an archiver
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The same ``port == 0`` resolution trick is applied in
``keeper_get_most_advanced_standby()``: when the monitor's own
``get_most_advanced_standby()`` returns an ARCHIVING row as the most
advanced source during a multi-standby election, this function resolves
it to the archiver's real serve port before handing it back as the
upstream to catch up from. From there, ``standby_fetch_missing_wal()`` --
the same ``FAST_FORWARD`` machinery every node kind already uses -- opens
an ordinary streaming connection (``primary_conninfo``,
``START_REPLICATION``) against that host:port, with no archiver-specific
code in that path at all. This is the concrete mechanism behind the claim
in :ref:`archiving_fault_tolerance` that an archiver's own captured WAL
becomes "a real, Fast_forward-eligible source for whichever candidate did
win."

Build and process wiring
--------------------------

``pg_walsender`` builds as its own binary next to ``pg_autoctl`` (own
``Makefile``, own ``main.c`` supplying the small set of global stubs
shared ``common/`` code expects, the same pattern ``pgaftest`` uses).
``service_archiver_serve_start_walsender()`` locates it via
``path_in_same_directory(pg_autoctl_program, "pg_walsender", ...)`` and
runs ``fork()``/``execv()`` on it::

  pg_walsender --port 6543 --routes <path to archiver-routes.ini>

``service_archiver_serve_loop()`` supervises that child the same way
capture supervises ``pg_receivewal`` -- restarting it if it's found dead
on a tick -- while also owning the periodic routes-file refresh described
in `Routing and auth: the routes file`_ above.

Extension points for future milestones
------------------------------------------

The design this page describes was built with specific room left for the
milestones that come after it:

- **Warm standby** (``replay``/``persistent`` mode): `Replay / volatile
  source`_ above already has the shape a persistent mode needs --
  everything through step 4 (promote a staging instance from locally
  captured WAL) is identical; a persistent mode would skip steps 5-6
  (backup-then-discard) and instead keep that instance resident as a
  ``cadence='scheduled'``-or-``'continuous'`` warm standby, tracked in a
  new ``archiver_node`` table this milestone doesn't have yet.
- **Point-in-time recovery**: needs a real disaster-recovery test end to
  end (drop data, recover via PITR, verify), plus its own
  ``archiver_node(kind='pitr')``, ``pitr_history``, and
  ``pitr_pending_command`` schema and CLI. Nothing in the current code
  blocks this, but nothing implements it yet either.
- **Cloud storage push**: the schema already has unused ``rclone_config``,
  ``archiver_storage``, and ``basebackup_storage`` tables/columns; no
  ``rclone`` invocation, ``--rclone-config`` CLI flag, or storage-sync
  logic exists yet.
- **``concurrency`` enforcement**: currently read but not enforced (see
  `The basebackup_policy table`_ above) -- meaningful once an archiver can
  hold more than one ``(formation, group)`` membership at a time, at which
  point ``service_archiver.c``/``service_archiver_basebackup.c``'s
  current single-membership-scoped, file-scope-global process tracking
  (one ``pg_receivewal`` pid, one backup-job pid) needs to grow into a
  per-membership map, and ``service_archiver_serve.c``'s one-``pg_walsender``-
  per-archiver-process model needs to grow multiple routes sections
  correctly, which the routes file format already supports.
- **``allowed_hosts`` enforcement**: fully implemented on the read side
  (`Routing and auth: the routes file`_ above); a real host-restriction
  feature needs only a writer-side change in
  ``service_archiver_serve_refresh_routes()``.
