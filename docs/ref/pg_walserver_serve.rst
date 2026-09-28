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

Archiving one ordinary PostgreSQL instance, without pg_auto_failover,
takes three steps: create a role on the primary, register the route,
and start the server. Everything after that -- an ``archive_command``
backstop, restoring with PITR, building a real standby, retention --
is optional, and covered in its own section below.

Creating the route and starting the server
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**1. On the primary**, create a replication role and add a line to
``pg_hba.conf`` admitting it over a replication connection from the
archive host, then reload::

  primary$ psql -c "CREATE ROLE archiver_repl REPLICATION LOGIN PASSWORD 's3kr3t'"

This needs ``wal_level`` already set to ``replica`` or higher (the
default since PostgreSQL 10).

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
  INFO  Wrote upstream Postgres version 170011 to
        "/var/lib/archiver/mycluster/pg_walserver_pgversion"
  INFO  setup complete: route "mycluster" is ready (no base backup taken
        here -- "pg_walserver serve" bootstraps the route's first base
        backup automatically, once, the next time it starts or reloads
        this route; run "pg_walserver basebackup" by hand at any time to
        take another one)
  INFO  No running "pg_walserver serve" found at
        "/var/lib/archiver/pg_walserver.pid": the route just written
        will take effect the next time "serve" starts

Receiving the route's own WAL continuously once ``serve`` starts
(below), with no separate ``pg_receivewal`` process, is the default
behavior: ``receivewal = pull`` is written into the route unless told
otherwise. Pass ``--no-receivewal`` to skip this and feed the route
another way (an externally-run ``pg_receivewal``, or ``archive-wal``
alone). No server is running yet at this point, so ``setup`` only logs
that this config will take effect the next time ``serve`` starts --
which is the next step. The recorded upstream Postgres version is also
what later picks the right ``pg_basebackup`` client for this route --
see :ref:`pg_walserver_basebackup`.

**3. Configure access and start the server**. ``setup`` does not touch
HBA or the password file::

  archive$ PGPASSWORD=s3kr3t pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/pg_walserver_passwd
  archive$ cat > /var/lib/archiver/pg_walserver_hba.conf <<EOF
  hostssl  mycluster  archiver_repl  10.0.0.0/8  scram-sha-256
  EOF
  archive$ pg_walserver create-cert --pgdata /var/lib/archiver --hostname archive
  archive$ pg_walserver --pgdata /var/lib/archiver --port 6543
  INFO  TLS is enabled ("/var/lib/archiver/server.crt")
  INFO  Started the embedded receivewal worker for route "mycluster"
        (pid 25673), receiving into "/var/lib/archiver/mycluster"
  INFO  Route "mycluster" has no base backup yet: starting an automatic
        bootstrap base backup in the background (pid 25674)
  INFO  pg_walserver listening on port 6543, routes
        /var/lib/archiver/pg_walserver.ini
  INFO  Route "mycluster": taking its automatic bootstrap base backup
        (attempt 1/3)
  INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known
        Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  INFO  Base backup "basebackup-20260928T134117Z" is now the latest for
        "/var/lib/archiver/mycluster"
  INFO  Route "mycluster": automatic bootstrap base backup complete

Taking the route's first base backup automatically at this point, in
the background, once its embedded receivewal worker (if any) shows
real streaming evidence, is ``serve``'s own job: no separate
``pg_walserver basebackup`` call is needed, and the ``pg_basebackup``
client it uses is picked to match the upstream's own recorded Postgres
version, not just whatever happens to be first on ``$PATH``. Running
``pg_walserver setup`` again later, for the same or a new route, while
``serve`` is already running, reloads it immediately (a ``SIGHUP``, the
same as ``pg_walserver reload``) instead of waiting for a restart.

Optional: adding archive_command as a backstop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add ``archive_command`` alongside the embedded receivewal worker, on
the primary::

  archive_mode = on
  archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'

Because ``mycluster`` has ``receivewal = pull`` configured (step 2
above), each invocation only ever runs ``CHECK_FILE``: exit 0 once the
embedded worker has already delivered the segment, exit 1 otherwise.
It never pushes anything itself; PostgreSQL's own retry of
``archive_command`` covers the case where the worker has not yet
caught up.

Restoring with point-in-time recovery
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Take a real ``pg_basebackup`` against ``pg_walserver``, then use
``restore-wal`` as ``restore_command``::

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

Building a real, continuously-streaming standby
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Instead of PITR, the same base backup can feed a real standby, with
``primary_conninfo`` and ``standby.signal``.

A real standby's own ``primary_conninfo`` never actually controls what
``dbname`` gets sent: PostgreSQL always substitutes the literal
``dbname=replication`` for any physical replication connection, no
matter what ``primary_conninfo`` itself says. ``mycluster`` is not
reachable by that name, so give it a second entry, keyed literally
``replication``, pointing at the same ``path``, directly in
``pg_walserver.ini``, then reload::

  archive$ cat >> /var/lib/archiver/pg_walserver.ini <<EOF

  [replication]
  path = /var/lib/archiver/mycluster
  EOF
  archive$ pg_walserver reload --pgdata /var/lib/archiver

An operator who only ever serves real physical standbys, never
``pg_basebackup``/``pg_receivewal`` by name, would instead use
``replication`` as the route's one and only key from the start. A
``"*"`` wildcard route, or TLS SNI routing (below), work just as well
when more than one cluster needs to be reachable this way::

  standby$ PGPASSWORD=s3kr3t pg_basebackup \
      -d "host=archive port=6543 user=archiver_repl dbname=mycluster sslmode=require" \
      -D /var/lib/postgres/standby -X stream --no-manifest
  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=archive port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF
  standby$ touch /var/lib/postgres/standby/standby.signal
  standby$ pg_ctl -D /var/lib/postgres/standby start

Retention: keeping the archive from growing forever
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nothing above removes anything on its own. Wire ``archive-cleanup``
into cron on its own schedule, keeping at least a week of history and
at least 3 base backups::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver archive-cleanup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d

Or, for the common case of "take a fresh backup, then prune to the
same policy" as one cron line, pass the same flags to ``basebackup``
directly instead -- see :ref:`pg_walserver_basebackup`::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver basebackup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d

Checking on a running archive
------------------------------

Continuing the example above, with ``serve`` running and both routes
receiving or backed up at least once::

  archive$ pg_walserver status --pgdata /var/lib/archiver
  pg_walserver: running (pid 25671, uptime 0h04m31s)
    clusters:  2 configured, 2 with a base backup
    receivewal workers: 1/1 running
      mycluster            lsn 0/04000060 (timeline 1, 1s ago)
    bootstrap backups pending: 0

  archive$ pg_walserver list clusters --pgdata /var/lib/archiver
  CLUSTER              BACKUP   RECEIVEWAL   WORKER   WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull         yes      0/02000028             0/04000060
  another               yes      none         n/a      0/09000028             -

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver(25671) running, uptime 0h04m31s
  `-- receivewal(25673) mycluster, running, uptime 0h04m31s, restarts 0, lsn 0/04000060 (timeline 1, 1s ago)

Having no embedded receivewal worker to report on, running or
otherwise, is why ``another`` shows ``WORKER n/a``: it was set up with
``--no-receivewal``, and its WAL arrives only through
``archive-wal``/``ARCHIVE_FILE`` pushes, so
``list wal --cluster another`` may legitimately show zero segments until
the primary's own ``archive_command`` has pushed at least one. See
:ref:`pg_walserver_status`, :ref:`pg_walserver_ps`, and
:ref:`pg_walserver_list` for each command's own manual page.

Routing more than one cluster by name: TLS SNI
-----------------------------------------------

The ``[replication]`` alias above only disambiguates a single cluster: a
real standby's ``primary_conninfo`` always produces the same literal
``dbname``, so a
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

The certificate can also be created, or with ``--force`` replaced, by
hand at any time, with ``create-cert``::

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

The wire protocol, routing precedence, the embedded receivewal worker, and
the push-side ``CHECK_FILE``/``ARCHIVE_FILE`` design are documented in
full in ``src/bin/pg_walserver/README.md``, in the source tree.
