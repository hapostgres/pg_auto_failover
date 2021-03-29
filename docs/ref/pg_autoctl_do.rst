.. _pg_autoctl_do:

pg_autoctl do
=============

pg_autoctl do - Internal commands and internal QA tooling

The debug commands for ``pg_autoctl`` are only available when the
environment variable ``PG_AUTOCTL_DEBUG`` is set (to any value).

When testing pg_auto_failover, it is helpful to be able to play with the
local nodes using the same lower-level API as used by the pg_auto_failover
Finite State Machine transitions. Some commands could be useful in contexts
other than pg_auto_failover development and QA work, so some documentation
has been made available.

.. toctree::
   :maxdepth: 1

   pg_autoctl_do_tmux
   pg_autoctl_do_demo
   pg_autoctl_do_service_restart
   pg_autoctl_do_show
   pg_autoctl_do_pgsetup

The low-level API is made available through the following ``pg_autoctl do``
commands, only available in debug environments::

    pg_autoctl do
    + monitor  Query a pg_auto_failover monitor
    + fsm      Manually manage the keeper's state
    + primary  Manage a PostgreSQL primary server
    + standby  Manage a PostgreSQL standby server
    + show     Show some debug level information
    + pgsetup  Manage a local Postgres setup
    + pgctl    Signal the pg_autoctl postgres service
    + service  Run pg_autoctl sub-processes (services)
    + tmux     Set of facilities to handle tmux interactive sessions
    + azure    Manage a set of Azure resources for a pg_auto_failover demo
    + demo     Use a demo application for pg_auto_failover

    pg_autoctl do monitor
    + get                 Get information from the monitor
      register            Register the current node with the monitor
      active              Call in the pg_auto_failover Node Active protocol
      version             Check that monitor version is 1.5.0.1; alter extension update if not
      parse-notification  parse a raw notification message

    pg_autoctl do monitor get
      primary      Get the primary node from pg_auto_failover in given formation/group
      others       Get the other nodes from the pg_auto_failover group of hostname/port
      coordinator  Get the coordinator node from the pg_auto_failover formation

    pg_autoctl do fsm
      init    Initialize the keeper's state on-disk
      state   Read the keeper's state from disk and display it
      list    List reachable FSM states from current state
      gv      Output the FSM as a .gv program suitable for graphviz/dot
      assign  Assign a new goal state to the keeper
      step    Make a state transition if instructed by the monitor
    + nodes   Manually manage the keeper's nodes list

    pg_autoctl do fsm nodes
      get  Get the list of nodes from file (see --disable-monitor)
      set  Set the list of nodes to file (see --disable-monitor)

    pg_autoctl do primary
    + slot      Manage replication slot on the primary server
    + adduser   Create users on primary
      defaults  Add default settings to postgresql.conf
      identify  Run the IDENTIFY_SYSTEM replication command on given host

    pg_autoctl do primary slot
      create  Create a replication slot on the primary server
      drop    Drop a replication slot on the primary server

    pg_autoctl do primary adduser
      monitor  add a local user for queries from the monitor
      replica  add a local user with replication privileges

    pg_autoctl do standby
      init     Initialize the standby server using pg_basebackup
      rewind   Rewind a demoted primary server using pg_rewind
      promote  Promote a standby server to become writable

    pg_autoctl do show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      hostname  Print this node's default hostname
      reverse   Lookup given hostname and check reverse DNS setup

    pg_autoctl do pgsetup
      pg_ctl    Find a non-ambiguous pg_ctl program and Postgres version
      discover  Discover local PostgreSQL instance, if any
      ready     Return true is the local Postgres server is ready
      wait      Wait until the local Postgres server is ready
      logs      Outputs the Postgres startup logs
      tune      Compute and log some Postgres tuning options

    pg_autoctl do pgctl
      on   Signal pg_autoctl postgres service to ensure Postgres is running
      off  Signal pg_autoctl postgres service to ensure Postgres is stopped

    pg_autoctl do service
    + getpid        Get the pid of pg_autoctl sub-processes (services)
    + restart       Restart pg_autoctl sub-processes (services)
      pgcontroller  pg_autoctl supervised postgres controller
      postgres      pg_autoctl service that start/stop postgres when asked
      listener      pg_autoctl service that listens to the monitor notifications
      node-active   pg_autoctl service that implements the node active protocol

    pg_autoctl do service getpid
      postgres     Get the pid of the pg_autoctl postgres controller service
      listener     Get the pid of the pg_autoctl monitor listener service
      node-active  Get the pid of the pg_autoctl keeper node-active service

    pg_autoctl do service restart
      postgres     Restart the pg_autoctl postgres controller service
      listener     Restart the pg_autoctl monitor listener service
      node-active  Restart the pg_autoctl keeper node-active service

    pg_autoctl do tmux
      script   Produce a tmux script for a demo or a test case (debug only)
      session  Run a tmux session for a demo or a test case
      stop     Stop pg_autoctl processes that belong to a tmux session
      wait     Wait until a given node has been registered on the monitor
      clean    Clean-up a tmux session processes and root dir

    pg_autoctl do azure
    + provision  provision azure resources for a pg_auto_failover demo
    + tmux       Run a tmux session with an Azure setup for QA/testing
    + show       show azure resources for a pg_auto_failover demo
      deploy     Deploy a pg_autoctl VMs, given by name
      create     Create an azure QA environment
      drop       Drop an azure QA environment: resource group, network, VMs
      ls         List resources in a given azure region
      ssh        Runs ssh -l ha-admin <public ip address> for a given VM name
      sync       Rsync pg_auto_failover sources on all the target region VMs

    pg_autoctl do azure provision
      region  Provision an azure region: resource group, network, VMs
      nodes   Provision our pre-created VM with pg_autoctl Postgres nodes

    pg_autoctl do azure tmux
      session  Create or attach a tmux session for the created Azure VMs
      kill     Kill an existing tmux session for Azure VMs

    pg_autoctl do azure show
      ips    Show public and private IP addresses for selected VMs
      state  Connect to the monitor node to show the current state

    pg_autoctl do demo
      run      Run the pg_auto_failover demo application
      uri      Grab the application connection string from the monitor
      ping     Attempt to connect to the application URI
      summary  Display a summary of the previous demo app run
