# pg_autoctl

This directory contains the code the `pg_autoctl` utility, which implements
the facilities needed to operate and run a pg_auto_failover installation.
Such an installation is made of both a monitor and a keeper, and
`pg_autoctl` allows to operate in those two modes.

The `pg_autoctl` binary exposes a full command line with sub-commands. Most
of the commands exposed to the user are compatible with running in the
context of a monitor node or a keeper node.

## Code Structure

The code is organized in the following way:

  - files with a name that starts with `cli_` implement the command line
    facilities, their role is to understand the user's command and then call
    into the implementation code.

  - files with a name that starts with `cli_do_` implement the DEBUG command
    line facilities, that are meant to expose all the `pg_autoctl`
    facilities in a way that make them easy to invoke from the command line,
    for better testability of the software, and shorter interaction loops
    for developers.

  - files with a name that contains `utils`, such as `env_utils.c`,
    `file_utils.c`, or `string_utils.c` implement abstractions and
    facilities used in many places in the rest of the code. Files
    `parsing.[ch]` complete this list and could have been named
    `parsing_utils` really.

  - files with a name that contains `config` are related to handling of the
    configuration files for either a monitor or a keeper instance, and this
    is detailed later in this file.

  - files with a names that start with `ini_` implement our higher level
    facilities to handle configuration written in the INI format.

  - files with a names that starts with `pg` implement abstrations used to
    handle a Postgres service:

	  - the `pgsql` module contains code to query the local Postgres
        instance by using SQL queries, including the connection and result
        parsing code.

	  - the `monitor` module contains code to query the monitor Postgres
        instance by using its SQL API, made with stored procedures, written
        as a C extension to Postgres.

	  - the `pgctl` module contains code to run Postgres commands such as
        `pg_controldata`, `pg_basebackup`, or `pg_ctl`.

	  - the `pgsetup` module contains code that discovers the current status
        of a Postgres PGDATA directory, including whether the Postgres
        service is currently running, on which port, etc.

  - files with a name starting with `service` implement either process
    control or a subprocess in the `pg_autoctl` process tree, and the
    `supervisor.[ch]` files implement the main restart strategy to control
    our process tree, see later for more details.

  - the `primary_standby` file implements facilities to control Postgres
    streaming replication primary and standby nodes and is mainly used from
    the `fsm_transition.c` operations.

  - files with a name that contains `fsm` and `state` implement the
    “client-side” Finite State Machine that controls and implement
    pg_auto_failover.

There are more files needed to implement `pg_autoctl` and the remaining
files have specific charters:

  - the `main.c` file contains the `main(argc, argv)` function and
    initializes our program.

  - the `loop.c` file implements the keeper service.

  - the `keeper_pg_init` module implements the `pg_autoctl create postgres`
    command, which initializes a Postgres node for pg_auto_failover.

  - the `monitor_pg_init` module implements the `pg_autoctl create monitor`
    command, which initializes a monitor node for pg_auto_failover.

  - the `debian` module contains code that recognize if we're given a debian
    style cluster, such as created with `pg_createcluster`, and tools to
    move the configuration files back in PGDATA and allow `pg_autoctl` to
    own that cluster again.

  - the `signals` module implements our signal masks and how we react to
    receiving a SIGHUP or a SIGTERM signal, etc.

  - the `systemd_config` module uses our INI file abstractions to create a
    systemd unit configuration file that can be deployed and registered to
    make `pg_autoctl` a systemd unit service.

## Command Line and Configuration

The `pg_autoctl` tool provides a complex set of commands and sub-commands,
and handles user-given configuration. The configuration is handled in the
INI format and can be all managed through the `pg_autoctl config` commands:

```
Available commands:
  pg_autoctl config
    check  Check pg_autoctl configuration
    get    Get the value of a given pg_autoctl configuration variable
    set    Set the value of a given pg_autoctl configuration variable
```

The modules in `keeper_config` and `monitor_config` define macros allowing
to sync values read from the command line option parsing and the INI file
together.

All the read and edit operations for the configuration of `pg_autoctl` may
be done through the `pg_autoctl get|set` function, rather than having to
open the configuration file.

## Software Architecture

As the `pg_autoctl` tool unifies the management of two different process
with two different modes of operation, the internal structure of the
software reflects that.

  - the monitor parts of the code are designed around the monitor data
    structures:

	  Monitor
	  MonitorConfig
	  LocalPostgresServer
	  PostgresSetup

  - the keeper parts of the code are designed around the keeper data
    structures:

	  Keeper
	  KeeperConfig
	  KeeperStateData
	  LocalPostgresServer
	  PostgresSetup

We already see that we have common modules that are needed in both the
keeper and the monitor, that both have to manage a local Postgres instance.

## Process Supervision and Process Tree

The `pg_autoctl` owns the Postgres service as a sub-process. A typical
process tree for the monitor looks like the following:

```
-+= 84202 dim ./src/bin/pg_autoctl/pg_autoctl run
 |-+- 84205 dim pg_autoctl: start/stop postgres
 | \-+- 84212 dim /Applications/Postgres.app/Contents/Versions/12/bin/postgres -D /private/tmp/plop/m -p 4000 -h *
 |   |--= 84213 dim postgres: logger
 |   |--= 84215 dim postgres: checkpointer
 |   |--= 84216 dim postgres: background writer
 |   |--= 84217 dim postgres: walwriter
 |   |--= 84218 dim postgres: autovacuum launcher
 |   |--= 84219 dim postgres: stats collector
 |   |--= 84220 dim postgres: pg_auto_failover monitor
 |   |--= 84221 dim postgres: logical replication launcher
 |   |--= 84222 dim postgres: pg_auto_failover monitor worker
 |   |--= 84223 dim postgres: pg_auto_failover monitor worker
 |   |--= 84228 dim postgres: dim template1 [local] idle
 |   \--= 84229 dim postgres: autoctl_node pg_auto_failover [local] idle
 \--- 84206 dim pg_autoctl: monitor listener
```

The main process start by `pg_autoctl` is a supervisor process. Its job is
to loop around `waitpid()` and notice when sub-processes are finished. When
the termination of them is not expected, the supervisor then restarts them.

Given this process structure, the lifetime of the Postgres service is tied
to that of the `pg_autoctl` service. Yet, we might want to restart the
`pg_autoctl` code (to install a bugfix, for instance) and avoid restarting
Postgres.

To that end, the supervisor process starts its services by using `fork()`
and then `exec("/path/to/pg_autoctl")`. This allows to later easily stop and
restart that sub-process and load the new binary from disk, then loaded also
the bug fixes that possibly come with the new version.
