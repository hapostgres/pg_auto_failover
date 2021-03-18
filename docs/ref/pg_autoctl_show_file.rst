.. _pg_autoctl_show_file:

pg_autoctl show file
=============================

pg_autoctl show file - List pg_autoctl internal files (config, state, pid)

Synopsis
--------

This command the files that ``pg_autoctl`` uses internally for its own
configuration, state, and pid::

  usage: pg_autoctl show file  [ --pgdata --all --config | --state | --init | --pid --contents ]

  --pgdata      path to data directory
  --all         show all pg_autoctl files
  --config      show pg_autoctl configuration file
  --state       show pg_autoctl state file
  --init        show pg_autoctl initialisation state file
  --pid         show pg_autoctl PID file
  --contents    show selected file contents
  --json        output data in the JSON format

Description
-----------

The ``pg_autoctl`` command follows the `XDG Base Directory Specification`__
and places its internal and configuration files by default in places such as
``~/.config/pg_autoctl`` and ``~/.local/share/pg_autoctl``.

__ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

It is possible to change the default XDG locations by using the environment
variables ``XDG_CONFIG_HOME``, ``XDG_DATA_HOME``, and ``XDG_RUNTIME_DIR``.

Also, ``pg_config`` uses sub-directories that are specific to a given
``PGDATA``, making it possible to run several Postgres nodes on the same
machine, which is very practical for testing and development purposes,
though not advised for production setups.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--all

  List all the files that belong to this `pg_autoctl` node.

--config

  Show only the configuration file.

--state

  Show only the state file.

--init

  Show only the init state file, which only exists while the command
  ``pg_autoctl create postgres`` or the command ``pg_autoctl create
  monitor`` is running, or when than command failed (and can then be
  retried).

--pid

  Show only the pid file.

--contents

  When one of the options to show a specific file is in use, then
  ``--contents`` shows the contents of the selected file instead of showing
  its absolute file path.

--json

  Output JSON formated data.

Examples
--------

The following examples are taken from a QA environment that has been
prepared thanks to the ``make cluster`` command made available to the
pg_auto_failover contributors. As a result, the XDG environment variables
have been tweaked to obtain a self-contained test::

   $  tmux show-env | grep XDG
   XDG_CONFIG_HOME=/Users/dim/dev/MS/pg_auto_failover/tmux/config
   XDG_DATA_HOME=/Users/dim/dev/MS/pg_auto_failover/tmux/share
   XDG_RUNTIME_DIR=/Users/dim/dev/MS/pg_auto_failover/tmux/run

Within that self-contained test location, we can see the following examples.

::

   $ pg_autoctl show file --pgdata ./node1
      File | Path
   --------+----------------
    Config | /Users/dim/dev/MS/pg_auto_failover/tmux/config/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_autoctl.cfg
     State | /Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_autoctl.state
      Init | /Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_autoctl.init
       Pid | /Users/dim/dev/MS/pg_auto_failover/tmux/run/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_autoctl.pid
      'ANY 1 (pgautofailover_standby_2, pgautofailover_standby_3)'

   $ pg_autoctl show file --pgdata node1 --state
   /Users/dim/dev/MS/pg_auto_failover/tmux/share/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_autoctl.state

   $ pg_autoctl show file --pgdata node1 --state --contents
   Current Role:             primary
   Assigned Role:            primary
   Last Monitor Contact:     Thu Mar 18 17:32:25 2021
   Last Secondary Contact:   0
   pg_autoctl state version: 1
   group:                    0
   node id:                  1
   nodes version:            0
   PostgreSQL Version:       1201
   PostgreSQL CatVersion:    201909212
   PostgreSQL System Id:     6940955496243696337

   pg_autoctl show file --pgdata node1 --config --contents --json | jq .pg_autoctl
   {
     "role": "keeper",
     "monitor": "postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer",
     "formation": "default",
     "group": 0,
     "name": "node1",
     "hostname": "localhost",
     "nodekind": "standalone"
   }
