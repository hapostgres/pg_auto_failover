Configuring pg_auto_failover
============================

Several defaults settings of pg_auto_failover can be reviewed and changed depending
on the trade-offs you want to implement in your own production setup. The
settings that you can change will have an impact of the following
operations:

  - Deciding when to promote the secondary

    pg_auto_failover decides to implement a failover to the secondary node when it
    detects that the primary node is unhealthy. Changing the following
    settings will have an impact on when the pg_auto_failover monitor decides to
    promote the secondary PostgreSQL node::

      pgautofailover.health_check_max_retries
      pgautofailover.health_check_period
      pgautofailover.health_check_retry_delay
      pgautofailover.health_check_timeout
      pgautofailover.node_considered_unhealthy_timeout

  - Time taken to promote the secondary

    At secondary promotion time, pg_auto_failover waits for the following timeout to
    make sure that all pending writes on the primary server made it to the
    secondary at shutdown time, thus preventing data loss.::

      pgautofailover.primary_demote_timeout

  - Preventing promotion of the secondary

    pg_auto_failover implements a trade-off where data availability trumps service
    availability. When the primary node of a PostgreSQL service is detected
    unhealthy, the secondary is only promoted if it was known to be eligible
    at the moment when the primary is lost.

    In the case when *synchronous replication* was in use at the moment when
    the primary node is lost, then we know we can switch to the secondary
    safely, and the wal lag is 0 in that case.

    In the case when the secondary server had been detected unhealthy
    before, then the pg_auto_failover monitor switches it from the state SECONDARY to
    the state CATCHING-UP and promotion is prevented then.

    The following setting allows to still promote the secondary, allowing
    for a window of data loss::

      pgautofailover.promote_wal_log_threshold

pg_auto_failover Monitor
------------------------

The configuration for the behavior of the monitor happens in the PostgreSQL
database where the extension has been deployed::

  pg_auto_failover=> select name, setting, unit, short_desc from pg_settings where name ~ 'pgautofailover.';
  -[ RECORD 1 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.enable_sync_wal_log_threshold
  setting    | 16777216
  unit       |
  short_desc | Don't enable synchronous replication until secondary xlog is within this many bytes of the primary's
  -[ RECORD 2 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.health_check_max_retries
  setting    | 2
  unit       |
  short_desc | Maximum number of re-tries before marking a node as failed.
  -[ RECORD 3 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.health_check_period
  setting    | 5000
  unit       | ms
  short_desc | Duration between each check (in milliseconds).
  -[ RECORD 4 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.health_check_retry_delay
  setting    | 2000
  unit       | ms
  short_desc | Delay between consecutive retries.
  -[ RECORD 5 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.health_check_timeout
  setting    | 5000
  unit       | ms
  short_desc | Connect timeout (in milliseconds).
  -[ RECORD 6 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.node_considered_unhealthy_timeout
  setting    | 20000
  unit       | ms
  short_desc | Mark node unhealthy if last ping was over this long ago
  -[ RECORD 7 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.primary_demote_timeout
  setting    | 30000
  unit       | ms
  short_desc | Give the primary this long to drain before promoting the secondary
  -[ RECORD 8 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.promote_wal_log_threshold
  setting    | 16777216
  unit       |
  short_desc | Don't promote secondary unless xlog is with this many bytes of the master
  -[ RECORD 9 ]----------------------------------------------------------------------------------------------------
  name       | pgautofailover.startup_grace_period
  setting    | 10000
  unit       | ms
  short_desc | Wait for at least this much time after startup before initiating a failover.

You can edit the parameters as usual with PostgreSQL, either in the
``postgresql.conf`` file or using ``ALTER DATABASE pg_auto_failover SET parameter =
value;`` commands, then issuing a reload.

pg_auto_failover Keeper Service
-------------------------------

For an introduction to the ``pg_autoctl`` commands relevant to the pg_auto_failover
Keeper configuration, please see :ref:`pg_autoctl_config`.

An example configuration file looks like the following::

  [pg_autoctl]
  role = keeper
  monitor = postgres://autoctl_node@192.168.1.34:6000/pg_auto_failover
  formation = default
  group = 0
  hostname = node1.db
  nodekind = standalone

  [postgresql]
  pgdata = /data/pgsql/
  pg_ctl = /usr/pgsql-10/bin/pg_ctl
  dbname = postgres
  host = /tmp
  port = 5000

  [replication]
  slot = pgautofailover_standby
  maximum_backup_rate = 100M
  backup_directory = /data/backup/node1.db

  [timeout]
  network_partition_timeout = 20
  postgresql_restart_failure_timeout = 20
  postgresql_restart_failure_max_retries = 3

To output, edit and check entries of the configuration, the following
commands are provided::

  pg_autoctl config check [--pgdata <pgdata>]
  pg_autoctl config get [--pgdata <pgdata>] section.option
  pg_autoctl config set [--pgdata <pgdata>] section.option value

The ``[postgresql]`` section is discovered automatically by the ``pg_autoctl``
command and is not intended to be changed manually.

**pg_autoctl.monitor**

PostgreSQL service URL of the pg_auto_failover monitor, as given in the output of
the ``pg_autoctl show uri`` command.

**pg_autoctl.formation**

A single pg_auto_failover monitor may handle several postgres formations. The default
formation name `default` is usually fine.

**pg_autoctl.group**

This information is retrieved by the pg_auto_failover keeper when registering a node
to the monitor, and should not be changed afterwards. Use at your own risk.

**pg_autoctl.hostname**

Node `hostname` used by all the other nodes in the cluster to contact this
node. In particular, if this node is a primary then its standby uses that
address to setup streaming replication.

**replication.slot**

Name of the PostgreSQL replication slot used in the streaming replication
setup automatically deployed by pg_auto_failover. Replication slots can't be renamed
in PostgreSQL.

**replication.maximum_backup_rate**

When pg_auto_failover (re-)builds a standby node using the ``pg_basebackup``
command, this parameter is given to ``pg_basebackup`` to throttle the
network bandwidth used. Defaults to 100Mbps.

**replication.backup_directory**

When pg_auto_failover (re-)builds a standby node using the ``pg_basebackup``
command, this parameter is the target directory where to copy the bits from
the primary server. When the copy has been successful, then the directory is
renamed to **postgresql.pgdata**.

The default value is computed from ``${PGDATA}/../backup/${hostname}`` and
can be set to any value of your preference. Remember that the directory
renaming is an atomic operation only when both the source and the target of
the copy are in the same filesystem, at least in Unix systems.

**timeout**

This section allows to setup the behavior of the pg_auto_failover keeper in
interesting scenarios.

**timeout.network_partition_timeout**

Timeout in seconds before we consider failure to communicate with other
nodes indicates a network partition. This check is only done on a PRIMARY
server, so other nodes mean both the monitor and the standby.

When a PRIMARY node is detected to be on the losing side of a network
partition, the pg_auto_failover keeper enters the DEMOTE state and stops the
PostgreSQL instance in order to protect against split brain situations.

The default is 20s.

.. would be better not to have to do this, but that'll have to do for now
.. raw:: latex

    \newpage

**timeout.postgresql_restart_failure_timeout**

**timeout.postgresql_restart_failure_max_retries**

When PostgreSQL is not running, the first thing the pg_auto_failover keeper does is
try to restart it. In case of a transient failure (e.g. file system is full,
or other dynamic OS resource constraint), the best course of action is to
try again for a little while before reaching out to the monitor and ask for
a failover.

The pg_auto_failover keeper tries to restart PostgreSQL
``timeout.postgresql_restart_failure_max_retries`` times in a row
(default 3) or up to ``timeout.postgresql_restart_failure_timeout``
(defaults 20s) since it detected that PostgreSQL is not running, whichever
comes first.
