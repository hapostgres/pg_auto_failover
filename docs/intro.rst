Introduction to pg_auto_failover
================================

pg_auto_failover is an extension for PostgreSQL that monitors and manages failover
for a postgres clusters. It is optimised for simplicity and correctness.

Single Standby Architecture
---------------------------

.. figure:: ./tikz/arch-single-standby.svg
   :alt: pg_auto_failover Architecture with a primary and a standby node

   pg_auto_failover architecture with a primary and a standby node

pg_auto_failover implements Business Continuity for your PostgreSQL
services. pg_auto_failover implements a single PostgreSQL service using multiple
nodes with automated failover, and automates PostgreSQL maintenance
operations in a way that guarantees availability of the service to its users
and applications.

To that end, pg_auto_failover uses three nodes (machines, servers) per PostgreSQL
service:

  - a PostgreSQL primary node,
  - a PostgreSQL secondary node, using Synchronous Hot Standby,
  - a pg_auto_failover Monitor node that acts both as a witness and an orchestrator.

The pg_auto_failover Monitor implements a state machine and relies on in-core
PostgreSQL facilities to deliver HA. For example. when the *secondary* node
is detected to be unavailable, or when its lag is reported above a defined
threshold (the default is 1 WAL files, or 16MB, see the
`pgautofailover.promote_wal_log_threshold` GUC on the pg_auto_failover monitor), then the
Monitor removes it from the `synchronous_standby_names` setting on the
*primary* node. Until the *secondary* is back to being monitored healthy,
failover and switchover operations are not allowed, preventing data loss.

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
primary, another one on one on either node B or node D. Whenever we lose one
of those nodes, we can hold to this guarantee of two copies of the data set.

Adding to that, we have the standby server C which has been set up to not
participate in the replication quorum. Node C will not be found in the
``synchronous_standby_names`` list of nodes. Also, node C is set up in a way to
never be a candidate for failover, with ``candidate-priority = 0``.

This architecture would fit a situation where nodes A, B, and D are deployed
in the same data center or availability zone, and node C in another.
Those three nodes are set up to support the main production traffic and
implement high availability of both the Postgres service and the data set.

Node C might be set up for Business Continuity in case the first data center is
lost, or maybe for reporting the need for deployment on another application
domain.
