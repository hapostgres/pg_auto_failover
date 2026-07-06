.. _pg_autoctl_inspect:

pg_autoctl inspect
==================

pg_autoctl inspect - Read-only diagnostics for a pg_auto_failover node

The ``pg_autoctl inspect`` commands provide read-only access to local and
cluster state. They are always available — no ``PG_AUTOCTL_DEBUG`` environment
variable is required. All commands in this group are safe to run while
``pg_autoctl run`` is active.

.. toctree::
   :maxdepth: 1

   pg_autoctl_inspect_pgsetup
   pg_autoctl_inspect_show

``pg_autoctl inspect`` provides the following sub-command groups::

    pg_autoctl inspect
    + show     Network and hostname diagnostics
    + pgsetup  Local PostgreSQL setup inspection
    + fsm      Display keeper FSM state and transitions (read-only)
    + monitor  Query the monitor's current state (read-only)
    + getpid   Get the pid of pg_autoctl sub-processes (services)

    pg_autoctl inspect pgsetup
      pg_ctl    Find a non-ambiguous pg_ctl program and Postgres version
      discover  Discover local PostgreSQL instance, if any
      ready     Return true if the local Postgres server is ready
      wait      Wait until the local Postgres server is ready
      logs      Outputs the Postgres startup logs
      tune      Compute and log some Postgres tuning options

    pg_autoctl inspect fsm
      state  Read the keeper's state from disk and display it
      list   List reachable FSM states from current state
      gv     Output the FSM as a .gv program suitable for graphviz/dot

    pg_autoctl inspect show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      hostname  Print this node's default hostname
      reverse   Lookup given hostname and check reverse DNS setup

    pg_autoctl inspect monitor
      get                 Get information from the monitor
      parse-notification  Parse a raw notification message

    pg_autoctl inspect monitor get
      primary      Get the primary node from pg_auto_failover in given formation/group
      others       Get the other nodes from the pg_auto_failover group of hostname/port
      coordinator  Get the coordinator node from the pg_auto_failover formation

    pg_autoctl inspect getpid
      postgres     Get the pid of the pg_autoctl postgres controller service
      listener     Get the pid of the pg_autoctl monitor listener service
      node-active  Get the pid of the pg_autoctl keeper node-active service
