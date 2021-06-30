.. _pg_autoctl_config_set:

pg_autoctl config set
=====================

pg_autoctl config set - Set the value of a given pg_autoctl configuration variable

Synopsis
--------

This command prints a ``pg_autoctl`` configuration setting::

  usage: pg_autoctl config set  [ --pgdata ] [ --json ] section.option [ value ]

  --pgdata      path to data directory

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output JSON formated data.

Description
-----------

This commands allows to set a pg_autoctl configuration setting to a new
value. Most settings are possible to change and can be reloaded online.

Some of those commands can then be applied with a ``pg_autoctl reload``
command to an already running process.

Settings
--------

pg_autoctl.role

  This setting can not be changed. It can be either ``monitor`` or
  ``keeper`` and the rest of the configuration file is read depending on
  this value.

pg_autoctl.monitor

  URI of the pg_autoctl monitor Postgres service. Can be changed with a reload.

  To register an existing node to a new monitor, use ``pg_autoctl disable
  monitor`` and then ``pg_autoctl enable monitor``.

pg_autoctl.formation

  Formation to which this node has been registered. Changing this setting is
  not supported.

pg_autoctl.group

  Group in which this node has been registered. Changing this setting is not
  supported.

pg_autoctl.name

  Name of the node as known to the monitor and listed in ``pg_autoctl show
  state``. Can be changed with a reload.

pg_autoctl.hostname

  Hostname or IP address of the node, as known to the monitor. Can be
  changed with a reload.

pg_autoctl.nodekind

  This setting can not be changed and depends on the command that has been
  used to create this pg_autoctl node.

postgresql.pgdata

  Directory where the managed Postgres instance is to be created (or found)
  and managed. Can't be changed.

postgresql.pg_ctl

  Path to the ``pg_ctl`` tool used to manage this Postgres instance.
  Absolute path depends on the major version of Postgres and looks like
  ``/usr/lib/postgresql/13/bin/pg_ctl`` when using a debian or ubuntu OS.

  Can be changed after a major upgrade of Postgres.

postgresql.dbname

  Name of the database that is used to connect to Postgres. Can be changed,
  but then must be changed manually on the monitor's
  ``pgautofailover.formation`` table with a SQL command.

  .. warning::

	 When using pg_auto_failover enterprise edition with Citus support, this
	 is the database where pg_autoctl maintains the list of Citus nodes on
	 the coordinator. Using the same database name as your application that
	 uses Citus is then crucial.

postgresql.host

  Hostname to use in connection strings when connecting from the local
  ``pg_autoctl`` process to the local Postgres database. Defaults to using
  the Operating System default value for the Unix Domain Socket directory,
  either ``/tmp`` or when using debian or ubuntu ``/var/run/postgresql``.

  Can be changed with a reload.

postgresql.port

  Port on which Postgres should be managed. Can be changed offline, between
  a ``pg_autoctl stop`` and a subsequent ``pg_autoctl start``.

postgresql.listen_addresses

  Value to set to Postgres parameter of the same name. At the moment
  ``pg_autoctl`` only supports a single address for this parameter.

postgresql.auth_method

  Authentication method to use when editing HBA rules to allow the Postgres
  nodes of a formation to connect to each other, and to the monitor, and to
  allow the monitor to connect to the nodes.

  Can be changed online with a reload, but actually adding new HBA rules
  requires a restart of the "node-active" service.

postgresql.hba_level

  This setting reflects the choice of ``--skip-pg-hba`` or ``--pg-hba-lan``
  that has been used when creating this pg_autoctl node. Can be changed with
  a reload, though the HBA rules that have been previously added will not
  get removed.

ssl.active, ssl.sslmode, ssl.cert_file, ssl.key_file, etc

  Please use the command ``pg_autoctl enable ssl`` or ``pg_autoctl disable
  ssl`` to manage the SSL settings in the ``ssl`` section of the
  configuration. Using those commands, the settings can be changed online.

replication.maximum_backup_rate

  Used as a parameter to ``pg_basebackup``, defaults to ``100M``. Can be
  changed with a reload. Changing this value does not affect an already
  running ``pg_basebackup`` command.

  Limiting the bandwidth used by ``pg_basebackup`` makes the operation
  slower, and still has the advantage of limiting the impact on the disks of
  the primary server.

replication.backup_directory

  Target location of the ``pg_basebackup`` command used by pg_autoctl when
  creating a secondary node. When done with fetching the data over the
  network, then pg_autoctl uses the *rename(2)* system-call to rename the
  temporary download location to the target PGDATA location.

  The *rename(2)* system-call is known to be atomic when both the source and
  the target of the operation are using the same file system / mount point.

  Can be changed online with a reload, will not affect already running
  ``pg_basebackup`` sub-processes.

replication.password

  Used as a parameter in the connection string to the upstream Postgres
  node. The "replication" connection uses the password set-up in the
  pg_autoctl configuration file.

  Changing the ``replication.password`` of a pg_autoctl configuration has no
  effect on the Postgres database itself. The password must match what the
  Postgres upstream node expects, which can be set with the following SQL
  command run on the upstream server (primary or other standby node)::

	alter user pgautofailover_replicator password 'h4ckm3m0r3';

  The ``replication.password`` can be changed online with a reload, but
  requires restarting the Postgres service to be activated. Postgres only
  reads the ``primary_conninfo`` connection string at start-up, up to and
  including Postgres 12. With Postgres 13 and following, it is possible to
  *reload* this Postgres paramater.

timeout.network_partition_timeout

  Timeout (in seconds) that pg_autoctl waits before deciding that it is on
  the losing side of a network partition. When pg_autoctl fails to connect
  to the monitor and when the local Postgres instance
  ``pg_stat_replication`` system view is empty, and after this many seconds
  have passed, then pg_autoctl demotes itself.

  Can be changed with a reload.

timeout.prepare_promotion_catchup

  Currently not used in the source code. Can be changed with a reload.

timeout.prepare_promotion_walreceiver

  Currently not used in the source code. Can be changed with a reload.

timeout.postgresql_restart_failure_timeout

  When pg_autoctl fails to start Postgres for at least this duration from
  the first attempt, then it starts reporting that Postgres is not running
  to the monitor, which might then decide to implement a failover.

  Can be changed with a reload.

timeout.postgresql_restart_failure_max_retries

  When pg_autoctl fails to start Postgres for at least this many times then
  it starts reporting that Postgres is not running to the monitor, which
  them might decide to implement a failover.

  Can be changed with a reload.
