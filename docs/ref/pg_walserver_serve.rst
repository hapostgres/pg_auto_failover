.. _pg_walserver_serve:

pg_walserver serve
===================

pg_walserver serve - Run the pg_walserver accept loop

Synopsis
--------

::

  pg_walserver serve [--port <port>] [--pgdata <path> | --insecure]
      [--ssl-cert-file <path>] [--ssl-key-file <path>] [--ssl-ca-file <path>]
      [--auth-timeout <seconds>]

No sub-command is ever implicit: ``pg_walserver`` alone only prints
usage and exits non-zero, ``serve`` always has to be spelled out.

Options
-------

--port

  Port to listen on. Defaults to ``6543``, unless
  :ref:`pg_walserver_setup` persisted a different one in
  ``pg_walserver.ini``'s own global section, in which case that becomes
  the default here instead.

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
  ``<pgdata>/server.crt``, or whatever :ref:`pg_walserver_setup`
  persisted. Without a usable certificate and key, TLS is disabled and
  ``hostssl`` HBA lines never match.

--ssl-key-file

  The server private key to use for TLS. Defaults to
  ``<pgdata>/server.key``, or whatever :ref:`pg_walserver_setup`
  persisted.

--ssl-ca-file

  A PEM bundle of trusted CA certificates, used to validate a client
  certificate presented during the TLS handshake. Defaults to
  ``<pgdata>/ca.crt``, or whatever :ref:`pg_walserver_setup` persisted.
  Required for a ``clientcert=verify-full`` HBA line (see
  :ref:`pg_walserver`'s "Access control") to have anything to validate
  against; starting with such a line configured and no usable CA file is
  refused.

--auth-timeout

  Absolute deadline, in seconds, for a connection to complete its startup
  packet, TLS handshake, HBA lookup, and SCRAM exchange. Defaults to
  ``30``, unless :ref:`pg_walserver_setup` persisted a different one.

Every flag above, given directly on this command line, always wins over
whatever :ref:`pg_walserver_setup` persisted for it in
``pg_walserver.ini``'s own global section -- persisted values only ever
apply when the equivalent flag here is left out.

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
takes three steps: create a role on the primary, start the server, and
register the cluster against it. Everything after that -- an
``archive_command`` backstop, restoring with PITR, building a real
standby, retention -- is optional, and covered in its own section
below.

The server is started first, with no cluster registered yet, then the
cluster is registered against the already-running server -- the way
``pg_walserver`` is meant to be deployed in practice, as an OS service
or a container's own PID 1, with clusters added and removed over its
lifetime rather than baked into a one-shot startup sequence. This also
demonstrates ``cluster register``'s own auto-reload behavior.

Starting the server
~~~~~~~~~~~~~~~~~~~~

**1. On the primary**, create a replication role and add a line to
``pg_hba.conf`` admitting it over a replication connection from the
archive host, then reload::

  primary$ psql -c "CREATE ROLE archiver_repl REPLICATION LOGIN PASSWORD 's3kr3t'"

This needs ``wal_level`` already set to ``replica`` or higher (the
default since PostgreSQL 10).

**2. On the archive host**, set ``PGDATA`` once for every command
below, create the SCRAM verifier and the HBA rule for the cluster this
instance is about to serve, and create its TLS certificate right away
(a self-signed one is enough to start with; replace it with a real one
before running on a reachable network)::

  archive$ export PGDATA=/var/lib/archiver
  archive$ PGPASSWORD=s3kr3t pg_walserver scram-secret --user archiver_repl \
      >> /var/lib/archiver/pg_walserver_passwd
  archive$ cat > /var/lib/archiver/pg_walserver_hba.conf <<EOF
  hostssl  mycluster  archiver_repl  0.0.0.0/0  scram-sha-256
  EOF
  archive$ pg_walserver create-cert --hostname archive

**3. Start the server**, in the background, before any cluster is
registered -- ``serve`` needs neither ``--pgdata`` (``PGDATA`` is
already exported above) nor ``--port`` (``6543`` is already the
default)::

  archive$ pg_walserver serve &
  23:02:10 92 INFO  TLS is enabled ("/var/lib/archiver/server.crt")
  23:02:10 92 INFO  pg_walserver listening on port 6543, routes /var/lib/archiver/pg_walserver.ini

Registering the cluster
~~~~~~~~~~~~~~~~~~~~~~~~

**4. Register the cluster** against the now-running server --
``--upstream`` takes a plain libpq connection string, keyword/value or
``postgres://`` URI, either works; ``--path`` only ever needs to be
given to override its own default, ``<pgdata>/<cluster>``::

  archive$ PGPASSWORD=s3kr3t pg_walserver cluster register --cluster mycluster \
      --upstream "postgres://archiver_repl@primary:5432/?sslmode=require"
  23:02:21 99 INFO  Added route "mycluster" (path "/var/lib/archiver/mycluster") to "/var/lib/archiver/pg_walserver.ini"
  23:02:21 99 INFO  Connecting to primary:5432 as "archiver_repl" to fetch the system identifier
  23:02:21 99 INFO  Wrote system identifier 7690725181130416167 to "/var/lib/archiver/mycluster/pg_walserver_systemid"
  23:02:21 99 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/mycluster/pg_walserver_pgversion"
  23:02:21 99 INFO  cluster register complete: route "mycluster" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  23:02:21 99 INFO  Reloaded the running pg_walserver (pid 92): it will pick up this route immediately

That reload is real, on the already-running server, not a restart --
its own log shows the route arriving, its embedded receivewal worker
starting, and the automatic bootstrap base backup that follows::

  23:02:21 92 INFO  Received SIGHUP: reloading "/var/lib/archiver/pg_walserver.ini" and "/var/lib/archiver/pg_walserver_hba.conf"
  23:02:21 92 INFO  reload: route "mycluster" added (path "/var/lib/archiver/mycluster")
  23:02:21 92 INFO  reload: routes: 1 added, 0 removed, 0 changed (1 total now)
  23:02:21 92 INFO  reload: HBA ruleset unchanged (1 rule)
  23:02:21 92 INFO  Started the embedded receivewal worker for route "mycluster" (pid 105), receiving into "/var/lib/archiver/mycluster"
  23:02:21 92 INFO  Reload: started a new embedded receivewal worker for route "mycluster"
  23:02:21 92 INFO  Reload: receivewal worker reconciliation: 1 started, 0 stopped, 0 restarted, 0 unchanged
  23:02:21 92 INFO  Reload complete: now serving 1 route
  23:02:21 92 INFO  Route "mycluster" has no base backup yet: starting an automatic bootstrap base backup in the background (pid 106)
  23:02:21 106 INFO  Route "mycluster": waiting for its embedded receivewal worker to start streaming before taking the bootstrap base backup
  23:02:21 106 INFO  Route "mycluster": taking its automatic bootstrap base backup (attempt 1/3)
  23:02:21 106 INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  23:02:21 106 INFO  Taking a base backup of primary:5432 into "/var/lib/archiver/mycluster/basebackups/basebackup-20260928T230221Z"
  23:02:23 106 INFO  Base backup "basebackup-20260928T230221Z" is now the latest for "/var/lib/archiver/mycluster"
  23:02:23 106 INFO  Route "mycluster": automatic bootstrap base backup complete

Receiving the route's own WAL continuously, with no separate
``pg_receivewal`` process, is the default behavior: ``receivewal =
pull`` is written into the route unless told otherwise
(``--no-receivewal`` opts out, feeding the route another way instead --
an externally-run ``pg_receivewal``, or ``archive-wal`` alone). Taking
the route's first base backup automatically, once its embedded
receivewal worker shows real streaming evidence, is ``serve``'s own
job: no separate ``pg_walserver basebackup`` call is needed, and the
``pg_basebackup`` client it uses is picked to match the upstream's own
recorded Postgres version (written alongside its system identifier by
``cluster register``), not just whatever happens to be first on
``$PATH`` -- see :ref:`pg_walserver_basebackup`. Running ``cluster
register`` again later, for the same or a new route, while ``serve`` is
already running, reloads it immediately (a ``SIGHUP``, the same as
``pg_walserver reload``) exactly as it just did here.

Adding archive_command as a backstop
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Every production deployment must also add ``archive_command``, on the
primary, alongside the embedded receivewal worker -- this is not
optional: the embedded worker alone cannot survive every gap a real
``archive_command`` closes (a timeline switch during a promotion, most
notably)::

  archive_mode = on
  archive_command = 'pg_walserver archive-wal %p %f --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'

Because ``mycluster`` has ``receivewal = pull`` configured (the
default), each invocation only ever runs ``CHECK_FILE``: exit 0 once the
embedded worker has already delivered the segment, exit 1 otherwise.
It never pushes anything itself; PostgreSQL's own retry of
``archive_command`` covers the case where the worker has not yet
caught up.

Restoring with point-in-time recovery
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Take a real ``pg_basebackup`` against ``pg_walserver``, using the
``postgres://`` URI style and one env var naming it, then use
``restore-wal`` as ``restore_command``::

  restore$ export WALSERVER_PGURI='postgres://archiver_repl@archive:6543/mycluster?sslmode=require'
  restore$ export PITR_PGDATA=/var/lib/postgres/pitr
  restore$ PGPASSWORD=s3kr3t pg_basebackup -d "${WALSERVER_PGURI}" -D "${PITR_PGDATA}" --verbose --progress
  pg_basebackup: initiating base backup, waiting for checkpoint to complete
  pg_basebackup: checkpoint completed
  pg_basebackup: write-ahead log start point: 0/04000028 on timeline 1
  pg_basebackup: starting background WAL receiver
  pg_basebackup: created temporary replication slot "pg_basebackup_152"
  pg_basebackup: write-ahead log end point: 0/05024770
  pg_basebackup: waiting for background process to finish streaming ...
  pg_basebackup: syncing data to disk ...
  pg_basebackup: renaming backup_manifest.tmp to backup_manifest
  pg_basebackup: base backup completed
  restore$ cat >> ${PITR_PGDATA}/postgresql.auto.conf <<EOF
  restore_command = 'PGPASSWORD=s3kr3t pg_walserver restore-wal %f %p --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'
  recovery_target_time = '2026-09-27 11:30:00+00'
  EOF
  restore$ touch ${PITR_PGDATA}/recovery.signal
  restore$ pg_ctl -D "${PITR_PGDATA}" start

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
  archive$ pg_walserver reload

An operator who only ever serves real physical standbys, never
``pg_basebackup``/``pg_receivewal`` by name, would instead use
``replication`` as the route's one and only key from the start. A
``"*"`` wildcard route, or TLS SNI routing (below), work just as well
when more than one cluster needs to be reachable this way::

  standby$ export WALSERVER_PGURI='postgres://archiver_repl@archive:6543/mycluster?sslmode=require'
  standby$ PGPASSWORD=s3kr3t pg_basebackup -d "${WALSERVER_PGURI}" -D /var/lib/postgres/standby -X stream
  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=archive port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF
  standby$ touch /var/lib/postgres/standby/standby.signal
  standby$ pg_ctl -D /var/lib/postgres/standby start

Retention: keeping the archive from growing forever
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Nothing above removes anything on its own. The preferred way is to pass
the same retention flags to ``basebackup`` directly, taking a fresh
backup and pruning to the same policy as one cron line -- see
:ref:`pg_walserver_basebackup`::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver basebackup \
      --path /var/lib/archiver/mycluster --upstream "host=primary port=5432 user=archiver_repl sslmode=require" \
      --keep-count 3 --keep-age 7d
  23:04:39 172 INFO  base backup completed
  23:04:39 172 INFO  Base backup "basebackup-20260928T230437Z" is now the latest for "/var/lib/archiver/mycluster"
  23:04:39 172 INFO  archive-cleanup: --keep-count 3 and --keep-age 7d both given; the more conservative (keeps more) of the two wins -- retaining WAL from "000000010000000000000004" onward

Or, to prune on its own schedule with no fresh backup attached, wire
``archive-cleanup`` into cron directly, with the same policy::

  archive$ crontab -l
  0 3 * * * PGPASSWORD=s3kr3t pg_walserver archive-cleanup \
      --path /var/lib/archiver/mycluster --keep-count 3 --keep-age 7d
  23:04:23 154 INFO  archive-cleanup: --keep-count 3 and --keep-age 7d both given; the more conservative (keeps more) of the two wins -- retaining WAL from "000000010000000000000004" onward
  23:04:23 154 INFO  archive-cleanup: removing "/var/lib/archiver/mycluster/000000010000000000000003": older than the retention cutoff ("000000010000000000000004")

Checking on a running archive
------------------------------

Continuing the example above, with ``serve`` running and ``mycluster``
receiving::

  archive$ pg_walserver status
  pg_walserver: running (pid 92, uptime 0h01m36s)
    receivewal workers: 1/1 running
    bootstrap backups pending: 0

  archive$ pg_walserver list clusters
  CLUSTER              BACKUP   RECEIVEWAL WORKER   WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull       yes      0/04000028             0/05024778

  archive$ pg_walserver ps
  pg_walserver(92) running, uptime 0h01m36s
  `-- receivewal(105) mycluster, running, uptime 0h01m25s, restarts 0, lsn 0/05024778 (timeline 1, 5s ago)

``status`` is deliberately process-only -- pid, uptime, receivewal
worker/bootstrap counts, nothing about any one cluster's own progress.
Per-worker LSN detail lives in ``ps`` instead, and storage-level facts
(which cluster has a backup, its WAL range) live in ``list clusters``
and :ref:`pg_walserver_ls`. See :ref:`pg_walserver_status`,
:ref:`pg_walserver_ps`, and :ref:`pg_walserver_list` for each command's
own manual page.

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
otherwise. The shortest path to a second, SNI-routed cluster, against
an already-running server with one cluster already registered and no
certificate yet, is::

  archive$ PGPASSWORD=s3kr3t pg_walserver cluster register --cluster another \
      --upstream "postgres://archiver_repl@primary2/?sslmode=require" \
      --hostname another.archive.example.com --ssl-self-signed
  22:57:22 63 INFO  Added route "another" (path "/var/lib/archiver/another") to "/var/lib/archiver/pg_walserver.ini"
  22:57:22 63 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=another.archive.example.com"
  22:57:22 63 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=another.archive.example.com) -- replace it with a real one before running on a reachable network
  22:57:22 63 INFO  "/var/lib/archiver/pg_walserver.ini" now has 2 routes: TLS is required for more than one route to be reachable by name (dbname-based routing alone cannot tell a real physical standby's connection apart from any other route once there is more than one, see this project's own README.md)
  22:57:22 63 INFO  Connecting to primary2:5432 as "archiver_repl" to fetch the system identifier
  22:57:22 63 INFO  Wrote system identifier 7690676421909321516 to "/var/lib/archiver/another/pg_walserver_systemid"
  22:57:22 63 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/another/pg_walserver_pgversion"
  22:57:22 63 INFO  cluster register complete: route "another" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  22:57:22 63 INFO  Reloaded the running pg_walserver (pid 92): it will pick up this route immediately

Without ``--ssl-self-signed`` here, this same ``cluster register`` call
would have created the certificate itself, automatically, the moment
the file it just wrote to reached two routes -- either way works, this
only gets it sooner. The certificate can also be created, or with
``--force`` replaced, by hand at any time, with ``create-cert``::

  archive$ pg_walserver create-cert --hostname mycluster.archive.example.com

Each standby then names its own route's hostname in ``primary_conninfo``'s
``host=``::

  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=another.archive.example.com port=6543 user=archiver_repl password=s3kr3t sslmode=require'
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
