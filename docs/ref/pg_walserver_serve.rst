.. _pg_walserver_serve:

pg_walserver serve
===================

pg_walserver serve - Run the pg_walserver accept loop (the default command)

Synopsis
--------

The default sub-command; its own flags may also be given with no
sub-command name at all::

  pg_walserver [serve] [--port <port>] [--pgdata <path> | --insecure]
      [--ssl-cert-file <path>] [--ssl-key-file <path>] [--ssl-ca-file <path>]
      [--auth-timeout <seconds>]

Options
-------

--port

  Port to listen on. Defaults to ``6543``.

--pgdata

  This instance's own top-level storage root. Defaults to the ``PGDATA``
  environment variable. ``<pgdata>/pg_walserver.ini`` and
  ``<pgdata>/pg_walserver_hba.conf`` are read from under it. Refuses to start
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

--ssl-ca-file

  A PEM bundle of trusted CA certificates, used to validate a client
  certificate presented during the TLS handshake. Defaults to
  ``<pgdata>/ca.crt``. Required for a ``clientcert=verify-full`` HBA line
  (see :ref:`pg_walserver`'s "Access control") to have anything to
  validate against; starting with such a line configured and no usable
  CA file is refused.

--auth-timeout

  Absolute deadline, in seconds, for a connection to complete its startup
  packet, TLS handshake, HBA lookup, and SCRAM exchange. Defaults to
  ``30``.

Environment
-----------

PGDATA

  This instance's own top-level storage root. Can be used instead of
  ``--pgdata``.

PGPASSWORD

  The password ``pg_walserver scram-secret`` builds a verifier from, and
  the password used by ``archive-wal``/``restore-wal`` when connecting.

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

**2. On the archive host**, create the route and fetch the system
identifier::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --cluster mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require"
  INFO  Added route "mycluster" (path "/var/lib/archiver/mycluster") to
        "/var/lib/archiver/pg_walserver.ini"
  INFO  Connecting to primary:5432 as "archiver_repl" to fetch the
        system identifier
  INFO  Wrote system identifier 7690580638048137639 to
        "/var/lib/archiver/mycluster/pg_walserver_systemid"
  INFO  setup complete: route "mycluster" is ready (no base backup taken
        here -- "pg_walserver serve" bootstraps the route's first base
        backup automatically, once, the next time it starts or reloads
        this route; run "pg_walserver basebackup" by hand at any time to
        take another one)
  INFO  No running "pg_walserver serve" found at
        "/var/lib/archiver/pg_walserver.pid": the route just written
        will take effect the next time "serve" starts

``capture = pull`` is written by default, so the route's own WAL segments
are captured continuously once ``serve`` starts (below), without a
separate ``pg_receivewal`` process. Pass ``--no-capture`` to skip this and
feed the route another way (an externally-run ``pg_receivewal``, or
``archive-wal`` alone). No server is running yet at this point, so
``setup`` only logs that this config will take effect the next time
``serve`` starts -- which is the next step.

**3. Configure access and start the server**. ``setup`` does not touch
HBA or the password file::

  archive$ PGPASSWORD=s3kr3t pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/pg_walserver_passwd
  archive$ cat > /var/lib/archiver/pg_walserver_hba.conf <<EOF
  hostssl  mycluster  archiver_repl  10.0.0.0/8  scram-sha-256
  EOF
  archive$ pg_walserver create-cert --pgdata /var/lib/archiver --hostname archive
  archive$ pg_walserver --pgdata /var/lib/archiver --port 6543
  INFO  Started the embedded pull capturer for route "mycluster" (pid
        25673), capturing into "/var/lib/archiver/mycluster"
  INFO  Route "mycluster" has no base backup yet: starting an automatic
        bootstrap base backup in the background (pid 25674)
  INFO  pg_walserver listening on port 6543, routes
        /var/lib/archiver/pg_walserver.ini
  INFO  Route "mycluster": taking its automatic bootstrap base backup
        (attempt 1/3)
  INFO  Base backup "basebackup-20260928T134117Z" is now the latest for
        "/var/lib/archiver/mycluster"
  INFO  Route "mycluster": automatic bootstrap base backup complete

``serve`` takes the route's first base backup automatically at this point,
in the background, once its embedded capturer (if any) shows real
streaming evidence: no separate ``pg_walserver basebackup`` call is
needed. Running ``pg_walserver setup`` again later, for the same or a new
route, while ``serve`` is already running, reloads it immediately (a
``SIGHUP``, the same as ``pg_walserver reload``) instead of waiting for a
restart.

**4. Optional: add** ``archive_command`` **as a backstop** alongside the
embedded capturer, on the primary::

  archive_mode = on
  archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'

``mycluster`` has ``capture = pull`` configured (step 2 above), so each
invocation only ever runs ``CHECK_FILE``: exit 0 once the embedded
capturer has already delivered the segment, exit 1 otherwise. It never
pushes anything itself; PostgreSQL's own retry of ``archive_command``
covers the case where the capturer has not yet caught up.

**5. Point-in-time recovery**: take a real ``pg_basebackup`` against
``pg_walserver``, then use ``restore-wal`` as ``restore_command``::

  restore$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/pitr -X none --no-manifest
  restore$ cat >> /var/lib/postgres/pitr/postgresql.auto.conf <<EOF
  restore_command = 'PGPASSWORD=s3kr3t pg_walserver restore-wal %f %p --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'
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
      -D /var/lib/postgres/standby -X stream --no-manifest
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

**7. Keep the archive from growing forever**: nothing above removes
anything on its own -- wire ``archive-cleanup`` into cron, keeping at
least a week of history and at least 3 base backups::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver archive-cleanup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d

Checking on a running archive
------------------------------

Continuing the example above, with ``serve`` running and both routes
captured or backed up at least once::

  archive$ pg_walserver status --pgdata /var/lib/archiver
  pg_walserver: running (pid 25671, uptime 0h04m31s)
    clusters:  2 configured, 2 with a base backup
    capturers: 1/1 running
    bootstrap backups pending: 0

  archive$ pg_walserver list clusters --pgdata /var/lib/archiver
  CLUSTER              BACKUP   CAPTURE   CAPTURER  WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull      yes       0/04000028             0/05000000
  another              yes      none      n/a       0/09000028             -

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver serve: pid 25671, running, uptime 0h04m31s

  KIND     CLUSTER              PID      STATUS    UPTIME       RESTARTS
  ----------------------------------------------------------------------
  capture  mycluster            25673    running   0h04m31s     0

``another`` shows ``CAPTURER n/a``: it was set up with ``--no-capture``,
so there is no embedded capturer to report on, running or otherwise --
its WAL arrives only through ``archive-wal``/``ARCHIVE_FILE`` pushes, so
``list wal --cluster another`` may legitimately show zero segments until
the primary's own ``archive_command`` has pushed at least one. See
:ref:`pg_walserver_status`, :ref:`pg_walserver_ps`, and
:ref:`pg_walserver_list` for each command's own manual page.

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
      --pgdata /var/lib/archiver --cluster mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary port=5432 user=archiver_repl sslmode=require" \
      --hostname mycluster.archive.example.com

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --cluster another \
      --path /var/lib/archiver/another \
      --upstream "host=primary2 port=5432 user=archiver_repl sslmode=require" \
      --hostname another.archive.example.com

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
