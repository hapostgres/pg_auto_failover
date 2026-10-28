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

Two tracking hooks, feeding the live LSN shown in ``ps``/``status``
-------------------------------------------------------------------

``pg_receivewal`` streams and writes segments to a local directory
with no hook of its own: nothing else in the same process gets told
when a segment closes, or how far the stream has progressed since.
The vendored copy adds two callbacks to its own ``stop_streaming()``
(already invoked on every check-in, segment-closing or not), and
``pg_walserver`` wires both of them, in ``cli_internal.c``, into a
single, throttled (roughly once a second) write of a small
``<cluster path>/receivewal-progress`` file:

``pgaf_wal_segment_closed_hook(xlogpos, timeline)``
  Called the moment a WAL segment finishes. ``xlogpos`` is that
  segment's own end boundary.

``pgaf_wal_progress_hook(xlogpos, timeline)``
  Called on every other check-in (far more often than a segment
  actually closes, roughly once per ``--status-interval`` round).
  ``xlogpos`` here is raw stream position, not guaranteed to land on a
  record boundary -- an observability/lag metric only, never a safe
  replay target on its own. This is why ``receivewal-progress`` is a
  file of its own, never folded into ``wal_dir_scan.c``'s own
  directory-scan cache (``archiver-position``): every other reader of
  that cache treats it as a safe resume point, which this hook's own
  reading is not guaranteed to be.

That file is what ``pg_walserver ps``/``pg_walserver status`` read
back (each running receivewal worker's own ``lsn <LSN> (timeline <N>,
<secs>s ago)`` line) and what ``pg_walserver list clusters`` prefers
for its own WAL END column when a cluster's receivewal worker is
running, falling back to the directory-scan approximation otherwise.
Both hooks run synchronously, on the streaming loop's own thread of
control, so the write they trigger has to stay fast and non-blocking,
the same rule a real ``archive_command`` script has to follow --
best-effort only: a failed write is logged and never stops the worker
itself from streaming.

The only other change from upstream is mechanical: ``main()`` is
renamed to ``pg_receivewal_main()`` and is no longer ``static``/the
process entry point, so it can be called directly, once per
(re)connection cycle, from the forked child that runs it. Everything
else is byte-for-byte unchanged from PostgreSQL's own source.

What starts this service
--------------------------

Today, only :ref:`pg_walserver`'s embedded receivewal worker does: each
cluster configured with ``receivewal = pull`` (the default; see
:ref:`pg_walserver_cluster`'s ``--receivewal`` option) gets a supervised
child running ``pg_walserver internal service pg-receivewal --cluster
<key> --upstream <conninfo> --path <dir>`` -- a real, separate,
supervised process (restarted on failure the same way any other
supervised service in this project is), just one that calls
``pg_receivewal_main()`` in-process instead of exec'ing a system
``pg_receivewal``. This sub-command is hidden from ``--help``: it is
not meant to be run by hand.

Before starting that child, the parent creates (or confirms) a real,
permanent physical replication slot on the upstream -- named
deterministically from the cluster key -- and passes it along
(``-S <slot>``): the same guarantee a real streaming standby's own
slot gives it, keeping the upstream from recycling a WAL segment this
worker has not fetched yet out from under it.
