.. _pg_walserver:

pg_walserver
============

pg_walserver - standalone PostgreSQL replication-protocol server

Synopsis
--------

``pg_walserver`` speaks the PostgreSQL replication wire protocol well
enough to serve ``pg_basebackup``, ``pg_receivewal``, and a real standby's
walreceiver directly out of a directory tree of WAL segments and base
backups, without a live ``postmaster`` behind it. It is not part of
``pg_autoctl``'s own process supervision: it is started and stopped on its
own.

Running the accept loop (``serve``) is the default action, so a bare
invocation with server-mode options works with no sub-command name at
all::

  usage: pg_walserver [serve options] | scram-secret ... | setup ... |
                       fetch-systemid ... | basebackup ... |
                       create-cert ... | archive-wal ... | restore-wal ...

    serve           Run the accept loop (default command)
    scram-secret    Print one archiver-passwd line for a user
    setup           Create or validate one pg_walserver.ini route
    fetch-systemid  Fetch a route's upstream system identifier
    basebackup      Take a base backup of a route's upstream
    create-cert     Create a self-signed TLS certificate for --pgdata
    archive-wal     Push one WAL/.backup file into a route (archive_command)
    restore-wal     Fetch one WAL/.backup file from a route (restore_command)

See `Options`_ below for what each sub-command's flags do.

Description
-----------

Operating a PostgreSQL service in production requires a fully compliant
archiving story in place: it is the foundation of disaster recovery and
data durability in the event of a crash. PostgreSQL itself does not
provide an archiving implementation, only a well-specified contract for
one (``archive_command``/``restore_command``, base backups, timelines).
External solutions exist to fill that gap, but none of them speak the
PostgreSQL replication protocol, which means that when the worst happens,
none of PostgreSQL's own tools -- ``pg_basebackup``, ``pg_receivewal``, a
real standby's ``primary_conninfo`` -- can talk to the archive to rebuild
a node.

``pg_walserver`` fills that gap: a replication-protocol-compatible
archiving server that implements PostgreSQL's own archiving contract in
full. It combines streaming (the embedded pull capturer, for efficiency)
with ``archive_command`` (for robustness) rather than requiring one or
the other.

Archiving one cluster
~~~~~~~~~~~~~~~~~~~~~

``pg_walserver setup`` connects to an upstream PostgreSQL instance,
records its system identifier, and takes a base backup. From that point
on, the route it created captures WAL continuously (an embedded,
supervised ``pg_receivewal``, on by default) directly into its own
storage. Adding ``archive_command = 'pg_walserver archive-wal ...'`` on
the primary is a defense-in-depth backstop on top of this, not a
replacement for it: every real production deployment should configure
both. See `Routing`_ below for what determines which files each
connecting client can reach, and `A complete standalone example`_ for the
full sequence.

Restoring, or building a standby, from the archive
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Because ``pg_walserver`` speaks the real protocol, restoring from it uses
PostgreSQL's own tools directly: ``pg_basebackup`` takes the base backup,
``restore_command = 'pg_walserver restore-wal ...'`` fetches WAL segments
during recovery, and a real standby can set ``primary_conninfo`` to
``pg_walserver`` itself and stream live changes with no intermediate
tooling at all.

Routing
~~~~~~~

One ``pg_walserver`` instance can archive more than one cluster.
``<pgdata>/pg_walserver.ini`` maps each route (an operator-chosen key,
carrying no filesystem meaning of its own) to its own storage root, and a
connection is matched to a route by its ``dbname``. One key, ``*``, is a
PgBouncer-style catch-all matching any ``dbname`` with no route of its
own.

A real standby's walreceiver cannot set its own ``dbname`` -- it always
sends the literal ``replication``, regardless of ``primary_conninfo`` --
so ``dbname``-based routing alone cannot direct it to one of several
named routes. ``pg_walserver`` resolves this with TLS SNI instead: each
route's ``--hostname`` becomes a second, independent way to select it,
matching whatever hostname the connecting client used. A deployment with
only one route needs none of this. The moment a second named route
exists, TLS becomes mandatory, and ``pg_walserver`` refuses to start
without it; see `Routing more than one cluster by name: TLS SNI`_ below
for the full mechanism and its DNS prerequisite.

Access control
~~~~~~~~~~~~~~

``<pgdata>/archiver-hba.conf`` decides, one rule per line
(``TYPE ROUTE USER ADDRESS METHOD``, first match wins), which peers may
connect and how they must authenticate; a missing, oversize, or malformed
file rejects every connection. ``<pgdata>/archiver-passwd`` holds one
SCRAM-SHA-256 verifier per line, produced with ``pg_walserver
scram-secret``. ``<pgdata>/server.crt``/``<pgdata>/server.key`` (or
``--ssl-cert-file``/``--ssl-key-file``) enable TLS; without them,
``hostssl`` HBA rules never match.

Without ``--pgdata`` (and no ``PGDATA`` environment variable), the server
refuses to start unless ``--insecure`` is given, which accepts any
``dbname`` with no authentication at all. This mode exists for trying
``pg_walserver`` out; it must never be used on a reachable network.

The wire protocol
~~~~~~~~~~~~~~~~~~

A connected client may issue ``IDENTIFY_SYSTEM``, ``SHOW``,
``BASE_BACKUP``, ``TIMELINE_HISTORY``,
``CREATE_REPLICATION_SLOT``/``READ_REPLICATION_SLOT``/
``DROP_REPLICATION_SLOT``, and ``START_REPLICATION``, exactly as against a
real PostgreSQL primary, plus two extensions of ``pg_walserver``'s own:
``FETCH_FILE '<name>'`` (a one-shot file fetch, used by ``restore-wal``)
and ``CHECK_FILE``/``ARCHIVE_FILE`` (the push-side counterpart, used by
``archive-wal``). See ``src/bin/pg_walserver/README.md`` for the wire
protocol's full design.

Options
-------

``serve``
~~~~~~~~~

The default sub-command; its own flags may also be given with no
sub-command name at all.

--port

  Port to listen on. Defaults to ``6543``.

--pgdata

  This instance's own top-level storage root. Defaults to the ``PGDATA``
  environment variable. ``<pgdata>/pg_walserver.ini`` and
  ``<pgdata>/archiver-hba.conf`` are read from under it. Refuses to start
  without it unless ``--insecure`` is given.

--insecure

  Accept any ``dbname`` with no authentication whatsoever, without
  ``--pgdata``. For manual testing only, never on a reachable network.

--ssl-cert-file

  The server certificate to use for TLS. Defaults to
  ``<pgdata>/server.crt``. Without a usable certificate and key, TLS is
  disabled and ``hostssl`` HBA lines never match.

--ssl-key-file

  The server private key to use for TLS. Defaults to
  ``<pgdata>/server.key``.

--auth-timeout

  Absolute deadline, in seconds, for a connection to complete its startup
  packet, TLS handshake, HBA lookup, and SCRAM exchange. Defaults to
  ``30``.

``scram-secret``
~~~~~~~~~~~~~~~~

Prints one ``archiver-passwd`` line (``<user>:<SCRAM-SHA-256 secret>``) to
standard output. The password is read from the ``PGPASSWORD`` environment
variable, never from the command line.

--user

  Role name the printed line authenticates. Defaults to
  ``pgautofailover_replicator``.

``fetch-systemid``
~~~~~~~~~~~~~~~~~~

Connects to a route's upstream, fetches its system identifier, and writes
it to ``<path>/archiver-systemid``. Refuses to overwrite an
already-recorded, different identifier unless ``--force``.

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--route

  The route key to fetch for, looked up in ``pg_walserver.ini``.

--path

  The route's own directory. Overrides the route's own ``path`` property.

--upstream

  A libpq connection string to connect with. Overrides the route's own
  ``upstream`` property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--force

  Overwrite an already-recorded, different system identifier.

``basebackup``
~~~~~~~~~~~~~~

Takes a real base backup from a route's upstream into
``<path>/basebackups/<label>/``, then updates
``<path>/basebackups/.latest`` once the backup is verified complete.

Accepts the same ``--pgdata``, ``--route``, ``--path``, ``--upstream``,
``--host``/``--port``/``--user`` options as ``fetch-systemid``.

``setup``
~~~~~~~~~

Writes or validates one ``pg_walserver.ini`` route, fetches its upstream
system identifier, and, with ``--with-basebackup``, takes its first base
backup synchronously.

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--route

  The route key to create or validate.

--path

  The route's own directory, created if missing.

--upstream

  A libpq connection string, written into the route's own ``upstream``
  property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--hostname

  The route's own TLS SNI hostname, written into its ``hostname``
  property. Creates a self-signed certificate for ``--pgdata``
  automatically the first time a second named route needs one.

--capture

  ``pull`` (the default) or ``none``. ``pull`` writes ``capture = pull``
  into the route, starting an embedded, supervised ``pg_receivewal``
  child against ``upstream`` once ``serve`` runs. ``none`` is equivalent
  to ``--no-capture``.

--no-capture

  Equivalent to ``--capture none``.

--force

  Overwrite an existing route's ``path``/``upstream`` instead of refusing.

--with-basebackup

  Take the route's first base backup before returning.

``create-cert``
~~~~~~~~~~~~~~~

Creates a self-signed TLS certificate for ``--pgdata``.

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``. The
  certificate is written as ``<pgdata>/server.crt`` and
  ``<pgdata>/server.key``.

--hostname

  The certificate's own CN/subject.

--force

  Overwrite an already-existing ``server.crt``/``server.key``.

``archive-wal``
~~~~~~~~~~~~~~~

::

  pg_walserver archive-wal <path-to-file> <filename> --route <key>
      --host <host> [--port <port>] [--user <name>] [--sslmode <mode>]

Pushes one WAL segment or ``.backup`` history file into a route, for use
as PostgreSQL's own ``archive_command``:

::

  archive_command = 'pg_walserver archive-wal %p %f --route mycluster \
                       --host archive.example.com --user archiver_repl'

--route

  The route to archive into, sent as ``dbname``.

--host

  The ``pg_walserver`` host to connect to.

--port

  The ``pg_walserver`` port to connect to. Defaults to ``6543``.

--user

  Role name. Defaults to ``pgautofailover_replicator``.

--sslmode

  libpq ``sslmode``. Defaults to libpq's own default, ``prefer``.

``restore-wal``
~~~~~~~~~~~~~~~

::

  pg_walserver restore-wal <filename> <destination-path> --route <key>
      --host <host> [--port <port>] [--user <name>] [--sslmode <mode>]

Fetches one WAL segment or ``.backup`` history file from a route, for use
as PostgreSQL's own ``restore_command``:

::

  restore_command = 'pg_walserver restore-wal %f %p --route mycluster \
                        --host archive.example.com --user archiver_repl'

Accepts the same ``--route``, ``--host``, ``--port``, ``--user``,
``--sslmode`` options as ``archive-wal``.

Environment
-----------

PGDATA

  This instance's own top-level storage root. Can be used instead of
  ``--pgdata``.

PGPASSWORD

  The password ``pg_walserver scram-secret`` builds a verifier from, and
  the password used by ``archive-wal``/``restore-wal`` when connecting.

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

This example archives one ordinary PostgreSQL instance and restores it
with point-in-time recovery, without pg_auto_failover.

**1. On the primary**, create a replication role and add a line to
``pg_hba.conf`` admitting it over a replication connection from the
archive host, then reload::

  primary$ psql -c "CREATE ROLE archiver_repl REPLICATION LOGIN PASSWORD 's3kr3t'"

``wal_level`` must already be ``replica`` or higher (the default since
PostgreSQL 10).

**2. On the archive host**, create the route, fetch the system
identifier, and take the first base backup::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --route mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require" \
      --with-basebackup

``capture = pull`` is written by default, so the route's own WAL segments
are captured continuously once ``serve`` starts (below), without a
separate ``pg_receivewal`` process. Pass ``--no-capture`` to skip this and
feed the route another way (an externally-run ``pg_receivewal``, or
``archive-wal`` alone).

**3. Configure access and start the server**. ``setup`` does not touch
HBA or the password file::

  archive$ PGPASSWORD=s3kr3t pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/archiver-passwd
  archive$ cat > /var/lib/archiver/archiver-hba.conf <<EOF
  hostssl  mycluster  archiver_repl  10.0.0.0/8  scram-sha-256
  EOF
  archive$ pg_walserver --pgdata /var/lib/archiver --port 6543

**4. Optional: add** ``archive_command`` **as a backstop** alongside the
embedded capturer, on the primary::

  archive_mode = on
  archive_command = 'pg_walserver archive-wal %p %f --route mycluster --host archive --port 6543 --user archiver_repl --sslmode require'

Each invocation asks ``pg_walserver`` via ``CHECK_FILE`` whether it
already has the segment, and only pushes it via ``ARCHIVE_FILE`` when it
doesn't; exit code 0 on success (including "already there"), nonzero
otherwise, matching the ``archive_command`` contract.

**5. Point-in-time recovery**: take a real ``pg_basebackup`` against
``pg_walserver``, then use ``restore-wal`` as ``restore_command``::

  restore$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/pitr -X none --no-manifest
  restore$ cat >> /var/lib/postgres/pitr/postgresql.auto.conf <<EOF
  restore_command = 'PGPASSWORD=s3kr3t pg_walserver restore-wal %f %p --route mycluster --host archive --port 6543 --user archiver_repl --sslmode require'
  recovery_target_time = '2026-09-27 11:30:00+00'
  EOF
  restore$ touch /var/lib/postgres/pitr/recovery.signal
  restore$ pg_ctl -D /var/lib/postgres/pitr start

PostgreSQL replays WAL from the base backup's own start position, fetching
each missing segment via ``restore-wal``, until it reaches
``recovery_target_time`` and promotes. A raw ``FETCH_FILE`` via ``psql``,
or any other replication-protocol client, also works for a one-off fetch::

  restore$ PGPASSWORD=s3kr3t psql "host=archive port=6543 dbname=mycluster user=archiver_repl replication=true sslmode=require" -c "FETCH_FILE <segment>" > <destination>

**6. Or a real, continuously-streaming standby** instead of PITR: the same
base backup, but with ``primary_conninfo`` and ``standby.signal``::

  standby$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/standby -X none --no-manifest
  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=archive port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF
  standby$ touch /var/lib/postgres/standby/standby.signal
  standby$ pg_ctl -D /var/lib/postgres/standby start

A real walreceiver's physical replication connection always sends the
literal ``dbname=replication``, regardless of what ``primary_conninfo``
says (PostgreSQL's own ``libpqrcv_connect()`` overrides it
unconditionally). A route reachable by a real standby by name therefore
needs a second section literally keyed ``[replication]`` pointing at the
same ``path``, a ``"*"`` wildcard route, or TLS SNI routing (below).

Routing more than one cluster by name: TLS SNI
-----------------------------------------------

The ``[replication]`` alias above only disambiguates a single cluster: a
real standby's walreceiver always sends the same literal ``dbname``, so a
second cluster needs a different signal. ``pg_walserver`` reads the TLS
Server Name Indication (SNI) extension every TLS client sends during the
handshake. libpq's own ``sslsni`` setting (on by default) sends the
connection's ``host=`` value this way, so a real standby's
``primary_conninfo`` already carries what is needed.

Each route's ``--hostname`` needs its own DNS entry (an A record or a
CNAME; either resolves identically for this purpose), and every one of
them must resolve to this ``pg_walserver`` instance. This is provisioned
outside ``pg_walserver`` entirely. A connection using a literal IP address
never sends SNI (RFC 6066), and cannot be routed by hostname.

One route needs none of this: ``dbname`` alone is unambiguous, and
``serve`` runs with no TLS configured at all. The moment a second named
route exists, TLS is required; ``pg_walserver`` refuses to start
otherwise. ``setup`` creates a self-signed certificate for ``--pgdata``
automatically the first time a second named route needs one::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --route mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary port=5432 user=archiver_repl sslmode=require" \
      --hostname mycluster.archive.example.com \
      --with-basebackup

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --route another \
      --path /var/lib/archiver/another \
      --upstream "host=primary2 port=5432 user=archiver_repl sslmode=require" \
      --hostname another.archive.example.com \
      --with-basebackup

  archive$ pg_walserver --pgdata /var/lib/archiver --port 6543 &

``create-cert`` creates or, with ``--force``, replaces the certificate by
hand at any time::

  archive$ pg_walserver create-cert --pgdata /var/lib/archiver \
      --hostname mycluster.archive.example.com

Each standby then names its own route's hostname in ``primary_conninfo``'s
``host=``::

  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=mycluster.archive.example.com port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF

A connection with no resolvable hostname, and no ``*`` wildcard
configured, fails cleanly rather than matching another route.
Exact-``dbname`` routing keeps working unchanged alongside SNI, and is
always tried first.

See Also
--------

``src/bin/pg_walserver/README.md`` in the source tree documents the wire
protocol, routing precedence, the embedded pull capturer, and the
push-side ``CHECK_FILE``/``ARCHIVE_FILE`` design in full.
