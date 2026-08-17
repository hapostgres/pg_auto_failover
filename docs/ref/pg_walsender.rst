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
- **``fetch-file`` mode**: a one-shot client that fetches a single named
  file over the ``FETCH_FILE`` side-channel and exits -- what a node's own
  ``restore_command`` shells out to, the same way this project already
  shells out to real ``pg_receivewal``/``pg_basebackup`` elsewhere.

Synopsis
--------

::

  pg_walsender --port <port> [--pgdata <path>]

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

--pgdata

  The archiver's own top-level storage root -- the same value given as
  ``--pgdata`` to ``pg_autoctl create archiver`` -- defaulting to the
  ``PGDATA`` environment variable. Used to derive
  ``<pgdata>/archiver-routes.ini``, the routes file mapping
  ``"<formation>/<group>"`` (matched against the incoming connection's
  dbname) to ``{ path, allowed_hosts }``. Written by
  :ref:`pg_autoctl_archiver`'s own reconciler process, not meant to be
  hand-edited. Omit both ``--pgdata`` and ``PGDATA`` only for manual,
  standalone testing (accepts any dbname, no host restriction).

``fetch-file`` mode options:

--host

  Hostname or IP address of the ``pg_walsender`` to fetch from.

--port

  Port of the ``pg_walsender`` to fetch from.

--route

  ``<formation>/<group>`` identifying which membership to fetch the file
  from, when that archiver serves more than one.

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
