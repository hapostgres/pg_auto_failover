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

Two tracking hooks, for a future layer
----------------------------------------

``pg_receivewal`` streams and writes segments to a local directory
with no hook of its own: nothing else in the same process gets told
when a segment closes, or how far the stream has progressed since.
Today, nothing in this codebase needs that -- ``pg_walserver`` decides
whether a segment is available by looking at the directory itself
(``wal_dir_scan.c``), not by reacting to a notification. The two hooks
below exist for a later layer, not yet built, that will want to track
or register WAL segment completions as they happen instead of
rescanning a directory to find out: a small, deliberate extension
point added while the vendored copy was already being touched for
other reasons, rather than a gap this project urgently needed closed.

The hooks are two callbacks added to the vendored copy's
``stop_streaming()`` callback (already invoked on every check-in,
segment-closing or not):

``pgaf_wal_segment_closed_hook(xlogpos, timeline)``
  Called the moment a WAL segment finishes. ``xlogpos`` is that
  segment's own end boundary.

``pgaf_wal_progress_hook(xlogpos, timeline)``
  Called on every other check-in (far more often than a segment
  actually closes, roughly once per ``--status-interval`` round).
  ``xlogpos`` here is raw stream position, not guaranteed to land on a
  record boundary -- an observability/lag metric only, never a safe
  replay target on its own.

Both run synchronously, on the streaming loop's own thread of control,
so a hook must stay fast and non-blocking, the same rule a real
``archive_command`` script has to follow. Neither hook is set by
anything in this codebase today: both default to ``NULL`` (a no-op),
and the vendored copy runs exactly like real, unmodified
``pg_receivewal`` until something sets one. They exist as an extension
point, wired and ready, not yet consumed.

The only other change from upstream is mechanical: ``main()`` is
renamed to ``pg_receivewal_main()`` and is no longer ``static``/the
process entry point, so it can be called directly, once per
(re)connection cycle, from the forked child that runs it. Everything
else is byte-for-byte unchanged from PostgreSQL's own source.

What starts this service
--------------------------

Today, only :ref:`pg_walserver`'s embedded receivewal worker does: each
route configured with ``receivewal = pull`` (the default; see
:ref:`pg_walserver_setup`'s ``--receivewal`` option) gets a supervised
child running ``pg_walserver internal service pg-receivewal --route
<key> --upstream <conninfo> --path <dir>`` -- a real, separate,
supervised process (restarted on failure the same way any other
supervised service in this project is), just one that calls
``pg_receivewal_main()`` in-process instead of exec'ing a system
``pg_receivewal``. This sub-command is hidden from ``--help``: it is
not meant to be run by hand.
