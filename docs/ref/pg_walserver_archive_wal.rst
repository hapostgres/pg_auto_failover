.. _pg_walserver_archive_wal:

pg_walserver archive-wal
=========================

pg_walserver archive-wal - Push one WAL/.backup file into a pg_walserver route (archive_command)

Synopsis
--------

::

  pg_walserver archive-wal <path-to-file> <filename> --cluster <name>
      --host <host> [--port <port>] [--user <name>] [--sslmode <mode>]

Pushes one WAL segment or ``.backup`` history file into a route, for
use as PostgreSQL's own ``archive_command``::

  archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster \
                       --host archive.example.com --user archiver_repl'

The connected route's own ``receivewal`` setting decides what each
invocation does. With ``receivewal = pull`` configured, ``archive-wal``
only ever runs ``CHECK_FILE``: it exits 0 when the segment already
matches what the route has, exits 1 otherwise, and never pushes
anything -- PostgreSQL's own retry of ``archive_command`` covers the
case where the embedded receivewal worker has not yet caught up. With
no ``receivewal = pull``, ``archive-wal`` only ever runs
``ARCHIVE_FILE``, unconditionally pushing the file, with no prior
``CHECK_FILE`` round trip.

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

A route with ``receivewal = pull`` configured, run against a segment
the embedded receivewal worker has already delivered -- ``CHECK_FILE``
only, nothing pushed::

  primary$ PGPASSWORD=s3kr3t pg_walserver archive-wal \
      pg_wal/000000010000000000000002 000000010000000000000002 \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require
  INFO  "000000010000000000000002" already matches what "archive" has
        for "mycluster": nothing to push

The same route, run against a segment the embedded receivewal worker
has not caught up to yet -- ``CHECK_FILE`` fails, exit 1, and PostgreSQL retries
``archive_command`` later::

  primary$ PGPASSWORD=s3kr3t pg_walserver archive-wal \
      pg_wal/0000000100000000000000FF 0000000100000000000000FF \
      --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require
  ERROR "0000000100000000000000FF" is not yet on "archive" route
        "mycluster" (missing): waiting for its own pull capturer to catch up

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_restore_wal`
* :ref:`pg_walserver_archive_cleanup`
* :ref:`pg_walserver_basebackup`
