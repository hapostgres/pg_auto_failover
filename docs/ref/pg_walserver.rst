.. _pg_walserver:

pg_walserver
============

pg_walserver - the archiver's own standalone replication-protocol server

Synopsis
--------

``pg_walserver`` speaks enough of the PostgreSQL replication protocol to
serve ``pg_basebackup``, ``pg_receivewal`` and a real standby's own
walreceiver directly out of a directory tree of WAL segments and base
backups it owns, instead of out of a live ``postmaster``. Running the
accept loop (``serve``) is the default action, so a bare invocation with
server-mode options works with no sub-command name at all. This is
``pg_walserver --help``'s own output, verbatim::

  pg_walserver: The archiver's own replication-protocol server
  usage: pg_walserver [serve options] | scram-secret ...

    serve         Run the accept loop (default command, used when no
                  sub-command name is given at all)
    scram-secret  Print one archiver-passwd line for a user


  Available commands:
    pg_walserver
      serve         Run the pg_walserver accept loop (the default command)
      scram-secret  Print one archiver-passwd line for a user

Neither line in "Available commands" gets a ``+`` marker (unlike
:ref:`pg_autoctl`'s own tree, where ``create``, ``drop`` and friends do):
that marker means "this sub-command has sub-commands of its own", and
``serve``/``scram-secret`` are both leaves, not that one of them is the
default. ``--help``/``-h`` are also the one case ``pg_walserver``'s "no
sub-command name means serve" shim (``pg_walserver_default_argv()``,
``cli_root.c``) deliberately leaves alone, so ``pg_walserver --help`` shows
the *root* help above, never ``serve``'s own flags -- for those, ask
``serve`` directly, which is also ``--help``'s own output, verbatim::

  pg_walserver serve: Run the pg_walserver accept loop (the default command)
  usage: pg_walserver serve [--port <port>] [--pgdata <path> | --insecure] [--ssl-cert-file <path> --ssl-key-file <path>] [--auth-timeout <seconds>]

See `Options`_ below for what each flag does.

Description
-----------

``pg_walserver`` is not part of ``pg_autoctl``'s own process supervision:
it is a standalone binary, started and stopped on its own, that reads a
handful of files directly off disk to decide what it may serve and to
whom:

- ``<pgdata>/pg_walserver.ini`` maps each route this instance serves (an
  opaque key, matched against the connection's ``dbname``) to that route's
  own local storage root: WAL cache, base backups, and a handful of small
  bookkeeping files. A route key is never parsed or given any filesystem
  meaning of its own by ``pg_walserver`` -- pg_auto_failover's own
  convention is ``"<formation>/<group>"`` (e.g. ``default/0``), which reads
  like a path but is not one; a bare cluster name works exactly as well.
  One key, ``*``, is a PgBouncer-style catch-all matching any ``dbname``
  with no route of its own -- see `Examples`_ below;

- ``<pgdata>/archiver-hba.conf`` decides, one rule per line
  (``TYPE ROUTE USER ADDRESS METHOD``, first match wins), which peers may
  connect and how they must authenticate; a missing, oversize, or
  malformed file rejects every connection;

- ``<pgdata>/archiver-passwd`` holds one SCRAM-SHA-256 verifier per line,
  produced with ``pg_walserver scram-secret``;

- ``<pgdata>/server.crt`` / ``<pgdata>/server.key`` (or
  ``--ssl-cert-file`` / ``--ssl-key-file``) enable TLS; without them
  ``hostssl`` rules in the HBA file never match.

Without ``--pgdata`` (and no ``PGDATA`` environment variable either),
``pg_walserver`` refuses to start unless ``--insecure`` is given
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
would). See ``src/bin/pg_walserver/README.md`` in the source tree for the
full design.

Options
-------

The ``serve`` sub-command (the default; its own flags may be given with no
sub-command name at all) accepts:

--port

  Port to listen on. Defaults to ``6543``.

--pgdata

  The archiver's own top-level storage root, defaulting to the ``PGDATA``
  environment variable. ``<pgdata>/pg_walserver.ini`` and
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

  The password ``pg_walserver scram-secret`` builds a verifier from.

Examples
--------

Create a SCRAM secret for the replication role, from a password given in
the environment::

  $ PGPASSWORD='s3kr3t' pg_walserver scram-secret --user pgautofailover_replicator
  pgautofailover_replicator:SCRAM-SHA-256$4096:...

Run the server against an existing ``--pgdata`` directory::

  $ pg_walserver --pgdata /var/lib/archiver --port 6543

Run the server with no authentication, for manual testing only::

  $ pg_walserver --insecure --port 6543

A complete standalone example
------------------------------

This walks through archiving one ordinary, standalone PostgreSQL instance
-- no pg_auto_failover anywhere in the picture -- and restoring it with
point-in-time recovery, entirely by hand. It is the same shape
pg_auto_failover's own archiver automates later, using exactly the files
``pg_walserver`` itself reads: nothing here is specific to
pg_auto_failover.

**0. On the primary**: a replication role and enough WAL retained to catch
up::

  primary$ psql -c "CREATE ROLE archiver_repl REPLICATION LOGIN PASSWORD 's3kr3t'"

Add a line to the primary's own ``pg_hba.conf`` admitting that role over a
replication connection from the archive host, then reload
(``pg_ctl reload``). ``wal_level`` must already be ``replica`` or higher
(the default since PostgreSQL 10).

**1. On the archive host, one command**: ``pg_walserver setup`` creates the
route's own directory, writes its ``pg_walserver.ini`` section (``path``
and ``upstream``), fetches the real system identifier (``IDENTIFY_SYSTEM``
checks every connection against it), and -- with ``--with-basebackup`` --
takes the route's first base backup, synchronously: ``setup`` does not
return until it has actually succeeded. A route key is an opaque string of
your choosing (see `Description`_ above); this example uses ``mycluster``,
a plain name, specifically to show that pg_auto_failover's own
``"<formation>/<group>"`` convention is not required::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --route mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require" \
      --with-basebackup

That one command replaces what used to be three separate steps by hand:
writing ``pg_walserver.ini``'s section, fetching the system identifier with
a plain ``psql``, and taking the initial base backup with ``pg_basebackup``
directly. Either of the last two can still be run on their own, any time
after ``setup`` -- ``pg_walserver fetch-systemid`` and ``pg_walserver
basebackup`` take the same ``--route``/``--pgdata`` (or ``--path``/
``--upstream``) flags and are what ``setup`` itself calls internally.

**2. Start continuous WAL capture**, straight into the route's own
directory (not a subdirectory -- ``START_REPLICATION``/``FETCH_FILE`` read
WAL segments directly out of a route's own top-level directory), as a
long-running service (a plain ``&`` here for the example; run it under a
real process supervisor in production; an embedded, supervised capturer is
planned, see ``DESIGN-standalone-archiving.md``)::

  archive$ nohup env PGPASSWORD=s3kr3t pg_receivewal \
      -d "host=primary user=archiver_repl sslmode=require" \
      -D /var/lib/archiver/mycluster --synchronous \
      > /var/lib/archiver/mycluster-receivewal.log 2>&1 &

(A non-default ``wal_segment_size`` needs one more file,
``<path>/archiver-walsegsize``, holding the byte count in decimal; the
16MB default needs nothing extra.)

**3. Configure access and start pg_walserver** -- ``setup`` never touches
HBA or the passwd file, a deliberately separate concern::

  archive$ PGPASSWORD=s3kr3t pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/archiver-passwd
  archive$ cat > /var/lib/archiver/archiver-hba.conf <<EOF
  hostssl  mycluster  archiver_repl  10.0.0.0/8  scram-sha-256
  EOF
  archive$ pg_walserver --pgdata /var/lib/archiver --port 6543

Running more than one cluster behind the same ``pg_walserver``, and don't
want to name each one individually? Skip the ``[mycluster]`` section and
write a single wildcard route instead (see `Description`_ above and
``src/bin/pg_walserver/README.md`` for the full precedence rules and why
it is deliberately *not* PgBouncer's own per-request substitution)::

  [*]
  path = /var/lib/archiver/shared

**4. Point-in-time recovery**: build a fresh ``PGDATA`` from the base
backup ``pg_walserver`` is serving (a real ``pg_basebackup`` against
``pg_walserver`` itself, exercising the exact same wire protocol a real
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
each missing segment from ``pg_walserver`` one ``FETCH_FILE`` request at a
time, until it reaches ``recovery_target_time`` and promotes.

**5. Or a real, continuously-streaming standby instead of PITR**: the same
base backup, but with ``primary_conninfo`` and ``standby.signal`` -- no
``restore_command``, no ``FETCH_FILE``, a genuine walreceiver talking
``START_REPLICATION`` to ``pg_walserver``::

  standby$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/standby -X none --no-manifest
  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=archive port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF
  standby$ touch /var/lib/postgres/standby/standby.signal
  standby$ pg_ctl -D /var/lib/postgres/standby start

One real-protocol subtlety worth knowing: unlike ``pg_basebackup``/
``pg_receivewal`` above, a real walreceiver's *physical* replication
connection never actually sends ``dbname=mycluster`` on the wire, whatever
``primary_conninfo`` says -- PostgreSQL's own ``libpqrcv_connect()``
replaces it with the literal string ``"replication"`` unconditionally
("the database name is ignored by the server in replication mode, but
specify 'replication' for .pgpass lookup", ``libpqwalreceiver.c``'s own
comment). So a route meant to be reachable by a real standby needs a
second section literally keyed ``[replication]`` (pointing at the same
``path``) alongside its named one -- or, for a single-route deployment,
just use routes.ini's own ``"*"`` wildcard from the start and never worry
about the key a client happens to send at all.
