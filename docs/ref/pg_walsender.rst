.. _pg_walsender:

pg_walsender
============

pg_walsender - the archiver's own standalone replication-protocol server

Synopsis
--------

``pg_walsender`` speaks enough of the PostgreSQL replication protocol to
serve ``pg_basebackup``, ``pg_receivewal`` and a real standby's own
walreceiver directly out of a directory tree of WAL segments and base
backups it owns, instead of out of a live ``postmaster``. Running the
accept loop (``serve``) is the default action, so a bare invocation with
server-mode options works with no sub-command name at all::

  pg_walsender
  + serve         Run the accept loop (the default command)
    scram-secret  Print one archiver-passwd line for a user

  usage: pg_walsender [--port <port>] [--pgdata <path> | --insecure]
                       [--ssl-cert-file <path> --ssl-key-file <path>]
                       [--auth-timeout <seconds>]

  usage: pg_walsender scram-secret [--user <name>]

Description
-----------

``pg_walsender`` is not part of ``pg_autoctl``'s own process supervision:
it is a standalone binary, started and stopped on its own, that reads a
handful of files directly off disk to decide what it may serve and to
whom:

- ``<pgdata>/archiver-routes.ini`` maps each route this instance serves (an
  opaque key, matched against the connection's ``dbname``) to that route's
  own local storage root: WAL cache, base backups, and a handful of small
  bookkeeping files. A route key is never parsed or given any filesystem
  meaning of its own by ``pg_walsender`` -- pg_auto_failover's own
  convention is ``"<formation>/<group>"`` (e.g. ``default/0``), which reads
  like a path but is not one; a bare cluster name works exactly as well.
  One key, ``*``, is a PgBouncer-style catch-all matching any ``dbname``
  with no route of its own -- see `Examples`_ below;

- ``<pgdata>/archiver-hba.conf`` decides, one rule per line
  (``TYPE ROUTE USER ADDRESS METHOD``, first match wins), which peers may
  connect and how they must authenticate; a missing, oversize, or
  malformed file rejects every connection;

- ``<pgdata>/archiver-passwd`` holds one SCRAM-SHA-256 verifier per line,
  produced with ``pg_walsender scram-secret``;

- ``<pgdata>/server.crt`` / ``<pgdata>/server.key`` (or
  ``--ssl-cert-file`` / ``--ssl-key-file``) enable TLS; without them
  ``hostssl`` rules in the HBA file never match.

Without ``--pgdata`` (and no ``PGDATA`` environment variable either),
``pg_walsender`` refuses to start unless ``--insecure`` is given
explicitly, in which case every ``dbname`` is accepted with no
authentication at all -- intended for manual testing only, never on a
reachable network.

On the wire, a connected client may issue ``IDENTIFY_SYSTEM``, ``SHOW``,
``BASE_BACKUP``, ``TIMELINE_HISTORY``,
``CREATE_REPLICATION_SLOT``/``READ_REPLICATION_SLOT``/
``DROP_REPLICATION_SLOT``, and ``START_REPLICATION``, exactly as against a
real PostgreSQL primary, plus one project-specific extension,
``FETCH_FILE '<name>'``, used to fetch a single WAL segment or timeline
history file in one request/response round trip (as a ``restore_command``
would). See ``src/bin/pg_walsender/README.md`` in the source tree for the
full design.

Options
-------

The ``serve`` sub-command (the default; its own flags may be given with no
sub-command name at all) accepts:

--port

  Port to listen on. Defaults to ``6543``.

--pgdata

  The archiver's own top-level storage root, defaulting to the ``PGDATA``
  environment variable. ``<pgdata>/archiver-routes.ini`` and
  ``<pgdata>/archiver-hba.conf`` are read from under it. The server
  refuses to start without it unless ``--insecure`` is given.

--insecure

  No ``--pgdata``: accept any ``dbname`` with **no authentication
  whatsoever**. For manual testing only, never on a reachable network.

--ssl-cert-file

  The server certificate to use for TLS. Defaults to
  ``<pgdata>/server.crt``. Without a usable certificate and key, TLS is
  disabled and ``hostssl`` HBA lines never match.

--ssl-key-file

  The server private key to use for TLS. Defaults to
  ``<pgdata>/server.key``.

--auth-timeout

  Absolute deadline, in seconds, for a connection to complete its startup
  packet, TLS handshake, HBA lookup and SCRAM exchange. Defaults to
  ``30``.

The ``scram-secret`` sub-command prints one ``archiver-passwd`` line
(``<user>:<SCRAM-SHA-256 secret>``) to standard output, reading the
password from the ``PGPASSWORD`` environment variable -- never from the
command line, where it would be visible in the process list:

--user

  Role name the printed line authenticates. Defaults to
  ``pgautofailover_replicator``.

Environment
-----------

PGDATA

  The archiver's own top-level storage root. Can be used instead of the
  ``--pgdata`` option.

PGPASSWORD

  The password ``pg_walsender scram-secret`` builds a verifier from.

Examples
--------

Create a SCRAM secret for the replication role, from a password given in
the environment::

  $ PGPASSWORD='s3kr3t' pg_walsender scram-secret --user pgautofailover_replicator
  pgautofailover_replicator:SCRAM-SHA-256$4096:...

Run the server against an existing ``--pgdata`` directory::

  $ pg_walsender --pgdata /var/lib/archiver --port 6543

Run the server with no authentication, for manual testing only::

  $ pg_walsender --insecure --port 6543

A complete standalone example
------------------------------

This walks through archiving one ordinary, standalone PostgreSQL instance
-- no pg_auto_failover anywhere in the picture -- and restoring it with
point-in-time recovery, entirely by hand. It is the same shape
pg_auto_failover's own archiver automates later, using exactly the files
``pg_walsender`` itself reads: nothing here is specific to
pg_auto_failover.

**0. On the primary**: a replication role and enough WAL retained to catch
up::

  primary$ psql -c "CREATE ROLE archiver_repl REPLICATION LOGIN PASSWORD 's3kr3t'"

Add a line to the primary's own ``pg_hba.conf`` admitting that role over a
replication connection from the archive host, then reload
(``pg_ctl reload``). ``wal_level`` must already be ``replica`` or higher
(the default since PostgreSQL 10).

**1. On the archive host**: pick a storage root and a route key. A route
key is an opaque string of your choosing (see `Description`_ above) --
this example uses ``mycluster``, a plain name, specifically to show that
pg_auto_failover's own ``"<formation>/<group>"`` convention is not
required::

  archive$ export ROUTE=/var/lib/archiver/mycluster
  archive$ mkdir -p $ROUTE/basebackups

Record the cluster's system identifier, checked on every connection
(``IDENTIFY_SYSTEM``) so a client can tell it is talking to the right
archive::

  archive$ PGPASSWORD=s3kr3t psql "host=primary user=archiver_repl dbname=postgres sslmode=require" \
      -Atc "SELECT system_identifier FROM pg_control_system()" \
      > $ROUTE/archiver-systemid

**2. Take an initial base backup**, in plain format, directly under
``$ROUTE/basebackups/`` -- ``BASE_BACKUP`` re-serves whatever is on disk
there, it never takes one itself. ``-X none``: a live, continuous WAL
capture is started next, so the base backup does not need to stream WAL of
its own too::

  archive$ label=$(date -u +%Y%m%dT%H%M%SZ)
  archive$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=primary user=archiver_repl sslmode=require" \
      -D $ROUTE/basebackups/$label -X none --no-manifest -c fast
  archive$ echo "$label" > $ROUTE/basebackups/.latest

**3. Start continuous WAL capture**, straight into ``$ROUTE`` itself (not a
subdirectory -- ``START_REPLICATION``/``FETCH_FILE`` read WAL segments
directly out of a route's own top-level directory), as a long-running
service (a plain ``&`` here for the example; run it under a real process
supervisor in production)::

  archive$ nohup env PGPASSWORD=s3kr3t pg_receivewal \
      -d "host=primary user=archiver_repl sslmode=require" \
      -D $ROUTE --synchronous > $ROUTE/../mycluster-receivewal.log 2>&1 &

(A non-default ``wal_segment_size`` needs one more file,
``$ROUTE/archiver-walsegsize``, holding the byte count in decimal; the
16MB default needs nothing extra.)

**4. Configure and start pg_walsender**::

  archive$ mkdir -p /var/lib/archiver
  archive$ cat > /var/lib/archiver/archiver-routes.ini <<EOF
  [mycluster]
  path = $ROUTE
  EOF
  archive$ PGPASSWORD=s3kr3t pg_walsender scram-secret --user archiver_repl \
      >> /var/lib/archiver/archiver-passwd
  archive$ cat > /var/lib/archiver/archiver-hba.conf <<EOF
  hostssl  mycluster  archiver_repl  10.0.0.0/8  scram-sha-256
  EOF
  archive$ pg_walsender --pgdata /var/lib/archiver --port 6543

Running more than one cluster behind the same ``pg_walsender``, and don't
want to name each one individually? Skip the ``[mycluster]`` section and
write a single wildcard route instead (see `Description`_ above and
``src/bin/pg_walsender/README.md`` for the full precedence rules and why
it is deliberately *not* PgBouncer's own per-request substitution)::

  [*]
  path = /var/lib/archiver/shared

**5. Point-in-time recovery**: build a fresh ``PGDATA`` from the base
backup ``pg_walsender`` is serving (a real ``pg_basebackup`` against
``pg_walsender`` itself, exercising the exact same wire protocol a real
standby uses), then let ``restore_command`` fetch each WAL segment on
demand via ``FETCH_FILE``, this project's own single-request/response
extension (see `Description`_ above) -- a plain ``psql`` one-liner is
enough, no client tooling beyond what ships with PostgreSQL itself::

  restore$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/pitr -X none --no-manifest
  restore$ cat >> /var/lib/postgres/pitr/postgresql.auto.conf <<EOF
  restore_command = 'PGPASSWORD=s3kr3t psql "host=archive port=6543 dbname=mycluster user=archiver_repl replication=true sslmode=require" -c "FETCH_FILE %f" > %p'
  recovery_target_time = '2026-09-27 11:30:00+00'
  EOF
  restore$ touch /var/lib/postgres/pitr/recovery.signal
  restore$ pg_ctl -D /var/lib/postgres/pitr start

PostgreSQL replays WAL from the base backup's own start position, fetching
each missing segment from ``pg_walsender`` one ``FETCH_FILE`` request at a
time, until it reaches ``recovery_target_time`` and promotes.
