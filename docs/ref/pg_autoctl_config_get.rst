.. _pg_autoctl_config_get:

pg_autoctl config get
=====================

pg_autoctl config get - Get the value of a given pg_autoctl configuration variable

Synopsis
--------

This command prints a ``pg_autoctl`` configuration setting::

  usage: pg_autoctl config get  [ --pgdata ] [ --json ] [ section.option ]

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

When the argument ``section.option`` is used, this is the name of a
configuration ooption. The configuration file for ``pg_autoctl`` is stored
using the INI format.

When no argument is given to ``pg_autoctl config get`` the entire
configuration file is given in the output. To figure out where the
configuration file is stored, see :ref:`pg_autoctl_show_file` and use
``pg_autoctl show file --config``.

Examples
--------

Without arguments, we get the entire file::

  $ pg_autoctl config get --pgdata node1
  [pg_autoctl]
  role = keeper
  monitor = postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer
  formation = default
  group = 0
  name = node1
  hostname = localhost
  nodekind = standalone

  [postgresql]
  pgdata = /Users/dim/dev/MS/pg_auto_failover/tmux/node1
  pg_ctl = /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl
  dbname = demo
  host = /tmp
  port = 5501
  proxyport = 0
  listen_addresses = *
  auth_method = trust
  hba_level = app

  [ssl]
  active = 1
  sslmode = require
  cert_file = /Users/dim/dev/MS/pg_auto_failover/tmux/node1/server.crt
  key_file = /Users/dim/dev/MS/pg_auto_failover/tmux/node1/server.key

  [replication]
  maximum_backup_rate = 100M
  backup_directory = /Users/dim/dev/MS/pg_auto_failover/tmux/backup/node_1

  [timeout]
  network_partition_timeout = 20
  prepare_promotion_catchup = 30
  prepare_promotion_walreceiver = 5
  postgresql_restart_failure_timeout = 20
  postgresql_restart_failure_max_retries = 3

It is possible to pipe JSON formated output to the ``jq`` command line and
filter the result down to a specific section of the file::

  $ pg_autoctl config get --pgdata node1 --json | jq .pg_autoctl
  {
    "role": "keeper",
    "monitor": "postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer",
    "formation": "default",
    "group": 0,
    "name": "node1",
    "hostname": "localhost",
    "nodekind": "standalone"
  }

Finally, a single configuration element can be listed::

  $ pg_autoctl config get --pgdata node1 ssl.sslmode --json
  require
