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
all. This is ``pg_walserver --help``'s own output, verbatim::

  usage: pg_walserver [serve options] | scram-secret ... | setup ... |
                       fetch-systemid ... | basebackup ... |
                       create-cert ... | archive-wal ... | restore-wal ... |
                       archive-cleanup ... | reload ... | ps ... | ls ... |
                       status ... | list clusters|backups|wal ...

  Available commands:
    pg_walserver
      serve           Run the pg_walserver accept loop (the default command)
      scram-secret    Print one pg_walserver_passwd line for a user
      fetch-systemid  Fetch a route's upstream system identifier
      basebackup      Take a base backup of a route's upstream
      setup           Create or validate one pg_walserver.ini route
      create-cert     Create a self-signed TLS certificate for --pgdata
      archive-wal     Push one WAL/.backup file into a pg_walserver route (archive_command)
      restore-wal     Fetch one WAL/.backup file from a pg_walserver route (restore_command)
      archive-cleanup Remove WAL/base backups this route no longer needs to keep
      reload          Ask a running pg_walserver to reload its configuration
      ps              Show serve's own process-level status (pid, capturers, bootstrap jobs)
      ls              List pg_walserver's own on-disk footprint under --pgdata
      status          Show a short pg_walserver status dashboard
      list clusters   List every route, its backup/capture status, and its WAL range
      list backups    List base backups per cluster
      list wal        List WAL cache stats per cluster, or every file with --segments

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

A cluster's data is safe once three things are on record: its system
identifier, a base backup to restore from, and continuous WAL capture
from that point on. ``pg_walserver`` builds all three from a single
``setup`` call followed by a running ``serve``:

``pg_walserver setup`` connects to the upstream PostgreSQL instance,
records its system identifier, and writes the route into
``pg_walserver.ini``. ``pg_walserver serve`` picks the route up -- at
startup, and again after every ``pg_walserver reload`` -- takes its
first base backup automatically, and starts capturing WAL continuously
into the route's own storage (an embedded, supervised
``pg_receivewal``, on by default). Running ``setup`` once and starting
(or reloading) ``serve`` is the whole sequence; no separate step takes
that first backup.

Add ``archive_command = 'pg_walserver archive-wal ...'`` on the primary
as well, as a defense-in-depth backstop alongside continuous capture:
every production deployment should configure both. Keeping a route's
backup current after that first, automatic one is a recurring operator
task, the same as WAL retention: run ``pg_walserver basebackup`` by
hand, or from a cron job, whenever a fresh one is wanted. See
`Routing`_ below for what determines which files each connecting
client can reach, and `A complete standalone example`_ for the full
sequence.

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
carrying no filesystem meaning of its own) to its own storage root: the
WAL and base backups of one entire PostgreSQL cluster (its whole
``PGDATA``), never a single database within it -- WAL archiving is
inherently a whole-cluster concept. Every connection is matched to its
route one of two ways: by ``dbname``, tried first and enough on its own
for a single cluster; or, for a client that cannot set its own
``dbname`` (a real standby's walreceiver, most notably), by the TLS SNI
hostname it connected with.

A single cluster needs only ``dbname``, set to the route's own key::

  archive$ pg_walserver setup --pgdata /var/lib/archiver --cluster mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require"

  standby$ psql "host=archive port=6543 dbname=mycluster user=archiver_repl sslmode=require" \
      -c "IDENTIFY_SYSTEM"

More than one cluster behind the same ``pg_walserver`` instance needs a
second, independent way to tell them apart: TLS SNI virtual-hosts each
one behind its own ``--hostname``::

  archive$ pg_walserver setup --pgdata /var/lib/archiver --cluster mycluster \
      --path /var/lib/archiver/mycluster \
      --upstream "host=primary port=5432 user=archiver_repl sslmode=require" \
      --hostname mycluster.archive.example.com

  archive$ pg_walserver setup --pgdata /var/lib/archiver --cluster another \
      --path /var/lib/archiver/another \
      --upstream "host=primary2 port=5432 user=archiver_repl sslmode=require" \
      --hostname another.archive.example.com

  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=mycluster.archive.example.com port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF

``setup`` creates a self-signed certificate for ``--pgdata``
automatically the moment a second named route needs one, and
``pg_walserver`` requires TLS from that point on. See `Routing more than
one cluster by name: TLS SNI`_ below for the full mechanism, its DNS
prerequisite, and why a real standby's walreceiver needs it.

A connection's ``dbname`` is a routing key, not a database selection:
real PostgreSQL's own physical replication connections repurpose the
same field for an unrelated reason. ``libpqrcv_connect()`` sends the
literal ``dbname=replication`` on every walreceiver connection,
overriding whatever ``primary_conninfo`` says, purely so a ``.pgpass``
lookup has something to match -- the server itself ignores ``dbname``
once ``replication=true`` is set, because the replication protocol has
no field of its own for "which cluster is this." ``pg_walserver``
reuses that same ignored field as its own routing key instead: it never
corresponds to an actual database inside the archived cluster, and is
never validated against one. One key, ``*``, is a PgBouncer-style
catch-all matching any ``dbname`` with no route of its own.

Access control
~~~~~~~~~~~~~~

``<pgdata>/pg_walserver_hba.conf`` decides, one rule per line
(``TYPE ROUTE USER ADDRESS METHOD``, first match wins), which peers may
connect and how they must authenticate; a missing, oversize, or malformed
file rejects every connection. Three ``METHOD`` values are supported:

``trust``

  Accept the connection outright, with no password check. Only appropriate
  behind another access control already trusted, such as a firewalled
  private network.

``scram-sha-256``

  Run a real SCRAM-SHA-256 exchange (RFC 5802) against a verifier stored in
  ``pg_walserver_passwd``. The method to use for any connection reachable
  from outside a fully trusted network.

``reject``

  Refuse the connection outright. Also the default when no rule matches at
  all, so a rule is required to admit anything.

``<pgdata>/pg_walserver_passwd`` holds the credential store ``scram-sha-256``
checks against: one SCRAM-SHA-256 verifier per line, produced with
``pg_walserver scram-secret``. ``pg_walserver`` is not PostgreSQL: there is
no ``pg_authid`` or role system underneath it to check a password against,
so this file is its own, separate store, populated by hand or by whatever
provisions a route.

``<pgdata>/server.crt``/``<pgdata>/server.key`` (or
``--ssl-cert-file``/``--ssl-key-file``) enable TLS; without them,
``hostssl`` HBA rules never match.

``METHOD`` may be followed by one more field, ``clientcert=verify-full``:
the TLS peer certificate's CN must equal the connecting role name exactly.
With ``METHOD`` ``trust`` the certificate check is the whole
authentication; with ``scram-sha-256`` both the certificate and the
password are required::

  hostssl  all  archiver_repl  10.0.0.0/8  scram-sha-256  clientcert=verify-full

This requires ``--ssl-ca-file`` (see `Options`_) to validate the client
certificate against; a rule using it with no usable CA file configured is
refused at startup.

``--insecure`` starts the server with no ``--pgdata`` at all -- no
``pg_walserver.ini``, no HBA file, no passwd file, no TLS -- accepting
any ``dbname`` with no authentication. With no routes configured, there
is nothing to reach for a real file behind it; this exists purely to
confirm the binary starts, listens, and answers the wire protocol
correctly (network and TLS reachability, ``IDENTIFY_SYSTEM``, ...)
before writing any real configuration. Never appropriate on a reachable
network.

Configuration reload
~~~~~~~~~~~~~~~~~~~~~

``serve`` writes its own pid to ``<pgdata>/pg_walserver.pid`` and parses
``pg_walserver.ini``/``pg_walserver_hba.conf`` once at startup.
``pg_walserver reload`` (see `Options`_ below) sends that pid
``SIGHUP``, which re-parses both files and installs them only if both
still parse cleanly, reconciling the embedded pull capturer set against
the new routes. The TLS certificate and key are not reloaded this way;
a rotated certificate needs a restart.

The wire protocol
~~~~~~~~~~~~~~~~~~

A connected client may issue any of the following, the same as against a
real PostgreSQL primary except where noted:

``IDENTIFY_SYSTEM``

  Reports the connected route's system identifier, current timeline, and
  WAL position.

``SHOW``

  Reports server and route parameters. A ``pg_walserver`` extension:
  also answers ``capture``, the connected route's own setting, ``pull``
  or ``none``.

``BASE_BACKUP``

  Streams a base backup of the connected route's storage.

``TIMELINE_HISTORY``

  Streams a timeline's ``.history`` file.

``CREATE_REPLICATION_SLOT`` / ``READ_REPLICATION_SLOT`` / ``DROP_REPLICATION_SLOT``

  Manage a replication slot on the connected route.

``START_REPLICATION``

  Streams WAL from a given position, exactly as a real standby's
  walreceiver expects.

``FETCH_FILE '<name>'``

  A ``pg_walserver`` extension: fetches one named WAL segment or
  ``.backup``/``.history`` file in a single round trip. Used by
  ``restore-wal``.

``CHECK_FILE`` / ``ARCHIVE_FILE``

  ``pg_walserver`` extensions, the push side of the protocol: check
  whether a file already matches what the route has, or push one. Used
  by ``archive-wal``.

See ``src/bin/pg_walserver/README.md`` for the wire protocol's full
design.

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
  (see `Access control`_) to have anything to validate against; starting
  with such a line configured and no usable CA file is refused.

--auth-timeout

  Absolute deadline, in seconds, for a connection to complete its startup
  packet, TLS handshake, HBA lookup, and SCRAM exchange. Defaults to
  ``30``.

Provisioning, archiving, and restoring each have their own manual page,
with worked, captured examples:

.. toctree::
   :maxdepth: 1

   pg_walserver_setup
   pg_walserver_scram_secret
   pg_walserver_fetch_systemid
   pg_walserver_basebackup
   pg_walserver_create_cert
   pg_walserver_archive_wal
   pg_walserver_restore_wal
   pg_walserver_archive_cleanup

``reload``
~~~~~~~~~~

Sends ``SIGHUP`` to the running ``pg_walserver serve`` instance whose pid
is recorded in ``<pgdata>/pg_walserver.pid``, the same shape as
``pg_ctl reload``::

  $ pg_walserver reload --pgdata /var/lib/archiver

Exits 0 once the signal was delivered. Exits nonzero, with a clear error,
if the pidfile is missing, stale, or unreadable.

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``. The
  pidfile read is ``<pgdata>/pg_walserver.pid``.

``ps``
~~~~~~

Shows ``serve``'s own process-level status: its pid, every supervised
embedded pull capturer child (pid, cluster, running or stopped, uptime,
restart count), and any in-flight one-time bootstrap base backup job.
Reads the same pidfile ``reload`` does to decide whether ``serve`` is
running at all; with none running, prints a clean message and exits 0,
never an error.

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

``ls``
~~~~~~

Lists pg_walserver's own on-disk footprint under ``--pgdata``: one row per
well-known bookkeeping file (``pg_walserver.ini``,
``pg_walserver_hba.conf``, ``pg_walserver_passwd``, ``server.crt``,
``server.key``, ``ca.crt``, ``pg_walserver.pid``), whether it exists, its
size, and its last-modified time. This is pg_walserver's own
configuration footprint, never the archived WAL or base backup data
itself -- see ``list`` below for that.

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

``status``
~~~~~~~~~~

Prints a short, scannable dashboard: running or not (a real liveness
check, not just "the pidfile exists"), pid, how many clusters are
configured and how many already have a base backup, how many embedded
pull capturers are running versus configured, and how many bootstrap
backups are still pending.

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

``list clusters``
~~~~~~~~~~~~~~~~~

::

  pg_walserver list clusters --pgdata <path> [--cluster <name>]

Lists every route configured in ``<pgdata>/pg_walserver.ini``: whether it
has a base backup, its ``capture`` setting, whether its embedded pull
capturer is currently running, and the WAL range it currently covers (the
start LSN from its latest base backup's own ``backup_label``, the end LSN
from the newest WAL segment actually present).

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

``list backups``
~~~~~~~~~~~~~~~~

::

  pg_walserver list backups --pgdata <path> [--cluster <name>]

Lists every base backup found under each matching route's own
``basebackups/`` directory: its label, when it was taken, its size on
disk, and whether it is the route's ``.latest``.

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

``list wal``
~~~~~~~~~~~~

::

  pg_walserver list wal --pgdata <path> [--cluster <name>] [--segments]

Prints aggregate WAL cache stats per route by default (segment count,
total bytes, oldest and newest segment, ``.history`` file count); with
``--segments``, lists every individual WAL/``.partial``/``.backup``/
``.history`` file instead.

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

--segments

  List every individual file instead of the default aggregate stats.

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
the primary's own ``archive_command`` has pushed at least one.

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

* :ref:`pg_walserver_setup`
* :ref:`pg_walserver_scram_secret`
* :ref:`pg_walserver_fetch_systemid`
* :ref:`pg_walserver_basebackup`
* :ref:`pg_walserver_create_cert`
* :ref:`pg_walserver_archive_wal`
* :ref:`pg_walserver_restore_wal`
* :ref:`pg_walserver_archive_cleanup`

``src/bin/pg_walserver/README.md`` in the source tree documents the wire
protocol, routing precedence, the embedded pull capturer, and the
push-side ``CHECK_FILE``/``ARCHIVE_FILE`` design in full.
