.. _pg_walserver_register:

pg_walserver register
=======================

pg_walserver register - Register one cluster this pg_walserver archives, or list what's registered

Synopsis
--------

::

  pg_walserver register cluster <name> --pgdata <path> [--config-file <path>]
      [--path <dir>] --pguri <conninfo> | --host <host> [--port <port>]
      [--user <name>] [--hostname <name>] [--receivewal pull|none]
      [--ssl-self-signed] [--force]

  pg_walserver register list --pgdata <path> [--config-file <path>]

``pg_walserver register`` creates (or validates) the clusters (routes)
one ``pg_walserver`` instance archives, and lists what is currently
registered. It is split out of what used to be ``pg_walserver setup``:
``setup`` (:ref:`pg_walserver_setup`) now only configures the server
itself (port, TLS, auth-timeout); ``register`` (this page),
:ref:`pg_walserver_drop`, and :ref:`pg_walserver_set_upstream` configure
what it serves. The cluster's own name is always given positionally --
``register cluster mycluster ...`` -- never a ``--cluster`` flag.

pg_walserver register cluster
-------------------------------

Writes or validates one config file route and fetches its upstream
system identifier. It never takes a base backup itself: instead, it
reloads an already-running ``pg_walserver serve`` for the same
``--pgdata``, if there is one, so the new or changed route takes effect
immediately (with no server running, the config just written takes
effect the next time ``serve`` starts). Either way, ``serve`` itself
takes the route's first base backup automatically, once, the next time
it starts or reloads -- see :ref:`pg_walserver`'s "Archiving one
cluster" section.

Options
^^^^^^^

<name>

  The cluster's own name, given positionally (never a flag).

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives, independent of ``--pgdata``.
  Defaults to ``<pgdata>/pg_walserver.ini``, or the
  ``PG_WALSERVER_CONFIG_FILE`` environment variable -- a Debian-style
  deployment's own split, e.g. ``/etc/pg_walserver/pg_walserver.ini``
  for config, ``--pgdata`` at ``/var/lib/pg_walserver`` for data.

--path

  The route's own directory, created if missing. Defaults to
  ``<pgdata>/<name>``; rarely needs to be given explicitly.

--pguri

  A libpq connection string, keyword/value or ``postgres://`` URI,
  written into the route's own ``upstream`` property. Named ``pguri``
  to match this project's own vocabulary for a Postgres connection
  string elsewhere (``pg_autoctl``'s own ``monitor_pguri``).

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
^^^^^^^^

Register a cluster, no ``--path`` given (it defaults to
``<pgdata>/<name>``), ``--pguri`` as a ``postgres://`` URI,
``--ssl-self-signed`` creating a certificate right away, no server
running yet::

  archive$ export PGDATA=/var/lib/archiver
  archive$ PGPASSWORD=s3kr3t pg_walserver register cluster mycluster \
      --pguri "postgres://archiver_repl@primary:5432/?sslmode=disable" \
      --ssl-self-signed --hostname archive
  22:57:11 40 INFO  Added route "mycluster" (path "/var/lib/archiver/mycluster") to "/var/lib/archiver/pg_walserver.ini"
  22:57:11 40 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=archive"
  22:57:12 40 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=archive) -- replace it with a real one before running on a reachable network
  22:57:12 40 INFO  Connecting to primary:5432 as "archiver_repl" to fetch the system identifier
  22:57:12 40 INFO  Wrote system identifier 7690723939657998375 to "/var/lib/archiver/mycluster/pg_walserver_systemid"
  22:57:12 40 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/mycluster/pg_walserver_pgversion"
  22:57:12 40 INFO  register cluster complete: route "mycluster" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  22:57:12 40 INFO  No running "pg_walserver serve" found at "/var/lib/archiver/pg_walserver.pid": the route just written will take effect the next time "serve" starts

Register a second cluster, with ``serve`` already running --
``register cluster`` reloads it immediately instead of waiting for a
restart, and, this now being the ini's second route, warns that this
one has no ``--hostname`` of its own::

  archive$ PGPASSWORD=s3kr3t pg_walserver register cluster third \
      --pguri "host=primary port=5432 user=archiver_repl sslmode=disable"
  22:57:22 63 INFO  Added route "third" (path "/var/lib/archiver/third") to "/var/lib/archiver/pg_walserver.ini"
  22:57:22 63 INFO  "/var/lib/archiver/pg_walserver.ini" now has 2 routes: TLS is required for more than one route to be reachable by name (dbname-based routing alone cannot tell a real physical standby's connection apart from any other route once there is more than one, see this project's own README.md)
  22:57:22 63 WARN  Route "third" has no --hostname: it can only be reached by dbname (pg_basebackup/pg_receivewal/archive_command) or the "*" wildcard, never by name by a real physical standby -- pass --hostname next time, or edit "/var/lib/archiver/pg_walserver.ini" by hand, to add one
  22:57:22 63 INFO  Connecting to primary:5432 as "archiver_repl" to fetch the system identifier
  22:57:22 63 INFO  Wrote system identifier 7690723939657998375 to "/var/lib/archiver/third/pg_walserver_systemid"
  22:57:22 63 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/third/pg_walserver_pgversion"
  22:57:22 63 INFO  register cluster complete: route "third" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  22:57:22 63 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

pg_walserver register list
-----------------------------

Lists every route this ``pg_walserver`` instance has registered:
cluster name, receivewal mode, upstream, TLS SNI hostname (or ``-`` if
none), and on-disk path. This is a *registration* view, not the
operational, storage-level view :ref:`pg_walserver_list_clusters` gives
over the archived data itself::

  archive$ pg_walserver register list --pgdata /var/lib/archiver
  CLUSTER              RECEIVEWAL UPSTREAM                         HOSTNAME                 PATH
  ------------------------------------------------------------------------------------------------------------------
  mycluster            pull      postgres://archiver_repl@primary:5432/?sslmode=disable archive                  /var/lib/archiver/mycluster
  third                pull      host=primary port=5432 user=archiver_repl sslmode=disable -                        /var/lib/archiver/third

With no cluster registered yet, it says so instead of printing an empty
table.

Options
^^^^^^^

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_setup`
* :ref:`pg_walserver_drop`
* :ref:`pg_walserver_set_upstream`
* :ref:`pg_walserver_scram_secret`
* :ref:`pg_walserver_create_cert`
* :ref:`pg_walserver_basebackup`
* :ref:`pg_walserver_fetch_systemid`
* :ref:`pg_walserver_list_clusters`
