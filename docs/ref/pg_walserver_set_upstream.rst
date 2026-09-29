.. _pg_walserver_set_upstream:

pg_walserver set-upstream
===========================

pg_walserver set-upstream - Point an already-registered cluster at a new upstream

Synopsis
--------

::

  pg_walserver set-upstream cluster <name> --pgdata <path>
      [--config-file <path>] --pguri <conninfo> [--force-basebackup]

pg_walserver set-upstream cluster
------------------------------------

Points an already-registered cluster at a new upstream -- typically
after a failover promotes a different node. Reloads an already-running
``serve`` for the same ``--pgdata`` immediately afterward: its own
reconciliation already detects the ``upstream`` change on reload and
restarts this route's embedded receivewal worker against the new one,
so there is no separate step needed to "move" it.

Options
^^^^^^^

<name>

  The cluster's own name, given positionally (never a flag).

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--pguri

  The new libpq connection string, replacing the route's own
  ``upstream`` property.

--force-basebackup

  Also take a fresh base backup against the new upstream right away,
  rather than waiting for the next scheduled
  :ref:`pg_walserver_basebackup`.

Example
^^^^^^^

Re-point ``third`` at the new primary after a failover, taking a fresh
base backup against it right away::

  archive$ PGPASSWORD=s3kr3t pg_walserver set-upstream cluster third \
      --pgdata /var/lib/archiver \
      --pguri "host=new_primary port=5432 user=archiver_repl sslmode=disable" \
      --force-basebackup
  22:57:31 82 INFO  Set "upstream = host=new_primary port=5432 user=archiver_repl sslmode=disable" for route "third" in "/var/lib/archiver/pg_walserver.ini"
  22:57:31 82 INFO  Connecting to new_primary:5432 as "archiver_repl" to fetch the system identifier
  22:57:31 82 INFO  "/var/lib/archiver/third" already has the correct system identifier (7690723939657998375)
  22:57:31 82 INFO  "/var/lib/archiver/third" already has the correct upstream Postgres version (170011)
  22:57:31 82 INFO  Route "third"'s own upstream is now "host=new_primary port=5432 user=archiver_repl sslmode=disable"
  22:57:31 82 INFO  Using pg_basebackup for PostgreSQL 17 found at its well-known Debian/Ubuntu path "/usr/lib/postgresql/17/bin/pg_basebackup"
  22:57:31 82 INFO  Taking a base backup of new_primary:5432 into "/var/lib/archiver/third/basebackups/basebackup-20260928T225731Z"
  22:57:33 82 INFO  base backup completed
  22:57:33 82 INFO  Base backup "basebackup-20260928T225731Z" is now the latest for "/var/lib/archiver/third"
  22:57:33 82 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_register`
* :ref:`pg_walserver_drop`
* :ref:`pg_walserver_basebackup`
