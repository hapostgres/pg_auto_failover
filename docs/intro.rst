Introduction to pg_auto_failover
================================

pg_auto_failover is an extension for PostgreSQL that monitors and manages
failover for postgres clusters. It is optimised for simplicity and
correctness.

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

.. _archiving_architecture:

Archiving & Disaster Recovery Architecture
-------------------------------------------

.. figure:: ./tikz/arch-archiver.svg
   :alt: pg_auto_failover Architecture with a primary, a standby, and an archiver

   pg_auto_failover architecture with a primary, a standby, and an archiver

An **archiver** is a separate physical entity, added on top of any of the
architectures on this page — it applies just as well to the single-standby
setup above as it does to a multi-standby fleet, since it addresses a
different concern: disaster recovery, independent of how many nodes
currently participate in the failover quorum.

Unlike a standby, an archiver holds no copy of the primary's data directory
and never takes writes or reads for the application. It runs its own
`pg_receivewal`__ continuously against the group's current primary,
capturing every WAL segment into a local cache the moment it's generated,
and periodically produces full base backups from that cache. Both are
reported back to the pg_auto_failover Monitor, the same way a standby
reports its own replication state — so the Monitor can tell an operator,
or a client library, when a given segment has landed durably on enough
archivers to be considered safe (``archiver_quorum``), and where the most
recent base backup lives.

__ https://www.postgresql.org/docs/current/app-pgreceivewal.html

The pg_auto_failover Monitor tracks an archiver's participation in a group
as its own node, in the ``archiving`` **archiving node** state — reported
and monitored the same way ``primary``/``secondary`` are, but never a
candidate for promotion or failover: an archiving node holds no
`PGDATA`__ of its own, so there is nothing to promote it *to*.

__ https://www.postgresql.org/docs/current/app-initdb.html

Because the archiver keeps a complete, continuously updated copy of the
group's WAL stream and periodic base backups independent of any single
standby, it serves two purposes beyond ordinary high availability: a new
node can be provisioned straight from an archiver's cache instead of
placing extra load on a live primary or secondary, and a group that has
lost every other node still has everything needed to rebuild from scratch,
as long as the archiver itself survived.

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
