.. _pg_walserver_fetch_systemid:

pg_walserver fetch-systemid
============================

pg_walserver fetch-systemid - Fetch a route's upstream system identifier

Synopsis
--------

::

  pg_walserver fetch-systemid --pgdata <path> [--config <path>] --cluster <name>
      [--path <dir>] [--upstream <conninfo> | --host <host> [--port <port>] [--user <name>]]
      [--force]

Connects to a route's upstream, fetches its system identifier and its
current major version, and writes them to
``<path>/pg_walserver_systemid``/``<path>/pg_walserver_pgversion`` --
the latter is what later picks the right ``pg_basebackup`` client for
this route, see :ref:`pg_walserver_basebackup`. Refuses to overwrite
an already-recorded, different identifier unless ``--force``.
``pg_walserver cluster register`` calls this itself; running it
directly is for checking or repairing a route's own recorded
identifier without touching anything else about it.

Options
-------

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--cluster

  The cluster name to fetch for, looked up in the config file.

--path

  The route's own directory. Overrides the route's own ``path``
  property.

--upstream

  A libpq connection string to connect with. Overrides the route's own
  ``upstream`` property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--force

  Overwrite an already-recorded, different system identifier.

Examples
--------

Running it again for a route that already has the correct identifier
and version on record is a no-op::

  archive$ PGPASSWORD=s3kr3t pg_walserver fetch-systemid --cluster mycluster \
      --upstream "host=primary port=5534 user=archiver_repl sslmode=require"
  21:12:33 2587439 INFO  Connecting to primary:5534 as "archiver_repl" to fetch the system identifier
  21:12:33 2587439 INFO  "/var/lib/archiver/mycluster" already has the correct system identifier (7690676421909321516)
  21:12:33 2587439 INFO  "/var/lib/archiver/mycluster" already has the correct upstream Postgres version (170011)

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_cluster`
