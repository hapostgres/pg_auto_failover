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

.. _pg_auto_failover_security:

Security
--------

Connections between monitor and data nodes use *trust* authentication by
default. This lets accounts used by ``pg_auto_failover`` to connect to nodes
without needing a password. Default behaviour could be changed using ``--auth``
parameter when creating monitor or data Node. Any auth method supported by
PostgreSQL could be used here. Please refer to `PostgreSQL pg_hba documentation`__
for available options.

__ https://www.postgresql.org/docs/current/auth-pg-hba-conf.html

Security for following connections should be considered when setting up
`.pgpass` file.

  1. health check connection from monitor for `autoctl` user to both `postgres` and `pg_auto_failover` databases.
  2. connections for `pg_autoctl` command from data nodes to monitor for `autoctl_node` user.
  3. replication connections from secondary to primary data nodes for `replication` user.
     Notice that primary and secondary nodes change during failover. Thus this setting
     should be done on both primary and secondary nodes.
  4. settings need to be updated after a new node is added.

See `PostgreSQL documentation`__ on setting up `.pgpass` file.

__ https://www.postgresql.org/docs/current/libpq-pgpass.html


Operations
----------

It is possible to operate pg_auto_failover formations and groups directly
from the monitor. All that is needed is an access to the monitor Postgres
database as a client, such as ``psql``. It's also possible to add those
management SQL function calls in your own ops application if you have one.

For security reasons, the ``autoctl_node`` is not allowed to perform
maintenance operations. This user is limited to what ``pg_autoctl`` needs.
You can either create a specific user and authentication rule to expose for
management, or edit the default HBA rules for the ``autoctl`` user. In the
following examples we're directly connecting as the ``autoctl`` role.

The main operations with pg_auto_failover are node maintenance and manual
failover, also known as a controlled switchover.

Maintenance of a secondary node
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is possible to put a secondary node in any group in a MAINTENANCE state,
so that the Postgres server is not doing *synchronous replication* anymore
and can be taken down for maintenance purposes, such as security kernel
upgrades or the like.

The monitor exposes the following API to schedule maintenance operations on
a secondary node::

  $ psql postgres://autoctl@monitor/pg_auto_failover
  > select pgautofailover.start_maintenance('nodename', 5432);
  > select pgautofailover.stop_maintenance('nodename', 5432);

The command line tool ``pg_autoctl`` also exposes an API to schedule
maintenance operations on the current node, which must be a secondary node
at the moment when maintenance is requested::

  $ pg_autoctl enable maintenance
  ...
  $ pg_autoctl disable maintenance

When a standby node is in maintenance, the monitor sets the primary node
replication to WAIT_PRIMARY: in this role, the PostgreSQL streaming
replication is now asynchronous and the standby PostgreSQL server may be
stopped, rebooted, etc.

pg_auto_failover does not provide support for primary server maintenance.

Triggering a failover
^^^^^^^^^^^^^^^^^^^^^

It is possible to trigger a failover manually with pg_auto_failover, by
using the SQL API provided by the monitor::

  $ psql postgres://autoctl@monitor/pg_auto_failover
  > select pgautofailover.perform_failover(formation_id => 'default', group_id => 0);

To call the function, you need to figure out the formation and group of the
group where the failover happens. The following commands when run on a
pg_auto_failover keeper node provide for the necessary information::

  $ export PGDATA=...
  $ pg_autoctl config get pg_autoctl.formation
  $ pg_autoctl config get pg_autoctl.group

Implementing a controlled switchover
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is generally useful to distinguish a *controlled switchover* to a
*failover*. In a controlled switchover situation it is possible to organise
the sequence of events in a way to avoid data loss and lower downtime to a
minimum.

In the case of pg_auto_failover, because we use **synchronous replication**,
we don't face data loss risks when triggering a manual failover. Moreover,
our monitor knows the current primary health at the time when the failover
is triggerred, and drives the failover accordingly.

So to trigger a controlled switchover with pg_auto_failover you can use the
same API as for a manual failover::

  $ psql postgres://autoctl@monitor/pg_auto_failover
  > select pgautofailover.perform_failover(formation_id => 'default', group_id => 0);

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
