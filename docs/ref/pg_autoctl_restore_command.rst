.. _pg_autoctl_restore_command:

pg_autoctl restore command
===========================

pg_autoctl restore command - restore_command: fetch a WAL file from the archiver

Synopsis
--------

This command is meant to be used as Postgres ``restore_command``. It is a
thin wrapper around ``pg_walsender fetch-file`` (see :ref:`archiving_architecture`):
the real FETCH_FILE client code is not reimplemented here, only the
connection info an operator would otherwise have to write into
``restore_command`` by hand (host, port, ``<formation>/<group>`` route,
role name) is resolved on the caller's behalf.

::

  usage: pg_autoctl restore command  [ --pgdata --host --port --route --user ] <%f> <%p>

    --pgdata          path to this node's PGDATA, if any (defaults to $PGDATA)
    --host            archiver hostname (default: this node's own registered archiver)
    --port            archiver pg_walsender port (default: 6543)
    --route           "<formation>/<group>" to fetch from (default: this node's own)
    --user            replication role name (default: pgautofailover_replicator)
    --set-up          write --host/--port/--route/--user to a cache file and exit

Description
-----------

The connection info is resolved in this order:

1. any of ``--host``/``--port``/``--route``/``--user`` given on the command
   line, taken as-is (an explicit override always wins);

2. when this node is itself a registered pg_auto_failover node (a
   warm-standby-style replica created with ``pg_autoctl create postgres``
   or ``node run``, pointed at an archiver) and its monitor is reachable,
   the archiver registered for this node's own ``(formation, group)`` is
   looked up on the monitor (the same lookup ``pg_autoctl create postgres
   --from-archiver`` already relies on) -- nothing to configure by hand,
   and the answer is cached for step 3;

3. otherwise, the small cache file ``$PGDATA/pg_autoctl.restore-command``,
   written once by ``pg_autoctl restore command --set-up ...`` for a plain
   ad hoc replica with no pg_auto_failover node of its own (a PITR restore
   target, for instance), or refreshed automatically by a successful step 2
   lookup.

The password is never read from any of the above: like a hand-written
``restore_command``, it comes from ``PGPASSWORD`` or ``.pgpass``, resolved
by libpq itself when ``pg_walsender fetch-file`` connects.

The exit code is whatever ``pg_walsender fetch-file`` itself returns: 0 once
the file is placed at ``%p``, 1 otherwise so that Postgres retries.

New nodes are set up with::

  restore_command = 'pg_autoctl restore command %f %p'

A plain ad hoc replica sets the connection info up once::

  pg_autoctl restore command --set-up --pgdata PGDATA \
    --host archiver1 --port 6543 --route default/0 --user pgautofailover_replicator

and then uses the very same short ``restore_command`` line.
