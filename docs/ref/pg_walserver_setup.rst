.. _pg_walserver_setup:

pg_walserver setup
===================

pg_walserver setup - Create or validate one pg_walserver.ini route

Synopsis
--------

::

  pg_walserver setup --pgdata <path> --cluster <name> --path <dir>
      --upstream <conninfo> | --host <host> [--port <port>] [--user <name>]
      [--hostname <name>] [--capture pull|none] [--force]

Writes or validates one ``pg_walserver.ini`` route and fetches its
upstream system identifier. It never takes a base backup itself:
instead, it reloads an already-running ``pg_walserver serve`` for the
same ``--pgdata``, if there is one, so the new or changed route takes
effect immediately (with no server running, the config just written
takes effect the next time ``serve`` starts). Either way, ``serve``
itself takes the route's first base backup automatically, once, the
next time it starts or reloads -- see :ref:`pg_walserver`'s "Archiving
one cluster" section.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  The cluster name to create or validate.

--path

  The route's own directory, created if missing.

--upstream

  A libpq connection string, written into the route's own ``upstream``
  property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--hostname

  The route's own TLS SNI hostname, written into its ``hostname``
  property. Creates a self-signed certificate for ``--pgdata``
  automatically the first time a second named route needs one.

--capture

  ``pull`` (the default) or ``none``. ``pull`` writes ``capture = pull``
  into the route, starting an embedded, supervised ``pg_receivewal``
  child against ``upstream`` once ``serve`` runs. ``none`` is equivalent
  to ``--no-capture``.

--no-capture

  Equivalent to ``--capture none``.

--force

  Overwrite an existing route's ``path``/``upstream`` instead of
  refusing.

Examples
--------

Create a route for a new cluster, no server running yet::

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

Run it again, for a second route, with ``serve`` already running::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup \
      --pgdata /var/lib/archiver --cluster another \
      --path /var/lib/archiver/another \
      --upstream "host=primary2 user=archiver_repl sslmode=require"
  INFO  Added route "another" (path "/var/lib/archiver/another") to
        "/var/lib/archiver/pg_walserver.ini"
  INFO  Connecting to primary2:5432 as "archiver_repl" to fetch the
        system identifier
  INFO  Wrote system identifier 7690580638048199999 to
        "/var/lib/archiver/another/pg_walserver_systemid"
  INFO  setup complete: route "another" is ready ...
  INFO  Sent SIGHUP to pg_walserver pid 25671

Related commands
----------------

.. toctree::
   :hidden:
   :maxdepth: 1

   pg_walserver_scram_secret
   pg_walserver_create_cert

* :ref:`pg_walserver_scram_secret`
* :ref:`pg_walserver_create_cert`

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_basebackup`
* :ref:`pg_walserver_fetch_systemid`
