.. _pg_walserver_archive_wal:

pg_walserver archive-wal
=========================

pg_walserver archive-wal - Push one WAL/.backup file into a pg_walserver cluster (archive_command)

Synopsis
--------

::

  pg_walserver archive-wal <path-to-file> <filename> --cluster <name>
      --host <host> [--port <port>] [--user <name>] [--sslmode <mode>]

Pushes one WAL segment or ``.backup`` history file into a cluster, for
use as PostgreSQL's own ``archive_command``::

  archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster \
                       --host archive.example.com --user archiver_repl'

The connected cluster's own ``receivewal`` setting decides what each
invocation does. With ``receivewal = pull`` configured, ``archive-wal``
ordinarily only ever runs ``CHECK_FILE``: it exits 0 when the segment
already matches what the cluster has, exits 1 otherwise, and never pushes
anything -- PostgreSQL's own retry of ``archive_command`` covers the
case where the embedded receivewal worker has not yet caught up. With
no ``receivewal = pull``, ``archive-wal`` only ever runs
``ARCHIVE_FILE``, unconditionally pushing the file, with no prior
``CHECK_FILE`` round trip.

The one exception on a ``receivewal = pull`` cluster: ``CHECK_FILE``'s own
smart-fallback signal. A streaming worker can only ever move forward, so
a segment it has already streamed *past* without ever producing --
almost always a timeline switch that left a segment behind on the old
timeline -- is a genuine hole it can never retroactively fill, not the
ordinary "hasn't caught up yet" case PostgreSQL's own retry loop already
handles. When ``CHECK_FILE`` detects this (cheaply, from the receivewal
worker's own last-observed position, the same one ``pg_walserver ps``
displays -- no extra directory scan), ``archive-wal`` pushes the file
directly via ``ARCHIVE_FILE`` right away instead of waiting on a retry
loop that would otherwise never succeed.

Options
-------

--cluster

  The cluster to archive into, sent as ``dbname``.

--host

  The ``pg_walserver`` host to connect to.

--port

  The ``pg_walserver`` port to connect to. Defaults to ``6543``.

--user

  Role name. Defaults to ``pgautofailover_replicator``.

--sslmode

  libpq ``sslmode``. Defaults to libpq's own default, ``prefer``.

Examples
--------

A cluster with ``receivewal = pull`` configured, run against a segment
the embedded receivewal worker has already delivered -- ``CHECK_FILE``
only, nothing pushed::

  primary$ PGPASSWORD=s3kr3t pg_walserver archive-wal \
      pg_wal/000000010000000000000018 000000010000000000000018 \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require
  21:13:25 2589209 INFO  "000000010000000000000018" already matches what "archive" has for "mycluster": nothing to push

The same cluster, run against a segment the embedded receivewal worker
has not caught up to yet -- ``CHECK_FILE`` fails, exit 1, and PostgreSQL retries
``archive_command`` later::

  primary$ PGPASSWORD=s3kr3t pg_walserver archive-wal \
      pg_wal/0000000100000000000000FF 0000000100000000000000FF \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require
  21:13:25 2589232 ERROR "0000000100000000000000FF" is not yet on "archive" cluster "mycluster" (missing): waiting for its own receivewal worker to catch up

The same cluster, run against an older segment that never arrived even
though the embedded receivewal worker has already streamed well past it
(a timeline switch left it behind, most commonly) -- ``CHECK_FILE`` says
so, and ``archive-wal`` pushes it directly instead of waiting on a retry
that would otherwise never succeed::

  primary$ PGPASSWORD=s3kr3t pg_walserver archive-wal \
      pg_wal/000000010000000000000002 000000010000000000000002 \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode disable
  01:12:16 131 INFO  "000000010000000000000002" is not on "archive" cluster "mycluster" (missing), and its own embedded receivewal worker has already streamed past it (likely a timeline switch left it behind): pushing it directly via ARCHIVE_FILE instead of waiting
  01:12:16 131 INFO  Archived "000000010000000000000002" to "archive" cluster "mycluster"

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_restore_wal`
* :ref:`pg_walserver_archive_cleanup`
* :ref:`pg_walserver_basebackup`
