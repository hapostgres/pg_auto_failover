.. _pg_autoctl_config_check:

pg_autoctl config check
=======================

pg_autoctl config check - Check pg_autoctl configuration

Synopsis
--------

This command implements a very basic list of sanity checks for a pg_autoctl
node setup::

  usage: pg_autoctl config check  [ --pgdata ] [ --json ]

  --pgdata      path to data directory
  --json        output data in the JSON format

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output JSON formated data.

Examples
--------

::

  $ pg_autoctl config check --pgdata node1
  18:37:27 63749 INFO  Postgres setup for PGDATA "/Users/dim/dev/MS/pg_auto_failover/tmux/node1" is ok, running with PID 5501 and port 99698
  18:37:27 63749 INFO  Connection to local Postgres ok, using "port=5501 dbname=demo host=/tmp"
  18:37:27 63749 INFO  Postgres configuration settings required for pg_auto_failover are ok
  18:37:27 63749 WARN  Postgres 12.1 does not support replication slots on a standby node
  18:37:27 63749 INFO  Connection to monitor ok, using "postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer"
  18:37:27 63749 INFO  Monitor is running version "1.5.0.1", as expected
  pgdata:                /Users/dim/dev/MS/pg_auto_failover/tmux/node1
  pg_ctl:                /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl
  pg_version:            12.3
  pghost:                /tmp
  pgport:                5501
  proxyport:             0
  pid:                   99698
  is in recovery:        no
  Control Version:       1201
  Catalog Version:       201909212
  System Identifier:     6941034382470571312
  Latest checkpoint LSN: 0/6000098
  Postmaster status:     ready
