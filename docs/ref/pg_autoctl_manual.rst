.. _pg_autoctl_manual:

pg_autoctl manual
=================

pg_autoctl manual - Operator-driven FSM operations for manual cluster recovery

The ``pg_autoctl manual`` commands provide low-level control over the
pg_auto_failover finite state machine and supporting services. They are
always available — no ``PG_AUTOCTL_DEBUG`` environment variable is required.

These commands are intended for manual recovery when the automated FSM is
stopped or stuck, and for low-level diagnostic work. Using them while
``pg_autoctl run`` is active can interfere with automated operations.

.. toctree::
   :maxdepth: 1

   pg_autoctl_manual_service_restart

``pg_autoctl manual`` provides the following sub-command groups::

    pg_autoctl manual
    + fsm          Manually drive the keeper FSM (mutating operations)
    + service      Restart pg_autoctl sub-processes or signal the postgres controller
    + monitor      Manually drive monitor RPCs (register / active / version)
    + primary      Manage a PostgreSQL primary server
    + standby      Manage a PostgreSQL standby server
    + coordinator  Manage Citus coordinator node metadata

    pg_autoctl manual fsm
      init    Initialize the keeper's state on-disk
      assign  Assign a new goal state to the keeper
      step    Make a state transition if instructed by the monitor
    + nodes   Manually manage the keeper's nodes list

``pg_autoctl manual fsm step [report|advance]`` normally does both halves of
a step in one call: report the node's current state to the monitor, then
immediately attempt whatever transition the monitor assigns back. Passing
``report`` or ``advance`` as an argument splits that into its two
independently-issuable halves — ``report`` reports the current state and
returns without transitioning; ``advance`` attempts the transition the
monitor already assigned, without re-reporting first. Neither half re-runs
the other, so observing the effect of ``advance`` on the monitor's own view
still needs a following ``report`` (or plain ``step``) call. This is mainly
useful against a node started with ``pgaftest``'s ``no-autopilot`` cluster
modifier (see :ref:`pgaftest <pgaftest>`), where the node-active service
never ticks the FSM on its own — ``step``/``report``/``advance`` become the
only way to move it forward, one call at a time.

    pg_autoctl manual fsm nodes
      get  Get the list of nodes from file (see --disable-monitor)
      set  Set the list of nodes to file (see --disable-monitor)

    pg_autoctl manual service
    + restart  Restart pg_autoctl sub-processes (services)
    + pgctl    Signal the pg_autoctl postgres controller

    pg_autoctl manual service restart
      postgres     Restart the pg_autoctl postgres controller service
      listener     Restart the pg_autoctl monitor listener service
      node-active  Restart the pg_autoctl keeper node-active service

    pg_autoctl manual service pgctl
      on   Signal pg_autoctl postgres service to ensure Postgres is running
      off  Signal pg_autoctl postgres service to ensure Postgres is stopped

    pg_autoctl manual monitor
      register  Register the current node with the monitor
      active    Call in the pg_auto_failover Node Active protocol
      version   Check that monitor version is current; alter extension update if not

    pg_autoctl manual primary
    + slot      Manage replication slot on the primary server
    + adduser   Create users on primary
      defaults  Add default settings to postgresql.conf
      identify  Run the IDENTIFY_SYSTEM replication command on given host

    pg_autoctl manual primary slot
      create  Create a replication slot on the primary server
      drop    Drop a replication slot on the primary server

    pg_autoctl manual primary adduser
      monitor  Add a local user for queries from the monitor
      replica  Add a local user with replication privileges

    pg_autoctl manual standby
      init            Initialize the standby server using pg_basebackup
      rewind          Rewind a demoted primary server using pg_rewind
      crash-recovery  Setup postgres for crash-recovery and start postgres
      promote         Promote a standby server to become writable

    pg_autoctl manual coordinator
      add       Add this node to its formation's coordinator
      activate  Activate this node on its formation's coordinator
      remove    Remove this node from its formation's coordinator
    + update    Update current node's host:port on the coordinator

    pg_autoctl manual coordinator update
      prepare   Prepare a Citus coordinator metadata update
      commit    Commit a Citus coordinator metadata update
      rollback  Rollback a Citus coordinator metadata update
