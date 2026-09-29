.. _pg_walserver:

pg_walserver
============

pg_walserver - standalone PostgreSQL replication-protocol server

.. toctree::
   :hidden:
   :maxdepth: 1

   pg_walserver_serve
   pg_walserver_setup
   pg_walserver_cluster
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

Serving ``pg_basebackup``, ``pg_receivewal``, and a real standby set up
with a ``primary_conninfo`` directly out of a directory tree of WAL
segments and base backups, with no live ``postmaster`` behind it, needs
something that speaks the PostgreSQL replication wire protocol well
enough for that: this is what ``pg_walserver`` does. It is not part of
``pg_autoctl``'s own process supervision: it is started and stopped on
its own.

This is ``pg_walserver help``'s own output, verbatim -- the whole
sub-command tree, including every group's own nested sub-commands, in a
single call; ``pg_walserver --help`` alone only shows the top-level
list (the rows without a nested tree of their own below)::

  pg_walserver
    serve            Run the pg_walserver accept loop
    scram-secret     Print one pg_walserver_passwd line for a user
    fetch-systemid   Fetch a route's upstream system identifier
    basebackup       Take a base backup of a route's upstream
    setup            Configure pg_walserver itself (port, TLS, auth-timeout)
  + cluster          Register, drop, list, or re-point the clusters this pg_walserver archives
    create-cert      Create a self-signed TLS certificate for --pgdata
    archive-wal      Push one WAL/.backup file into a pg_walserver route (archive_command)
    restore-wal      Fetch one WAL/.backup file from a pg_walserver route (restore_command)
    archive-cleanup  Remove WAL/base backups this route no longer needs to keep (operator/cron-driven, never automatic)
    reload           Ask a running pg_walserver to reload its configuration
    stop             Stop a running pg_walserver cleanly
    ps               Show pg_walserver serve's own process-level status (pid, receivewal workers, bootstrap jobs)
    ls               Per-cluster storage summary: base backups, WAL, disk usage
    status           Show a short pg_walserver status dashboard
  + list             List clusters, base backups, or WAL cache contents
    help             Print this whole sub-command tree at once

  pg_walserver cluster
    register      Register (or validate) one cluster this pg_walserver archives
    drop          Drop one cluster's registration (never its own data, unless --purge)
    list          List every cluster this pg_walserver has registered
    set-upstream  Point an already-registered cluster at a new upstream (e.g. after a failover)

``list`` is itself three further sub-commands (``pg_walserver list
--help``, also verbatim)::

  pg_walserver list: List clusters, base backups, or WAL cache contents

  Available commands:
    pg_walserver list
      clusters  List every route, its backup/receivewal status, and the WAL range it covers
      backups   List base backups per cluster (label, size, which is .latest)
      wal       List WAL cache aggregate stats per cluster, or every file with --segments

Description
-----------

Operating a PostgreSQL service in production requires a fully compliant
`archiving`__ story in place: it is the foundation of disaster recovery
and data durability in the event of a crash. PostgreSQL itself does not
provide an archiving implementation, only a well-specified contract for
one (``archive_command``/``restore_command``, base backups, timelines).
External solutions exist to fill that gap, but none of them speak the
PostgreSQL replication protocol, which means that when the worst happens,
none of PostgreSQL's own tools -- ``pg_basebackup``, ``pg_receivewal``, a
real standby's ``primary_conninfo`` -- can talk to the archive to rebuild
a node.

__ https://www.postgresql.org/docs/current/continuous-archiving.html

That gap needs an archiving server that speaks the replication protocol
and implements PostgreSQL's own archiving contract in full:
``pg_walserver``. It combines streaming (the embedded receivewal
worker, for efficiency) with ``archive_command`` (for robustness)
rather than requiring one or the other.

Every ``pg_walserver`` command reads its own bookkeeping -- ``pg_walserver.ini``,
the HBA file, the SCRAM verifier file, the TLS certificate, the pid file --
from a directory given by ``--pgdata``, or the ``PGDATA`` environment
variable as a default, exactly like ``pg_autoctl``'s own ``--pgdata``/
``PGDATA``. This is ``pg_walserver``'s own directory, not the
PostgreSQL data directory of any cluster it archives: those are each a
route's own, separate ``path`` (below), never confused with this one.
A route's ``path`` itself defaults to ``<pgdata>/<cluster>`` and rarely
needs to be set explicitly -- see :ref:`pg_walserver_cluster`'s own
``--path``.

Archiving one cluster
~~~~~~~~~~~~~~~~~~~~~

A cluster's data is safe once three things are on record: its system
identifier, a base backup to restore from, and continuous WAL capture
from that point on. ``pg_walserver`` builds all three from a single
``cluster register`` call followed by a running ``serve``:

A single ``pg_walserver cluster register`` call handles connecting to
the upstream PostgreSQL instance, recording its system identifier, and
writing the route into ``pg_walserver.ini``. ``pg_walserver serve``
picks the route up -- at startup, and again after every ``pg_walserver
reload`` -- takes its first base backup automatically, and starts
capturing WAL continuously into the route's own storage (an embedded,
supervised ``pg_receivewal``, on by default). Running ``cluster
register`` once and starting (or reloading) ``serve`` is the whole
sequence; no separate step takes that first backup.

Every production deployment must also add
``archive_command = 'pg_walserver archive-wal ...'`` on the primary, as
a defense-in-depth backstop alongside continuous capture -- not an
optional extra, since the embedded receivewal worker alone cannot
survive every gap a real ``archive_command`` closes (a timeline switch
during a promotion, most notably). Keeping a route's
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

Archiving several PostgreSQL clusters from the same ``pg_walserver``
installation is possible thanks to "virtual host" routing, with a
routing setup managed in ``pg_walserver.ini``. That file registers each
cluster by name, under a route (an operator-chosen key, carrying no
filesystem meaning of its own) that maps to its own storage root: the
WAL and base backups of one entire PostgreSQL cluster -- everything
under that cluster's own data directory, never a single database
within it -- WAL archiving is inherently a whole-cluster concept.

The way to select a route when connecting -- with ``pg_basebackup``,
``psql``, or any other replication client -- is the ``dbname`` key of
the connection string, inspired by how PostgreSQL itself uses
`dbname=replication`__ to activate the replication protocol: a real
physical replication connection doesn't select an actual database
either, ``dbname`` there is just a formality. ``pg_walserver`` puts
that same, otherwise unused, field to work as a real routing key
instead: it looks the given ``dbname`` up in ``pg_walserver.ini``, and
never validates it against a real database in the archived cluster.
One entry, ``*``, acts as a catch-all -- exactly like PgBouncer's own
wildcard database -- matching any ``dbname`` with no route of its own.

__ https://www.postgresql.org/docs/current/protocol-replication.html

A client that cannot set its own ``dbname`` -- a real standby's
``primary_conninfo``, most notably, which only ever sends the literal
``dbname=replication`` -- is matched to its route a second way instead,
by the TLS SNI hostname it connected with. Every connection tries
``dbname`` first, which is enough on its own for a single cluster.

A single cluster needs only ``dbname``, set to the route's own key::

  archive$ pg_walserver cluster register mycluster --pgdata /var/lib/archiver \
      --pguri "postgres://archiver_repl@primary/?sslmode=require"

  standby$ psql "host=archive port=6543 dbname=mycluster user=archiver_repl sslmode=require" \
      -c "IDENTIFY_SYSTEM"

More than one cluster behind the same ``pg_walserver`` instance needs a
second, independent way to tell them apart: TLS SNI virtual-hosts each
one behind its own ``--hostname``::

  archive$ pg_walserver cluster register mycluster --pgdata /var/lib/archiver \
      --pguri "postgres://archiver_repl@primary:5432/?sslmode=require" \
      --hostname mycluster.archive.example.com

  archive$ pg_walserver cluster register another --pgdata /var/lib/archiver \
      --pguri "postgres://archiver_repl@primary2:5432/?sslmode=require" \
      --hostname another.archive.example.com

  standby$ cat >> /var/lib/postgres/standby/postgresql.auto.conf <<EOF
  primary_conninfo = 'host=mycluster.archive.example.com port=6543 user=archiver_repl password=s3kr3t sslmode=require'
  EOF

A self-signed certificate for ``--pgdata`` is created automatically by
``cluster register`` the moment a second named route needs one, and
``pg_walserver`` requires TLS from that point on. See
:ref:`pg_walserver_serve`'s "Routing more than one cluster by name: TLS
SNI" for the full mechanism, its DNS prerequisite, and why a real
standby's ``primary_conninfo`` needs it.

Access control
~~~~~~~~~~~~~~

Each connecting peer is checked against ``<pgdata>/pg_walserver_hba.conf``,
one rule per line (``TYPE ROUTE USER ADDRESS METHOD``, first match
wins); a missing, oversize, or malformed file rejects every connection
outright. Three ``METHOD`` values are supported:

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

A ``scram-sha-256`` verifier has to come from somewhere: PostgreSQL
keeps its own, in the ``pg_authid`` catalog, one entry per role.
``pg_walserver`` is not PostgreSQL, and has no role system underneath
it, so it keeps a separate credential store instead,
``<pgdata>/pg_walserver_passwd``, one SCRAM-SHA-256 verifier per line,
produced with ``pg_walserver scram-secret`` and populated by hand or by
whatever provisions a route.

TLS is enabled simply by the presence of
``<pgdata>/server.crt``/``<pgdata>/server.key`` (or
``--ssl-cert-file``/``--ssl-key-file``); without them, ``hostssl`` HBA
rules never match.

An optional sixth field after ``METHOD``, ``clientcert=verify-full``,
adds one more check: the TLS peer certificate's CN must equal the
connecting role name exactly. With ``METHOD`` ``trust`` the certificate
check is the whole authentication; with ``scram-sha-256`` both the
certificate and the password are required::

  hostssl  all  archiver_repl  10.0.0.0/8  scram-sha-256  clientcert=verify-full

Validating that client certificate needs ``--ssl-ca-file`` (see
:ref:`pg_walserver_serve`); a rule using it with no usable CA file
configured is refused at startup.

For manual testing, with no ``--pgdata`` at all -- no
``pg_walserver.ini``, no HBA file, no passwd file, no TLS -- pass
``--insecure`` to accept any ``dbname`` with no authentication, purely
to confirm the binary starts, listens, and answers the wire protocol
correctly (network and TLS reachability, ``IDENTIFY_SYSTEM``, ...)
before writing any real configuration. With no routes configured, there
is nothing to reach for a real file behind it. Never appropriate on a
reachable network.

Configuration reload
~~~~~~~~~~~~~~~~~~~~~

Its own pid is written to ``<pgdata>/pg_walserver.pid`` by ``serve`` at
startup, at the same time it parses
``pg_walserver.ini``/``pg_walserver_hba.conf``, once.
:ref:`pg_walserver_reload` sends that pid ``SIGHUP``, which re-parses
both files and installs them only if both still parse cleanly,
reconciling the embedded receivewal worker set against the new routes. The
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
  also answers ``receivewal``, the connected route's own setting,
  ``pull`` or ``none``.

``BASE_BACKUP``

  Streams a base backup of the connected route's storage.

``TIMELINE_HISTORY``

  Streams a timeline's ``.history`` file.

``CREATE_REPLICATION_SLOT`` / ``READ_REPLICATION_SLOT`` / ``DROP_REPLICATION_SLOT``

  Manage a replication slot on the connected route.

``START_REPLICATION``

  Streams WAL from a given position, exactly as a real standby set up
  with ``primary_conninfo`` expects.

``FETCH_FILE '<name>'``

  A ``pg_walserver`` extension: fetches one named WAL segment or
  ``.backup``/``.history`` file in a single round trip. Used by
  ``restore-wal``.

``CHECK_FILE`` / ``ARCHIVE_FILE``

  ``pg_walserver`` extensions, the push side of the protocol: check
  whether a file already matches what the route has, or push one.
  ``CHECK_FILE`` also reports whether the connected route's own embedded
  receivewal worker has already streamed past the file in question -- a
  hole it can never retroactively fill, typically a timeline switch --
  recommending an immediate ``ARCHIVE_FILE`` push over waiting. Used by
  ``archive-wal``.

See ``src/bin/pg_walserver/README.md`` for the wire protocol's full
design.

Examples
--------

Create a SCRAM secret for the replication role, from a password given in
the environment::

  $ PGPASSWORD='s3kr3t' pg_walserver scram-secret --user pgautofailover_replicator
  pgautofailover_replicator:SCRAM-SHA-256$4096:...

Run the server against an existing ``--pgdata`` directory. No
sub-command is ever implicit -- ``serve`` is always given explicitly::

  $ pg_walserver serve --pgdata /var/lib/archiver

Run the server with no authentication, for manual testing only::

  $ pg_walserver serve --insecure --port 6543

Registering a node and starting a PITR session, in shape
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The shortest possible path from nothing to a point-in-time restore --
register one PostgreSQL node, start serving, then restore from it.
Every command's own real output, and the reasoning behind each step,
are in :ref:`pg_walserver_serve`'s own "A complete standalone example"
walkthrough; this is only the shape of it::

  # on the archive host: register the node (creating a certificate right
  # away, --ssl-self-signed), then serve on the default port, 6543
  archive$ export PGDATA=/var/lib/archiver
  archive$ PGPASSWORD='s3kr3t' pg_walserver cluster register mycluster \
      --pguri "postgres://archiver_repl@primary:5432/?sslmode=require" \
      --ssl-self-signed --hostname archive
  archive$ pg_walserver serve &

  # elsewhere, once a base backup exists: restore to a point in time
  restore$ export WALSERVER_PGURI='postgres://archiver_repl@archive:6543/mycluster?sslmode=require'
  restore$ export PITR_PGDATA=/var/lib/postgres/pitr
  restore$ PGPASSWORD='s3kr3t' pg_basebackup -d "${WALSERVER_PGURI}" -D "${PITR_PGDATA}"
  restore$ cat >> ${PITR_PGDATA}/postgresql.auto.conf <<EOF
  restore_command = 'PGPASSWORD=s3kr3t pg_walserver restore-wal %f %p --cluster mycluster --host archive --port 6543 --user archiver_repl --sslmode require'
  recovery_target_time = '2026-09-27 11:30:00+00'
  EOF
  restore$ touch ${PITR_PGDATA}/recovery.signal
  restore$ pg_ctl -D "${PITR_PGDATA}" start

See :ref:`pg_walserver_serve` for the full standalone walkthrough
(create a route, configure access, start the server, add
``archive_command``, take a PITR restore or a live standby, and keep
the archive from growing forever) with every command's own real,
unedited output.
