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

Giving pg_receivewal an archive_command
-----------------------------------------

A real ``archive_command`` fires on the primary the instant one WAL
segment is ready, so an archiver can act on it right away.
``pg_receivewal`` has no equivalent: it streams and writes segments to
a local directory with no hook of its own, so the only way to react to
a segment closing is to poll the directory afterward and notice a new
file appeared. That gap is exactly what this project needed to close
to bootstrap a base backup at the right moment.

The fix is two hooks added to the vendored copy's ``stop_streaming()``
callback (already invoked on every check-in, segment-closing or not):

``pgaf_wal_segment_closed_hook(xlogpos, timeline)``
  Called the moment a WAL segment finishes. ``xlogpos`` is that
  segment's own end boundary -- the ``archive_command`` equivalent
  this was added for.

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
and calling the vendored copy without setting one runs exactly like
real ``pg_receivewal`` with no ``archive_command`` equivalent at all.
They exist as an extension point, wired and ready, not yet consumed.

The only other change from upstream is mechanical: ``main()`` is
renamed to ``pg_receivewal_main()`` and is no longer ``static``/the
process entry point, so it can be called directly, once per
(re)connection cycle, from the forked child that runs it. Everything
else is byte-for-byte unchanged from PostgreSQL's own source.

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
