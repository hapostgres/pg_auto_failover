.. _pg_walsender:

pg_walsender
============

``pg_walsender`` is the archiver's own server-side implementation of a
subset of the PostgreSQL replication protocol -- it speaks enough of it
that a real, unmodified ``pg_basebackup``, a real standby's own
``primary_conninfo``, or a ``restore_command`` fetch can all connect to
an archiver directly and get what they ask for, with no custom client
needed. See :ref:`archiving_architecture` for the full picture of what it
serves and why it exists as a standalone binary rather than living inside
``pg_autoctl`` itself.

Two modes of operation are supported:

- **Server mode** (the default): runs the accept loop, one connection per
  forked child, until stopped.
- **``scram-secret`` mode**: prints one ``archiver-passwd`` line
  (``<user>:SCRAM-SHA-256$...``) for the password in ``PGPASSWORD``.
- **``fetch-file`` mode**: a one-shot client that fetches a single named
  file with the ``FETCH_FILE`` replication command and exits -- what a node's own
  ``restore_command`` shells out to, the same way this project already
  shells out to real ``pg_receivewal``/``pg_basebackup`` elsewhere.

Synopsis
--------

::

  pg_walsender --port <port> [--pgdata <path> | --insecure]
               [--auth-timeout <seconds>]
               [--ssl-cert-file <path> --ssl-key-file <path>]
  PGPASSWORD=... pg_walsender scram-secret [--user <name>]

  pg_walsender fetch-file --host <host> --port <port> \
               --route <formation>/<group> \
               --filename <name> --output <path>

Description
-----------

``pg_walsender`` is exec'd and supervised by :ref:`pg_autoctl_archiver_serve`
(itself normally started as part of :ref:`pg_autoctl_run` against an
archiver, not invoked directly) -- it is not meant to be typed by hand in
production, but is fully runnable and testable on its own against a real
``psql``, ``pg_basebackup``, or ``pg_receivewal``.

It never connects to the monitor, on purpose: an archiver exists to keep
serving already-captured data even when the monitor it would otherwise
depend on is unreachable, and staying free of that dependency also keeps
this binary small, standalone, and independently testable -- no monitor
or database needed to exercise it. Everything it needs to serve a
connection -- which base backup is current, a group's system identifier,
the current WAL position, and the formation/group-to-storage-path mapping
-- is read from small local files pg_autoctl's own archiver processes
maintain; see :ref:`archiving_architecture`'s "Keeping local files
current" section for exactly which file carries what.

Options
-------

--port

  Port to listen on, in server mode. Defaults to ``6543``.

--ssl-cert-file, --ssl-key-file

  The TLS certificate and private key to serve. Default to
  ``<pgdata>/server.crt`` and ``<pgdata>/server.key``; without a usable
  pair the server answers ``N`` to ``SSLRequest`` and ``hostssl`` HBA lines
  never match. The key must not be accessible to group or others (PostgreSQL's
  rule).

--pgdata

  The archiver's own top-level storage root -- the same value given as
  ``--pgdata`` to ``pg_autoctl create archiver`` -- defaulting to the
  ``PGDATA`` environment variable. Everything the server needs is derived
  from it:

  - ``<pgdata>/archiver-routes.ini``: the routes file mapping
    ``"<formation>/<group>"`` (matched against the incoming connection's
    dbname) to its storage path. It is written by
    :ref:`pg_autoctl_archiver`'s reconciler process and is not meant to be
    hand-edited.
  - ``<pgdata>/archiver-hba.conf`` and ``<pgdata>/archiver-passwd``, which
    decide who may connect (see :ref:`archiving_architecture`). The HBA
    file is created with a default line that trusts the monitor's node
    list when it is missing.
  - ``<pgdata>/archiver-monitor.uri``, and a local ``archiver-nodes.list``
    per route: the copy of the monitor's node list that the ``monitor``
    HBA address is checked against, so that connections never depend on
    the monitor being up.
  - ``<route dir>/archiver-walsegsize``: the WAL segment size of the
    route's cluster, answered to ``SHOW wal_segment_size``.

  Omitting both ``--pgdata`` and ``PGDATA`` is refused unless ``--insecure``
  is given (see below).

--insecure

  Allow running without ``--pgdata``: no HBA file, no authentication, any
  dbname accepted. Only meant for manual, standalone testing; without this
  flag, ``pg_walsender`` exits with an error rather than silently serving
  everybody.

--auth-timeout

  Number of seconds a connection gets to complete startup, TLS negotiation
  and authentication (default ``30``). This is an absolute deadline from
  accept(), not an idle timeout: a client that trickles bytes gets no more
  time than a silent one. Pre-authentication messages larger than a small
  fixed bound are rejected and the connection closed, so an unauthenticated
  client cannot make the server buffer arbitrary amounts of data.

``fetch-file`` mode options:

--host

  Hostname or IP address of the ``pg_walsender`` to fetch from.

--port

  Port of the ``pg_walsender`` to fetch from.

--route

  ``<formation>/<group>`` identifying which membership to fetch the file
  from, when that archiver serves more than one.

--user

  The role to connect as (default ``pgautofailover_replicator``). The
  password comes from ``PGPASSWORD`` and TLS from ``PGSSLMODE`` (default
  ``prefer``), as with libpq.

--user

  The role to connect as (default ``pgautofailover_replicator``). The
  password comes from ``PGPASSWORD`` and TLS from ``PGSSLMODE``
  (default ``prefer``), as with libpq.

--filename

  Name of the file to fetch (a WAL segment or timeline history file).

--output

  Local path to write the fetched file to.

See Also
--------

:ref:`pg_autoctl_archiver_serve` supervises this binary as part of a
running archiver.

:ref:`archiving_architecture` covers the full process model, what this
binary serves, and the local files it reads to do so.

Access control behaviour
------------------------

- A malformed line in ``archiver-hba.conf`` makes the server fail closed:
  every connection is rejected until the file is fixed. The file is
  re-read for each new connection, no reload is needed.
- An unknown route (dbname that is not a known ``<formation>/<group>``)
  gets exactly the same generic rejection as a missing HBA entry, so an
  unauthenticated client cannot probe which formations exist. Only after a
  successful authentication is ``database does not exist`` (SQLSTATE
  ``3D000``) reported.
- ``FETCH_FILE`` is an ordinary replication-connection command, subject to
  the same authentication. It only serves WAL segment names
  (``[0-9A-F]{24}``) and timeline ``.history`` files; every other name,
  including ``archiver-hba.conf``, ``..`` paths and dot-files, is refused.
- ``CREATE_REPLICATION_SLOT`` is capped at 64 slots per route;
  ``DROP_REPLICATION_SLOT`` releases them.
