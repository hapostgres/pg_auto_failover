.. _pg_autoctl:

pg_autoctl
==========

pg_autoctl - control a pg_auto_failover node

Synopsis
--------

pg_autoctl provides the following commands::

  + create   Create a pg_auto_failover node, or formation
  + drop     Drop a pg_auto_failover node, or formation
  + config   Manages the pg_autoctl configuration
  + show     Show pg_auto_failover information
  + enable   Enable a feature on a formation
  + disable  Disable a feature on a formation
  + get      Get a pg_auto_failover node, or formation setting
  + set      Set a pg_auto_failover node, or formation setting
  + perform  Perform an action orchestrated by the monitor
    run      Run the pg_autoctl service (monitor or keeper)
    stop     signal the pg_autoctl service for it to stop
    reload   signal the pg_autoctl for it to reload its configuration
    status   Display the current status of the pg_autoctl service
    help     print help message
    version  print pg_autoctl version

  pg_autoctl create
    monitor    Initialize a pg_auto_failover monitor node
    postgres   Initialize a pg_auto_failover standalone postgres node
    formation  Create a new formation on the pg_auto_failover monitor

  pg_autoctl drop
    monitor    Drop the pg_auto_failover monitor
    node       Drop a node from the pg_auto_failover monitor
    formation  Drop a formation on the pg_auto_failover monitor

  pg_autoctl config
    check  Check pg_autoctl configuration
    get    Get the value of a given pg_autoctl configuration variable
    set    Set the value of a given pg_autoctl configuration variable

  pg_autoctl show
    uri            Show the postgres uri to use to connect to pg_auto_failover nodes
    events         Prints monitor's state of nodes in a given formation and group
    state          Prints monitor's state of nodes in a given formation and group
    settings       Print replication settings for a formation from the monitor
    standby-names  Prints synchronous_standby_names for a given group
    file           List pg_autoctl internal files (config, state, pid)
    systemd        Print systemd service file for this node

  pg_autoctl enable
    secondary    Enable secondary nodes on a formation
    maintenance  Enable Postgres maintenance mode on this node
    ssl          Enable SSL configuration on this node

  pg_autoctl disable
    secondary    Disable secondary nodes on a formation
    maintenance  Disable Postgres maintenance mode on this node
    ssl          Disable SSL configuration on this node

  pg_autoctl get
  + node       get a node property from the pg_auto_failover monitor
  + formation  get a formation property from the pg_auto_failover monitor

  pg_autoctl get node
    replication-quorum  get replication-quorum property from the monitor
    candidate-priority  get candidate property from the monitor

  pg_autoctl get formation
    settings              get replication settings for a formation from the monitor
    number-sync-standbys  get number_sync_standbys for a formation from the monitor

  pg_autoctl set
  + node       set a node property on the monitor
  + formation  set a formation property on the monitor

  pg_autoctl set node
    metadata            set metadata on the monitor
    replication-quorum  set replication-quorum property on the monitor
    candidate-priority  set candidate property on the monitor

  pg_autoctl set formation
    number-sync-standbys  set number-sync-standbys for a formation on the monitor

  pg_autoctl perform
    failover    Perform a failover for given formation and group
    switchover  Perform a switchover for given formation and group
    promotion   Perform a failover that promotes a target node

Description
-----------

The pg_autoctl tool is the client tool provided by pg_auto_failover to
create and manage Postgres nodes and the pg_auto_failover monitor node. The
command is built with many sub-commands that each have their own manual
page.

Help
----

To get the full recursive list of supported commands, use::

  pg_autoctl help

Version
-------

To grab the version of pg_autoctl that you're using, use::

   pg_autoctl --version
   pg_autoctl version

A typical output would be::

  pg_autoctl version 1.4.2
  pg_autoctl extension version 1.4
  compiled with PostgreSQL 12.3 on x86_64-apple-darwin16.7.0, compiled by Apple LLVM version 8.1.0 (clang-802.0.42), 64-bit
  compatible with Postgres 10, 11, 12, and 13


The version is also available as a JSON document when using the ``--json`` option::

  pg_autoctl --version --json
  pg_autoctl version --json

A typical JSON output would be::

  {
      "pg_autoctl": "1.4.2",
      "pgautofailover": "1.4",
      "pg_major": "12",
      "pg_version": "12.3",
      "pg_version_str": "PostgreSQL 12.3 on x86_64-apple-darwin16.7.0, compiled by Apple LLVM version 8.1.0 (clang-802.0.42), 64-bit",
      "pg_version_num": 120003
  }

This is for version 1.4.2 of pg_auto_failover. This particular version of
the pg_autoctl client tool has been compiled using ``libpq`` for PostgreSQL
12.3 and is compatible with Postgres 10, 11, 12, and 13.
