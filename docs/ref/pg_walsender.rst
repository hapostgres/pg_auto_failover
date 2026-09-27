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

- ``<pgdata>/archiver-routes.ini`` maps each ``<formation>/<group>`` this
  instance serves (the connection's ``dbname``) to that membership's own
  local storage root: WAL cache, base backups, and a handful of small
  bookkeeping files;

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
