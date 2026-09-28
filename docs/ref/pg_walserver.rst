.. _pg_walserver:

pg_walserver
============

pg_walserver - standalone PostgreSQL replication-protocol server

.. toctree::
   :hidden:
   :maxdepth: 1

   pg_walserver_serve
   pg_walserver_setup
   pg_walserver_scram_secret
   pg_walserver_fetch_systemid
   pg_walserver_basebackup
   pg_walserver_create_cert
   pg_walserver_archive_wal
   pg_walserver_restore_wal
   pg_walserver_archive_cleanup
   pg_walserver_reload
   pg_walserver_ps
   pg_walserver_ls
   pg_walserver_status
   pg_walserver_list

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

Each of these has its own manual page, in the sidebar.

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
client can reach, and :ref:`pg_walserver_serve`'s "A complete
standalone example" for the full sequence.

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
``pg_walserver`` requires TLS from that point on. See
:ref:`pg_walserver_serve`'s "Routing more than one cluster by name: TLS
SNI" for the full mechanism, its DNS prerequisite, and why a real
standby's walreceiver needs it.

``dbname`` here is a routing key, not a database selection: it never
corresponds to an actual database inside the archived cluster, and is
never validated against one. This is not a ``pg_walserver`` invention --
in a real PostgreSQL `physical replication connection`__, ``dbname`` is
already a formality rather than a real database name, since a physical
replication connection isn't attached to any one database in the first
place. ``pg_walserver`` simply puts that otherwise-unused field to work
as its own routing key. One key, ``*``, is a PgBouncer-style catch-all
matching any ``dbname`` with no route of its own.

__ https://www.postgresql.org/docs/current/protocol-replication.html

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

This requires ``--ssl-ca-file`` (see :ref:`pg_walserver_serve`) to
validate the client certificate against; a rule using it with no usable
CA file configured is refused at startup.

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
:ref:`pg_walserver_reload` sends that pid ``SIGHUP``, which re-parses
both files and installs them only if both still parse cleanly,
reconciling the embedded pull capturer set against the new routes. The
TLS certificate and key are not reloaded this way; a rotated
certificate needs a restart.

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

See :ref:`pg_walserver_serve` for the full standalone walkthrough
(create a route, configure access, start the server, add
``archive_command``, take a PITR restore or a live standby, and keep
the archive from growing forever).
