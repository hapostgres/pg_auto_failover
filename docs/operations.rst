Operating pg_auto_failover
==========================

This section is not yet complete. Please contact us with any questions.

Deployment
----------

pg_auto_failover is a general purpose tool for setting up PostgreSQL
replication in order to implement High Availability of the PostgreSQL
service.

Provisioning
------------

It is also possible to register pre-existing PostgreSQL instances with a
pg_auto_failover monitor. The ``pg_autoctl create`` command honors the ``PGDATA``
environment variable, and checks whether PostgreSQL is already running. If
Postgres is detected, the new node is registered in SINGLE mode, bypassing
the monitor's role assignment policy.

Operations
----------

The main operations with pg_auto_failover are:

  - maintenance of a secondary node

    It is possible to put a secondary node in any group in a MAINTENANCE
    state, so that the Postgres server is not doing *synchronous
    replication* anymore and can be taken down for maintenance purposes,
    such as security kernel upgrades or the like.

    The monitor exposes the following API to schedule maintenance operations
    on a secondary node::

      $ psql postgres://autoctl_node@monitor/pg_auto_failover
      > select pgautofailover.start_maintenance('nodename', 5432);
      > select pgautofailover.stop_maintenance('nodename', 5432);

    When a standby node is in maintenance, the monitor sets the primary node
    replication to WAIT_PRIMARY: in this role, the PostgreSQL streaming
    replication is now asynchronous and the standby PostgreSQL server may be
    stopped, rebooted, etc.

    pg_auto_failover does not provide support for primary server maintenance.

  - triggering a failover

    It is possible to trigger a failover manually with pg_auto_failover, by using
    the SQL API provided by the monitor::

      $ psql postgres://autoctl_node@monitor/pg_auto_failover
      > select pgautofailover.perform_failover(formation_id => 'default', group_id => 0);

    To call the function, you need to figure out the formation and group of
    the group where the failover happens. The following commands when run on
    a pg_auto_failover keeper node provide for the necessary information::

      $ export PGDATA=...
      $ pg_autoctl config get pg_autoctl.formation
      $ pg_autoctl config get pg_autoctl.group


Current state, last events
--------------------------

The following commands display information from the pg_auto_failover monitor tables
``pgautofailover.node`` and ``pgautofailover.event``:

::

  $ pg_autoctl show state
  $ pg_autoctl show events

When run on the monitor, the commands outputs all the known states and
events for the whole set of formations handled by the monitor. When run on a
PostgreSQL node, the command connects to the monitor and outputs the
information relevant to the service group of the local node only.

For interactive debugging it is helpful to run the following command from
the monitor node while e.g. initializing a formation from scratch, or
performing a manual failover::

  $ watch pg_autoctl show state

Monitoring pg_auto_failover in Production
-----------------------------------------

The monitor reports every state change decision to a LISTEN/NOTIFY channel
named ``state``. PostgreSQL logs on the monitor are also stored in a table,
``pgautofailover.event``, and broadcast by NOTIFY in the channel ``log``.

Trouble-Shooting Guide
----------------------

pg_auto_failover commands can be run repeatedly. If initialization fails the first
time -- for instance because a firewall rule hasn't yet activated -- it's
possible to try ``pg_autoctl create`` again. pg_auto_failover will review its previous
progress and repeat idempotent operations (``create database``, ``create
extension`` etc), gracefully handling errors.
