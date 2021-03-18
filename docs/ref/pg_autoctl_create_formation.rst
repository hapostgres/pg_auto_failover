.. _pg_autoctl_create_formation:

pg_autoctl create formation
===========================

pg_autoctl create formation - Create a new formation on the pg_auto_failover
monitor

Synopsis
--------

This command registers a new formation on the monitor, with the specified
kind::

  usage: pg_autoctl create formation  [ --pgdata --monitor --formation --kind --dbname  --with-secondary --without-secondary ]

  --pgdata      path to data directory
  --monitor     pg_auto_failover Monitor Postgres URL
  --formation   name of the formation to create
  --kind        formation kind, either "pgsql" or "citus"
  --dbname      name for postgres database to use in this formation
  --enable-secondary     create a formation that has multiple nodes that can be
                         used for fail over when others have issues
  --disable-secondary    create a citus formation without nodes to fail over to
  --number-sync-standbys minimum number of standbys to confirm write

Description
-----------

A single pg_auto_failover monitor may manage any number of formations, each
composed of at least one Postgres service group. This commands creates a new
formation so that it is then possible to register Postgres nodes in the new
formation.

Options
-------

The following options are available to ``pg_autoctl create formation``:

--pgdata

  Location where to initialize a Postgres database cluster, using either
  ``pg_ctl initdb`` or ``pg_basebackup``. Defaults to the environment
  variable ``PGDATA``.

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--formation

  Name of the formation to create.

--kind

  A pg_auto_failover formation could be of kind ``pgsql`` or of kind
  ``citus``. At the moment ``citus`` formation kinds are not managed in the
  Open Source version of pg_auto_failover.

--dbname

  Name of the database to use in the formation, mostly useful to formation
  kinds ``citus`` where the Citus extension is only installed in a single
  target database.

--enable-secondary

  The formation to be created allows using standby nodes. Defaults to
  ``true``. Mostly useful for Citus formations.

--disable-secondary

  See ``--enable-secondary`` above.

--number-sync-standby

  Postgres streaming replication uses ``synchronous_standby_names`` to setup
  how many standby nodes should have received a copy of the transaction
  data. When using pg_auto_failover this setup is handled at the formation
  level.

  Defaults to zero when creating the first two Postgres nodes in a formation
  in the same group. When set to zero pg_auto_failover uses synchronous
  replication only when a standby node is available: the idea is to allow
  failover, this setting does not allow proper HA for Postgres.

  When adding a third node that participates in the quorum (one primary, two
  secondaries), the setting is automatically changed from zero to one.
