.. _pg_walserver_fetch_systemid:

pg_walserver fetch-systemid
============================

pg_walserver fetch-systemid - Fetch a route's upstream system identifier

Synopsis
--------

::

  pg_walserver fetch-systemid --pgdata <path> --cluster <name>
      [--path <dir>] [--upstream <conninfo> | --host <host> [--port <port>] [--user <name>]]
      [--force]

Connects to a route's upstream, fetches its system identifier, and
writes it to ``<path>/pg_walserver_systemid``. Refuses to overwrite an
already-recorded, different identifier unless ``--force``. ``setup``
calls this itself; running it directly is for checking or repairing a
route's own recorded identifier without touching anything else about
it.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  The cluster name to fetch for, looked up in ``pg_walserver.ini``.

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
on record is a no-op::

  archive$ pg_walserver fetch-systemid --pgdata /var/lib/archiver --cluster mycluster \
      --upstream "host=primary user=archiver_repl sslmode=require"
  INFO  Connecting to primary:5432 as "archiver_repl" to fetch the
        system identifier
  INFO  "/var/lib/archiver/mycluster" already has the correct system
        identifier (7690580638048137639)

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_setup`
