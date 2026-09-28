.. _pg_receivewal:

pg_receivewal
=============

pg_receivewal - not a command this project installs

There is no ``pg_receivewal`` binary on this project's own ``PATH``.
PostgreSQL's own ``pg_receivewal`` is documented upstream:

  https://www.postgresql.org/docs/current/app-pgreceivewal.html

What this project ships instead, under
``src/bin/common/vendor/pg_receivewal/``, is that same tool's source
vendored in and built directly into ``pg_walserver`` itself, called
in-process rather than exec'd as a separate program.

Why an in-process copy
-----------------------

A separately-installed ``pg_receivewal`` would have to be found on
``PATH``, could drift out of sync with the PostgreSQL version this
project was built against, and could only be supervised as an opaque
child process -- there would be no way to act the moment it closes a
WAL segment except by polling the directory it writes to afterward.
Vendoring the source and calling its ``main()`` directly, in-process,
removes all three problems: no external binary to find, always the
exact version this project was built against, and a real extension
point (below) at the moment a segment actually closes.

The vendored copy is pinned to PostgreSQL 14's source
(``REL_14_STABLE``), not the version currently being targeted:
``REL_17_STABLE``'s own ``walmethods.h`` pulls in ``common/compression.h``,
which does not exist in PG14's header tree, and this project rebuilds
against PG14 through PG19's own headers alike. PG14's source, from
before that refactor, compiles against all of them; the replication
protocol itself is unaffected by it, so this still talks to a
PG17/18/19 primary exactly the way a real, unmodified ``pg_receivewal``
built from any of those versions would.

Two changes from upstream
--------------------------

Everything else is byte-for-byte unchanged from PostgreSQL's own
source; two changes are marked ``PGAF:`` at their exact location:

1. ``main()`` is renamed to ``pg_receivewal_main()`` and is no longer
   ``static``/the process entry point -- called directly, once per
   (re)connection cycle, from the forked child that runs it.

2. ``stop_streaming()`` (the ``StreamCtl.stream_stop`` callback,
   already invoked on every check-in, segment-closing or not) now also
   calls one of two hooks:

   ``pgaf_wal_segment_closed_hook(xlogpos, timeline)``
     Called the moment a WAL segment finishes. ``xlogpos`` is that
     segment's own end boundary -- the "archive_command, but a
     function call" extension point this was vendored for.

   ``pgaf_wal_progress_hook(xlogpos, timeline)``
     Called on every other check-in (far more often than a segment
     actually closes, roughly once per ``--status-interval`` round).
     ``xlogpos`` here is raw stream position, not guaranteed to land on
     a record boundary -- an observability/lag metric only, never a
     safe replay target on its own.

   Both run synchronously, on the streaming loop's own thread of
   control, so a hook must stay fast and non-blocking, the same rule a
   real ``archive_command`` script has to follow. Neither hook is set
   by anything in this codebase today: both default to ``NULL`` (a
   no-op), and calling the vendored copy without setting one runs
   exactly like real ``pg_receivewal`` with no ``archive_command``
   equivalent at all. They exist as an extension point, wired and
   ready, not yet consumed.

What starts this service
--------------------------

Today, only :ref:`pg_walserver`'s embedded pull capturer does: each
route configured with ``capture = pull`` (the default; see
:ref:`pg_walserver_setup`'s ``--capture`` option) gets a supervised
child running ``pg_walserver internal service pg-receivewal --route
<key> --upstream <conninfo> --path <dir>`` -- a real, separate,
supervised process (restarted on failure the same way any other
supervised service in this project is), just one that calls
``pg_receivewal_main()`` in-process instead of exec'ing a system
``pg_receivewal``. This sub-command is hidden from ``--help``: it is
not meant to be run by hand.
