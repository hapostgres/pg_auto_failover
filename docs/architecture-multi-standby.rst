Multi-nodes Architectures
=========================

pg_auto_failover also allows to have more than one standby node with
advanced control over your production architecture characteristics.

Architecture with two standby nodes
-----------------------------------

When adding your second standby node with the default setting you get the
following architecture:

.. figure:: ./tikz/arch-multi-standby.svg
   :alt: pg_auto_failover architecture with two standby nodes

   pg_auto_failover architecture with two standby nodes

In this case three nodes have been setup with the same characteristics,
allowing to achieve HA for both the Postgres service and the production data
set. With ``number_sync_standbys`` set to one, this architecture always
maintains two copies of the data set: one on the current primary node (node
A in the previous diagram), and one on the standby that acknowledges the
transaction first, either node B or node C in the diagram.

When one of the standby nodes is unavailable, the second copy of the data
set can be maintained thanks to the remaining standby and without
intervention to the Postgres settings.

Then, contrary to the default one-standby setup of pg_auto_failover where we
have ``number_sync_standbys`` set to zero, if the second standby node were
to become unhealthy at the same time as the first standby node, the Postgres
service would be degraded to read-only: transactions that write to Postgres
will have to wait until at least one standby node is healthy again.

Note that with pg_auto_failover it is possible to setup your production
architecture with ``number_sync_standbys`` set to zero to allow data to be
written even when both the standby nodes are down. In this case, a single
copy of the production data set is kept, and if the primary was then to
fail, some amount data will be lost. How much depends on your backup and
recovery mechanisms.

.. _architecture_setup:

Postgres Architecture Setup
---------------------------

The entire flexibility of pg_auto_failover can be leveraged with the
following three replication settings:

  - Number sync stanbys
  - Replication quorum
  - Candidate priority

Number Sync Standbys
^^^^^^^^^^^^^^^^^^^^

This parameter is used by Postgres in the `synchronous_standby_names`__
parameter: ``number_sync_standby`` is the number of synchronous standbys
that transactions need to wait for replies from.

__ https://www.postgresql.org/docs/current/runtime-config-replication.html#GUC-SYNCHRONOUS-STANDBY-NAMES

This parameter can be set at the *formation* level in pg_auto_failover,
meaning that it applies to the current primary and follow a failover to
apply to any new primary that might replace the current one.

To set this parameter to the value ``<n>``, use the following command::

  pg_autoctl set formation number-sync-standbys <n>

The default value in pg_auto_failover is zero. When set to zero, the
Postgres parameter ``synchronous_standby_names`` can be set to either
``'*'`` or to ``''``:

  - ``synchronous_standby_names = '*'`` means that any standby may
    participate in the replication quorum for transactions with
    ``synchronous_commit`` set to ``on`` or higher values.

    pg_autofailover uses ``synchronous_standby_names = '*'`` when there's at
	least one standby that is known to be healthy.

  - ``synchronous_standby_names = ''`` (empty string) disables synchrous
    commit and makes all your commits asynchronous, meaning that transaction
    commits will not wait for replication. In other words, a single copy of
    your production data is maintained when ``synchronous_standby_names`` is
    set that way.

    pg_autofailover uses ``synchronous_standby_names = ''`` only when
	number_sync_standbys is set to zero and there's no standby node known
	healthy by the monitor.

In order to set ``number_sync_standbys`` to a non-zero value,
pg_auto_failover requires that at least ``number_sync_standbys + 1`` standby
nodes be registered in the system.

When the first standby node is added to the pg_auto_failover monitor, the
only acceptable value for ``number_sync_standbys`` is zero. When a second
standby is added that participates in the replication quorum, then
``number_sync_standbys`` is automatically set to one.

The command ``pg_autoctl set formation number-sync-standbys`` can be used to
change the value of this parameter in a formation, even when all the nodes
are already running in production. The pg_auto_failover monitor then sets a
transition for the primary to update its local value of
``synchronous_standby_names``.

Replication Quorum
^^^^^^^^^^^^^^^^^^

The replication quorum setting is a boolean and defaults to ``true``. It can
be set on a node, and pg_auto_failover only includes a given node in
``synchronous_standby_names`` when the replication quorum parameter has been
set to true. This means that asynchronous replication will be used for nodes
where ``replication-quorum`` is set to ``false``.

It is possible to force asynchronous replication globally by setting
replication quorum to false on all the standby nodes in a formation.

To set this parameter to either true or false, use one of the following
commands::

  pg_autoctl set node replication-quorum true
  pg_autoctl set node replication-quorum false

Candidate Priority
^^^^^^^^^^^^^^^^^^

The candidate priority setting is an integer that can be set to any value
between 0 (zero) and 100 (one hundred). The default value is 50. When the
pg_auto_failover monitor decides to orchestrate a failover, it uses each
node's candidate priority to pick the new primary node.

To set this parameter to the value ``<n>``, use the following command::

  pg_autoctl set node candidate-priority <n>

When nodes have the same candidate priority, the monitor then picks the
standby with the most advanced LSN position published to the monitor. When
more than one node has published the same LSN position, a random one is
chosen.

When the candidate for failover has not published the most advanced LSN
position in the WAL, the pg_auto_failover orchestrate an intermediate step
in the failover mechanism where the candidate fetches the missing WAL bytes
from one of the standby with the most advanced LSN position prior to being
promoted. Postgres allows this operation thanks to cascading replication:
any standby can be the upstream node for another standby.

When setting the candidate priority of a node down to zero, this node will
never be selected to be promoted as the new primary when a failover is
orchestrated by the monitor. The monitor will instead wait until another
node registered is healthy and in a position to be promoted.

It is required that at least two nodes have a non-zero candidate priority in
any pg_auto_failover formation, at all times. Otherwise no failover is
possible.

Architecture with three standby nodes
-------------------------------------

When setting the three parameters above, it's possible to design very
different Postgres architectures for your production needs.

.. figure:: ./tikz/arch-three-standby.svg
   :alt: pg_auto_failover architecture with three standby nodes

   pg_auto_failover architecture with three standby nodes

In this case, the system is setup with three standby nodes all set the same
way, with default parameters. This allows to then setup
``number_sync_standbys = 2``. This means that Postgres will maintain three
copies of the production data set at all time.

On the other hand, if two standby nodes were to fail at the same time,
despite the fact that two copies of the data are still maintained, the
Postgres service would be degraded to read-only.

Architecture with three standby nodes, one async
------------------------------------------------

.. figure:: ./tikz/arch-three-standby-one-async.svg
   :alt: pg_auto_failover architecture with three standby nodes, one async

   pg_auto_failover architecture with three standby nodes, one async

In this case, the system is setup with two standby nodes participating in
the replication quorum, allowing for ``number_sync_standbys = 1``. The
system always maintain a minimum of two copies of the data set, one on the
primary, another one on either node B or node D. Whenever we lose one of
those nodes, we can hold to this guarantee of two copies of the data set.

Adding to that, we have the standby server C which has been setup to not
participate in the replication quorum. Node C will not be found in the
``synchronous_standby_names`` list of nodes. Also, node C is setup in a way
to never be a candidate for failover, with ``candidate-priority = 0``.

This architecture would fit a situation with nodes A, B, and D are deployed
in the same data center or availability zone and node C in another one.
Those three nodes are setup to support the main production traffic and
implement high availability of both the Postgres service and the data set.

Node C might be setup for Business Continuity in case the first data center
is lost, or maybe for reporting needs deployed on another application
domain.
