Introduction to pg_auto_failover
================================

pg_auto_failover is a complete system for operating PostgreSQL in
production. Its ``pg_autoctl`` process may runs `pid 1` or init in a
container based environment and supervises the Postgres ``postmaster``
underneath it. A dedicated monitor node coordinates state across every node
in the cluster: the monitor is a Postgres instance with the
``pgautofailover`` extension installed to implement our inter-node
communication protocol.

Together they provide full cluster management with a dynamic topology: nodes
can be added, removed, and reconfigured while the cluster keeps serving
production traffic, whether driven by an operator's own commands or
automatically by the monitor's own health checks.

Automated failover and full high availability can both be implemented and a
production cluster can evolve from simple failover capabilities to enhanced
data protection settings.

Two modes of operation are available side by side: the traditional
command-driven CLI (``pg_autoctl create ...``, ``pg_autoctl set ...``), and
a specification- file-driven mode, where a single ``node.ini`` file
describes a node's own desired configuration and :ref:`pg_autoctl_node_run`
continuously reconciles reality to match it.

.. _ha_dr_backups:

High Availability and Disaster Recovery: One System
------------------------------------------------------

.. figure:: ./tikz/arch-ha-dr-typical.svg
   :alt: Typical setup, High Availability from Patroni or repmgr, Disaster Recovery and Backups from pgBackRest or pgBarman, two entirely separate boxes

   A typical setup reaches for a product per box: Patroni or repmgr for
   High Availability, pgBackRest or pgBarman for Disaster Recovery and
   Backups

.. figure:: ./tikz/arch-ha-dr-pgautofailover.svg
   :alt: With pg_auto_failover, High Availability and Disaster Recovery collapse into a single box, with Backups (pgBackRest or pgBarman) as the one remaining separate concern

   With pg_auto_failover, High Availability and Disaster Recovery collapse
   into one system; Backups remains its own concern

With RDBMS such as PostgreSQL the concept of High Availability applies to
the service and also the data. Where most PostgreSQL setups treat these as
two separate problems, solved by two separate products, pg_auto_failover
addresses both HA aspects into a single deployment.

Postgres backup systems need to be able Point in Time Recovery, which
requires an archiving implemnentation when using Postgres. Also, Disaster
Recovery is built on-top of PITR. As a consequence, most systems are
implementing Disaster Recovery with their backup software solution, not
their High Availability solution.

Running both solutions together means trusting two different failure
domains, and, very often, discovering only during a real incident that they
were never actually exercised together.

pg_auto_failover starts from a different question: how to make things so
simple to setup and test that they just work once shipped in production?

High Availability of the Postgres service and Disaster Recovery of its data
set are two sides of the same problem, best solved by one system designed
around it rather than by gluing together two tools each designed in
isolation.

The same monitor that orchestrates failover also tracks every archiver's
captured WAL and base backups; the same WAL stream and base backups a
failover election already depends on to guarantee no data loss are what
disaster recovery, including point-in-time recovery, is built on.

High Availability and Disaster Recovery come from a single package, with a
single control plane, rather than from two independently-operated systems
that are only put to the test when a production incident happens.

Backups — in the narrower sense of long-term retention, cataloguing, and
cloud storage tiers — remain their own concern, typically still handled by a
dedicated tool like pgBackRest or pgBarman.

Single Standby Architecture
---------------------------

.. figure:: ./tikz/arch-single-standby.svg
   :alt: pg_auto_failover Architecture with a primary and a standby node

   pg_auto_failover architecture with a primary and a standby node

pg_auto_failover implements Business Continuity for your PostgreSQL
services. pg_auto_failover implements a single PostgreSQL service using
multiple nodes with automated failover, and automates PostgreSQL maintenance
operations in a way that guarantees availability of the service to its users
and applications.

To that end, pg_auto_failover uses three nodes (machines, servers) per PostgreSQL
service:

  - a PostgreSQL primary node,
  - a PostgreSQL secondary node, using Synchronous Hot Standby,
  - a pg_auto_failover Monitor node that acts both as a witness and an orchestrator.

The pg_auto_failover Monitor implements a state machine and relies on
in-core PostgreSQL facilities to deliver HA. For example. when the
*secondary* node is detected to be unavailable, or when its lag is reported
above a defined threshold (the default is 1 WAL files, or 16MB, see the
`pgautofailover.promote_wal_log_threshold` GUC on the pg_auto_failover
monitor), then the Monitor removes it from the `synchronous_standby_names`
setting on the *primary* node. Until the *secondary* is back to being
monitored healthy, failover and switchover operations are not allowed,
preventing data loss.

.. _archiving_and_disaster_recovery:

Archiving & Disaster Recovery Architecture
-------------------------------------------

.. figure:: ./tikz/arch-archiver.svg
   :alt: pg_auto_failover Architecture with a primary, a standby, and an archiver

   pg_auto_failover architecture with a primary, a standby, and an archiver

An **archiver** is a separate node, added on top of any of the architectures
on this page — it applies just as well to the single-standby setup above as
it does to a multi-standby fleet, since it addresses a different concern:
disaster recovery, independent of how many nodes currently participate in
the failover quorum.

An archiver then register archiving nodes to groups on formations managed by
the monitor it reports to. An archiving node is running ``pg_receivewal`` to
maintain the Postgres PITR archive storage, and schedules regular base
backup activity using ``pg_basebackup``. The *archiving node* reports to the
pg_auto_failover Monitor and participates in a group Finite State Machine:
it reports its WAL position and can be used in the replication quorum, and
other nodes in the same group can fetch WAL from an *archiving node* (see
REPORT_LSN and FORWARD_LSN states in the :ref:`failover_state_machine`:.

For that, pg_auto_failover implements its own server-side implementation of
the PostgreSQL replication protocol, a ``pg_walsender`` process that knows
how to serve the data from the archive local on-disk location (or remote
Cloud Object Storage) to the PostgreSQL client replication tools already
listed: ``pg_basebackup`` and `pg_receivewal``, as described in more details
in :ref:`archiving_architecture`.

Multiple Standby Architecture
-----------------------------

.. figure:: ./tikz/arch-multi-standby.svg
   :alt: pg_auto_failover Architecture for a standalone PostgreSQL service

   pg_auto_failover architecture with a primary and two standby nodes

In the pictured architecture, pg_auto_failover implements Business Continuity
and data availability by implementing a single PostgreSQL service using
multiple with automated failover and data redundancy. Even after losing any
Postgres node in a production system, this architecture maintains two copies of
the data on two different nodes.

When using more than one standby, different architectures can be achieved
with pg_auto_failover, depending on the objectives and trade-offs needed for
your production setup.

Multiple Standbys Architecture with 3 standby nodes, one async
--------------------------------------------------------------

.. figure:: ./tikz/arch-three-standby-one-async.svg
   :alt: pg_auto_failover architecture with a primary and three standby nodes

   pg_auto_failover architecture with a primary and three standby nodes

When setting the three parameters above, it's possible to design very
different Postgres architectures for your production needs.

In this case, the system is setup with two standby nodes participating in
the replication quorum, allowing for ``number_sync_standbys = 1``. The
system always maintains a minimum of two copies of the data set: one on the
primary, another one on either node B or node C. Whenever we lose one
of those nodes, we can hold to this guarantee of two copies of the data set.

Adding to that, we have the standby server D which has been set up to not
participate in the replication quorum. Node D will not be found in the
``synchronous_standby_names`` list of nodes. Also, node D is set up in a way to
never be a candidate for failover, with ``candidate-priority = 0``.

This architecture would fit a situation where nodes A, B, and C are deployed
in the same data center or availability zone, and node D in another. Those
three nodes are set up to support the main production traffic and implement
high availability of both the Postgres service and the data set.

Node D might be set up for Business Continuity in case the first data center
is lost, or maybe for reporting the need for deployment on another
application domain.

Citus Architecture
------------------

.. figure:: ./tikz/arch-citus.svg
   :alt: pg_auto_failover architecture with a Citus formation

   pg_auto_failover architecture with a Citus formation

pg_auto_failover implements Business Continuity for your Citus services.
pg_auto_failover implements a single Citus formation service using multiple
Citus nodes with automated failover, and automates PostgreSQL maintenance
operations in a way that guarantees availability of the service to its users
and applications.

In that case, pg_auto_failover knows how to orchestrate a Citus coordinator
failover and a Citus worker failover. A Citus worker failover can be
achieved with a very minimal downtime to the application, where during a
short time window SQL writes may error out.

In this figure we see a single standby node for each Citus node, coordinator
and workers. It is possible to implement more standby nodes, and even
read-only nodes for load balancing, see :ref:`citus_secondaries`.
