.. _pg_walserver_cluster:

pg_walserver cluster
=====================

pg_walserver cluster - Register, drop, list, or re-point the clusters a pg_walserver archives

Synopsis
--------

::

  pg_walserver cluster register <name> --pgdata <path> [--config <path>]
      [--path <dir>] --pguri <conninfo> | --host <host> [--port <port>]
      [--user <name>] [--hostname <name>] [--receivewal pull|none]
      [--ssl-self-signed] [--force]

  pg_walserver cluster drop <name> --pgdata <path> [--config <path>]
      [--purge]

  pg_walserver cluster list [--pgdata <path> | --config <path>] [--upstream]

  pg_walserver cluster set-upstream <name> --pgdata <path>
      [--config <path>] --pguri <conninfo> [--force-basebackup]

``pg_walserver cluster`` is the wizard that creates, removes, lists,
and re-points the clusters (routes) one ``pg_walserver`` instance
archives. It is split out of what used to be ``pg_walserver setup``:
``setup`` (:ref:`pg_walserver_setup`) now only configures the server
itself (port, TLS, auth-timeout, HBA); ``cluster`` configures what it
serves. The cluster's own name is always given positionally --
``cluster register mycluster ...`` -- never a ``--cluster`` flag.

pg_walserver cluster register
------------------------------

Writes or validates one config file route and fetches its upstream
system identifier. It never takes a base backup itself: instead, it
reloads an already-running ``pg_walserver serve`` for the same
``--pgdata``, if there is one, so the new or changed route takes
effect immediately (with no server running, the config just written
takes effect the next time ``serve`` starts). Either way, ``serve``
itself takes the route's first base backup automatically, once, the
next time it starts or reloads -- see :ref:`pg_walserver`'s "Archiving
one cluster" section.

Options
^^^^^^^

<name>

  The cluster's own name, given positionally (never a flag).

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives, independent of ``--pgdata``.
  Defaults to ``<pgdata>/pg_walserver.ini``, or the
  ``PG_WALSERVER_CONFIG_FILE`` environment variable -- a Debian-style
  deployment's own split, e.g. ``/etc/pg_walserver/pg_walserver.ini``
  for config, ``--pgdata`` at ``/var/lib/pg_walserver`` for data. Every
  sub-command on this page takes this same flag.

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
  archive$ PGPASSWORD=s3kr3t pg_walserver cluster register mycluster \
      --pguri "postgres://archiver_repl@primary:5432/?sslmode=disable" \
      --ssl-self-signed --hostname archive
  22:57:11 40 INFO  Added route "mycluster" (path "/var/lib/archiver/mycluster") to "/var/lib/archiver/pg_walserver.ini"
  22:57:11 40 INFO   /usr/bin/openssl req -new -x509 -days 365 -nodes -text -out /var/lib/archiver/server.crt -keyout /var/lib/archiver/server.key -subj "/CN=archive"
  22:57:12 40 INFO  Created a self-signed certificate for "/var/lib/archiver" ("/var/lib/archiver/server.crt"/"/var/lib/archiver/server.key", CN=archive) -- replace it with a real one before running on a reachable network
  22:57:12 40 INFO  Connecting to primary:5432 as "archiver_repl" to fetch the system identifier
  22:57:12 40 INFO  Wrote system identifier 7690723939657998375 to "/var/lib/archiver/mycluster/pg_walserver_systemid"
  22:57:12 40 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/mycluster/pg_walserver_pgversion"
  22:57:12 40 INFO  cluster register complete: route "mycluster" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  22:57:12 40 INFO  No running "pg_walserver serve" found at "/var/lib/archiver/pg_walserver.pid": the route just written will take effect the next time "serve" starts

Register a second cluster, with ``serve`` already running --
``cluster register`` reloads it immediately instead of waiting for a
restart, and, this now being the ini's second route, warns that this
one has no ``--hostname`` of its own::

  archive$ PGPASSWORD=s3kr3t pg_walserver cluster register third \
      --pguri "host=primary port=5432 user=archiver_repl sslmode=disable"
  22:57:22 63 INFO  Added route "third" (path "/var/lib/archiver/third") to "/var/lib/archiver/pg_walserver.ini"
  22:57:22 63 INFO  "/var/lib/archiver/pg_walserver.ini" now has 2 routes: TLS is required for more than one route to be reachable by name (dbname-based routing alone cannot tell a real physical standby's connection apart from any other route once there is more than one, see this project's own README.md)
  22:57:22 63 WARN  Route "third" has no --hostname: it can only be reached by dbname (pg_basebackup/pg_receivewal/archive_command) or the "*" wildcard, never by name by a real physical standby -- pass --hostname next time, or edit "/var/lib/archiver/pg_walserver.ini" by hand, to add one
  22:57:22 63 INFO  Connecting to primary:5432 as "archiver_repl" to fetch the system identifier
  22:57:22 63 INFO  Wrote system identifier 7690723939657998375 to "/var/lib/archiver/third/pg_walserver_systemid"
  22:57:22 63 INFO  Wrote upstream Postgres version 170011 to "/var/lib/archiver/third/pg_walserver_pgversion"
  22:57:22 63 INFO  cluster register complete: route "third" is ready (no base backup taken here -- "pg_walserver serve" bootstraps the route's first base backup automatically, once, the next time it starts or reloads this route; run "pg_walserver basebackup" by hand at any time to take another one)
  22:57:22 63 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

pg_walserver cluster list
---------------------------

Lists every route this ``pg_walserver`` instance has registered:
cluster name, receivewal mode, TLS SNI hostname (or ``-`` if none), and
on-disk path. This is a *registration* view, not the operational,
storage-level view :ref:`pg_walserver_list_clusters` gives over the
archived data itself. UPSTREAM (a full connection string/URI, often far
wider than every other column combined) is deliberately never in this
default table -- it would make every row unreadable -- pass ``--config``
alone (no ``--pgdata`` needed, either is enough) to show it works too::

  archive$ pg_walserver cluster list --pgdata /var/lib/archiver
  CLUSTER              RECEIVEWAL HOSTNAME                 PATH
  -------------------- ---------- ------------------------ ----
  mycluster            pull       archive                  /var/lib/archiver/mycluster
  third                pull       -                        /var/lib/archiver/third

With ``--upstream``, each cluster's own connection string is printed
too, pivoted into one ``key: value`` block per cluster instead of
widening the row::

  archive$ pg_walserver cluster list --pgdata /var/lib/archiver --upstream
  cluster:    mycluster
  receivewal: pull
  upstream:   postgres://archiver_repl@primary:5432/?sslmode=disable
  hostname:   archive
  path:       /var/lib/archiver/mycluster

  cluster:    third
  receivewal: pull
  upstream:   host=primary port=5432 user=archiver_repl sslmode=disable
  hostname:   -
  path:       /var/lib/archiver/third

With no cluster registered yet, it says so instead of printing an empty
table.

Options
^^^^^^^

--pgdata

  This instance's own data root. Either this or ``--config`` is enough.
  Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).
  Either this or ``--pgdata`` is enough.

--upstream

  Also print each cluster's own upstream connection string, pivoted
  into one block per cluster instead of a table column (see above).
  Skipped by default.

pg_walserver cluster set-upstream
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

  archive$ PGPASSWORD=s3kr3t pg_walserver cluster set-upstream third \
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

pg_walserver cluster drop
---------------------------

Removes a route's registration from the config file
(``cluster register`` above writes it). By default its own on-disk
data (every base backup and WAL segment it holds) is left in place;
pass ``--purge`` to also remove it. Reloads an already-running
``pg_walserver serve`` for the same ``--pgdata``, if there is one, so
it stops serving the dropped route immediately.

Options
^^^^^^^

<name>

  The cluster's own name, given positionally (never a flag).

--purge

  Also remove the route's own on-disk data. Without it, only the
  registration itself is removed.

Examples
^^^^^^^^

Drop a route, keeping its data::

  archive$ pg_walserver cluster drop third --pgdata /var/lib/archiver
  22:57:33 90 INFO  Dropped route "third" from "/var/lib/archiver/pg_walserver.ini"
  22:57:33 90 INFO  Route "third" dropped from "/var/lib/archiver/pg_walserver.ini"; its own data under "/var/lib/archiver/third" was left in place (pass --purge to remove it too)
  22:57:33 90 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

Drop a route and remove its data with it::

  archive$ pg_walserver cluster drop tmp --pgdata /var/lib/archiver --purge
  22:57:34 104 INFO  Dropped route "tmp" from "/var/lib/archiver/pg_walserver.ini"
  22:57:34 104 INFO  Removed "/var/lib/archiver/tmp" (--purge)
  22:57:34 104 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_setup`
* :ref:`pg_walserver_scram_secret`
* :ref:`pg_walserver_create_cert`
* :ref:`pg_walserver_basebackup`
* :ref:`pg_walserver_fetch_systemid`
* :ref:`pg_walserver_list_clusters`
