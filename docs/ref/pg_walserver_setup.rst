.. _pg_walserver_setup:

pg_walserver setup
===================

pg_walserver setup - Create or validate one pg_walserver.ini route

Synopsis
--------

::

  pg_walserver setup --pgdata <path> --cluster <name> [--path <dir>]
      --upstream <conninfo> | --host <host> [--port <port>] [--user <name>]
      [--hostname <name>] [--receivewal pull|none] [--ssl-self-signed]
      [--force]

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

  The route's own directory, created if missing. Defaults to
  ``<pgdata>/<cluster>``; rarely needs to be given explicitly.

--upstream

  A libpq connection string, keyword/value or ``postgres://`` URI,
  written into the route's own ``upstream`` property.

--host, --port, --user

  Override individual connection parameters. Default port ``5432``,
  default user ``pgautofailover_replicator``.

--hostname

  The route's own TLS SNI hostname, written into its ``hostname``
  property. Creates a self-signed certificate for ``--pgdata``
  automatically the first time a second named route needs one (or
  right away, with ``--ssl-self-signed`` below); also that
  certificate's own CN, when one is created.

--receivewal

  ``pull`` (the default) or ``none``. ``pull`` writes ``receivewal = pull``
  into the route, starting an embedded, supervised ``pg_receivewal``
  child against ``upstream`` once ``serve`` runs. ``none`` is equivalent
  to ``--no-receivewal``.

--no-receivewal

  Equivalent to ``--receivewal none``.

--ssl-self-signed

  Create a self-signed certificate for ``--pgdata`` right away, whether
  or not this is the only route -- skips a separate
  :ref:`pg_walserver_create_cert` call entirely. An already-existing
  certificate is left untouched. Without this flag, a certificate is
  still created automatically, but only once a second route makes TLS
  mandatory (see :ref:`pg_walserver`'s "Routing" section).

--force

  Overwrite an existing route's ``path``/``upstream`` instead of
  refusing.

Examples
--------

Create a route for a new cluster, no ``--path`` given (it defaults to
``<pgdata>/<cluster>``), ``--upstream`` as a ``postgres://`` URI,
``--ssl-self-signed`` creating a certificate right away, no server
running yet::

  archive$ export PGDATA=/var/lib/archiver
  archive$ PGPASSWORD=s3kr3t pg_walserver setup --cluster mycluster \
      --upstream "postgres://archiver_repl@primary:5535/?sslmode=require" \
      --ssl-self-signed --hostname archive
  21:38:22 2660205 ERROR Failed to open "/var/lib/archiver/pg_walserver.ini": No such file or directory
  21:38:22 2660205 ERROR Failed to read routes file "/var/lib/archiver/pg_walserver.ini"
  21:38:22 2660205 INFO  Added route "mycluster" (path "/var/lib/archiver/mycluster") to "/var/lib/archiver/pg_walserver.ini"
  21:38:22 2660205 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=archive"
  21:38:22 2660205 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=archive) -- replace it with a real one before running on a reachable network
  21:38:22 2660205 INFO  Connecting to primary:5535 as "archiver_repl" to fetch the system identifier
  21:38:22 2660205 INFO  Wrote system identifier 7690701918151237972 to "/var/lib/archiver/mycluster/pg_walserver_systemid"
  21:38:22 2660205 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/mycluster/pg_walserver_pgversion"
  21:38:22 2660205 INFO  setup complete: route "mycluster" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  21:38:22 2660205 INFO  No running "pg_walserver serve" found at "/var/lib/archiver/pg_walserver.pid": the route just written will take effect the next time "serve" starts

The two ``ERROR`` lines are harmless and expected on a first run:
``pg_walserver.ini`` does not exist yet the moment ``setup`` tries to
read it, before writing its very first route into it.

Run it again, for a third route, with ``serve`` already running and
two routes already configured -- ``setup`` reloads it immediately
instead of waiting for a restart, and, this now being the ini's third
route, warns that this one has no ``--hostname`` of its own::

  archive$ PGPASSWORD=s3kr3t pg_walserver setup --cluster third \
      --upstream "host=primary port=5534 user=archiver_repl sslmode=require"
  21:11:36 2585325 INFO  Added route "third" (path "/var/lib/archiver/third") to "/var/lib/archiver/pg_walserver.ini"
  21:11:36 2585325 INFO  "/var/lib/archiver/pg_walserver.ini" now has 3 routes: TLS is required for more than one route to be reachable by name (dbname-based routing alone cannot tell a real physical standby's connection apart from any other route once there is more than one, see this project's own README.md)
  21:11:36 2585325 WARN  Route "third" has no --hostname: it can only be reached by dbname (pg_basebackup/pg_receivewal/archive_command) or the "*" wildcard, never by name by a real physical standby -- pass --hostname next time, or edit "/var/lib/archiver/pg_walserver.ini" by hand, to add one
  21:11:36 2585325 INFO  Connecting to primary:5534 as "archiver_repl" to fetch the system identifier
  21:11:36 2585325 INFO  Wrote system identifier 7690676421909321516 to "/var/lib/archiver/third/pg_walserver_systemid"
  21:11:36 2585325 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/third/pg_walserver_pgversion"
  21:11:36 2585325 INFO  setup complete: route "third" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  21:11:36 2585325 INFO  Reloaded the running pg_walserver (pid 2584314): it will pick up this route immediately

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_scram_secret`
* :ref:`pg_walserver_create_cert`
* :ref:`pg_walserver_basebackup`
* :ref:`pg_walserver_fetch_systemid`
