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

The command line tool ``pg_autoctl`` exposes an API to schedule maintenance
operations on the current node, which must be a secondary node at the moment
when maintenance is requested.

Here's an example of using the maintenance commands on a secondary node,
including the output. Of course, when you try that on your own nodes, dates
and PID information might differ::

  $ pg_autoctl enable maintenance
  11:47:20 47247 INFO  Listening monitor notifications about state changes in formation "default" and group 0
  11:47:20 47247 INFO  Following table displays times when notifications are received
      Time |  ID |      Host |   Port |      Current State |     Assigned State
  ---------+-----+-----------+--------+--------------------+-------------------
  11:47:20 |   2 | localhost |   5002 |            primary |       wait_primary
  11:47:20 |   1 | localhost |   5001 |          secondary |        maintenance
  11:47:20 |   1 | localhost |   5001 |        maintenance |        maintenance

The command listens to the state changes in the current node's formation and
group on the monitor and displays those changes as it receives them. The
operation is done when the node has reached the ``maintenance`` state.

It is now possible to disable maintenance to allow ``pg_autoctl`` to manage
this standby node again::

  $ pg_autoctl disable maintenance
  11:47:37 47364 INFO  Listening monitor notifications about state changes in formation "default" and group 0
  11:47:37 47364 INFO  Following table displays times when notifications are received
      Time |  ID |      Host |   Port |      Current State |     Assigned State
  ---------+-----+-----------+--------+--------------------+-------------------
  11:47:38 |   1 | localhost |   5001 |        maintenance |         catchingup
  11:47:38 |   1 | localhost |   5001 |         catchingup |         catchingup
  11:47:39 |   1 | localhost |   5001 |         catchingup |          secondary
  11:47:39 |   2 | localhost |   5002 |       wait_primary |            primary
  11:47:39 |   1 | localhost |   5001 |          secondary |          secondary

When a standby node is in maintenance, the monitor sets the primary node
replication to WAIT_PRIMARY: in this role, the PostgreSQL streaming
replication is now asynchronous and the standby PostgreSQL server may be
stopped, rebooted, etc.

Maintenance of a primary node
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A primary node must be available at all times in any formation and group in
pg_auto_failover, that is the invariant provided by the whole solution. With
that in mind, the only way to allow a primary node to go to a maintenance
mode is to first failover and promote the secondary node.

The same command ``pg_autoctl enable maintenance`` implements that operation
when run on a primary node with the option ``--allow-failover``. Here is an
example of such an operation::

  $ pg_autoctl enable maintenance
  11:53:03 50526 WARN  Enabling maintenance on a primary causes a failover
  11:53:03 50526 FATAL Please use --allow-failover to allow the command proceed

As we can see the option ``allow-maintenance`` is mandatory. In the next
example we use it::

  $ pg_autoctl enable maintenance --allow-failover
  13:13:42 1614 INFO  Listening monitor notifications about state changes in formation "default" and group 0
  13:13:42 1614 INFO  Following table displays times when notifications are received
      Time |  ID |      Host |   Port |       Current State |      Assigned State
  ---------+-----+-----------+--------+---------------------+--------------------
  13:13:43 |   2 | localhost |   5002 |             primary | prepare_maintenance
  13:13:43 |   1 | localhost |   5001 |           secondary |   prepare_promotion
  13:13:43 |   1 | localhost |   5001 |   prepare_promotion |   prepare_promotion
  13:13:43 |   2 | localhost |   5002 | prepare_maintenance | prepare_maintenance
  13:13:44 |   1 | localhost |   5001 |   prepare_promotion |    stop_replication
  13:13:45 |   1 | localhost |   5001 |    stop_replication |    stop_replication
  13:13:46 |   1 | localhost |   5001 |    stop_replication |        wait_primary
  13:13:46 |   2 | localhost |   5002 | prepare_maintenance |         maintenance
  13:13:46 |   1 | localhost |   5001 |        wait_primary |        wait_primary
  13:13:47 |   2 | localhost |   5002 |         maintenance |         maintenance

Once the primary has reached the ``maintenance`` state then the secondary is
promoted, which we don't see here. When the operation is done we can have
the old primary re-join the group, this time as a secondary::

  $ pg_autoctl disable maintenance
  13:14:46 1985 INFO  Listening monitor notifications about state changes in formation "default" and group 0
  13:14:46 1985 INFO  Following table displays times when notifications are received
      Time |  ID |      Host |   Port |       Current State |      Assigned State
  ---------+-----+-----------+--------+---------------------+--------------------
  13:14:47 |   2 | localhost |   5002 |         maintenance |          catchingup
  13:14:47 |   2 | localhost |   5002 |          catchingup |          catchingup
  13:14:52 |   2 | localhost |   5002 |          catchingup |           secondary
  13:14:52 |   1 | localhost |   5001 |        wait_primary |             primary
  13:14:52 |   2 | localhost |   5002 |           secondary |           secondary
  13:14:53 |   1 | localhost |   5001 |             primary |             primary


Triggering a failover
^^^^^^^^^^^^^^^^^^^^^

It is possible to trigger a manual failover, or a switchover, using the
command ``pg_autoctl perform failover``. Here's an example of what happens
when running the command::

  $ pg_autoctl perform failover
  11:58:00 53224 INFO  Listening monitor notifications about state changes in formation "default" and group 0
  11:58:00 53224 INFO  Following table displays times when notifications are received
      Time |  ID |      Host |   Port |      Current State |     Assigned State
  ---------+-----+-----------+--------+--------------------+-------------------
  11:58:01 |   1 | localhost |   5001 |            primary |           draining
  11:58:01 |   2 | localhost |   5002 |          secondary |  prepare_promotion
  11:58:01 |   1 | localhost |   5001 |           draining |           draining
  11:58:01 |   2 | localhost |   5002 |  prepare_promotion |  prepare_promotion
  11:58:02 |   2 | localhost |   5002 |  prepare_promotion |   stop_replication
  11:58:02 |   1 | localhost |   5001 |           draining |     demote_timeout
  11:58:03 |   1 | localhost |   5001 |     demote_timeout |     demote_timeout
  11:58:04 |   2 | localhost |   5002 |   stop_replication |   stop_replication
  11:58:05 |   2 | localhost |   5002 |   stop_replication |       wait_primary
  11:58:05 |   1 | localhost |   5001 |     demote_timeout |            demoted
  11:58:05 |   2 | localhost |   5002 |       wait_primary |       wait_primary
  11:58:05 |   1 | localhost |   5001 |            demoted |            demoted
  11:58:06 |   1 | localhost |   5001 |            demoted |         catchingup
  11:58:06 |   1 | localhost |   5001 |         catchingup |         catchingup
  11:58:08 |   1 | localhost |   5001 |         catchingup |          secondary
  11:58:08 |   2 | localhost |   5002 |       wait_primary |            primary
  11:58:08 |   1 | localhost |   5001 |          secondary |          secondary
  11:58:08 |   2 | localhost |   5002 |            primary |            primary

Again, timings and PID numbers are not expected to be the same when you run
the command on your own setup.

Also note in the output that the command shows the whole set of transitions
including when the old primary is now a secondary node. The database is
available for read-write traffic as soon as we reach the state
``wait_primary``.

Implementing a controlled switchover
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

It is generally useful to distinguish a *controlled switchover* to a
*failover*. In a controlled switchover situation it is possible to organise
the sequence of events in a way to avoid data loss and lower downtime to a
minimum.

In the case of pg_auto_failover, because we use **synchronous replication**,
we don't face data loss risks when triggering a manual failover. Moreover,
our monitor knows the current primary health at the time when the failover
is triggered, and drives the failover accordingly.

So to trigger a controlled switchover with pg_auto_failover you can use the
same API as for a manual failover::

  $ pg_autoctl perform switchover

Because the subtelties of orchestrating either a controlled switchover or an
unplanned failover are all handled by the monitor, rather than the client
side command line, at the client level the two command ``pg_autoctl perform
failover`` and ``pg_autoctl perform switchover`` are synonyms, or aliases.

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
