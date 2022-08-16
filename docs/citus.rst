.. _citus:

Citus Support
=============

The usual ``pg_autoctl`` commands work both with Postgres standalone nodes
and with Citus nodes.

.. figure:: ./tikz/arch-citus.svg
   :alt: pg_auto_failover architecture with a Citus formation

   pg_auto_failover architecture with a Citus formation

When using pg_auto_failover with Citus, a pg_auto_failover *formation* is
composed of a coordinator and a set of worker nodes.

When High-Availability is enabled at the formation level, which is the
default, then a minimum of two coordinator nodes are required: a primary and
a secondary coordinator to be able to orchestrate a failover when needed.

The same applies to the worker nodes: when using pg_auto_failover for Citus
HA, then each worker node is a pg_auto_failover group in the formation, and
each worker group is setup with at least two nodes (primary, secondary).

Setting-up your first Citus formation
-------------------------------------

Have a look at our documentation of :ref:`citus_quickstart` for more details
with a full tutorial setup on a single VM, for testing and QA.

Citus specific commands and operations
--------------------------------------

When setting up Citus with pg_auto_failover, the following Citus specific
commands are provided. Other ``pg_autoctl`` commands work as usual when
deploying a Citus formation, so that you can use the rest of this
documentation to operate your Citus deployments.

pg_autoctl create coordinator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This creates a Citus coordinator, that is to say a Postgres node with the
Citus extension loaded and ready to act as a coordinator. The coordinator is
always places in the pg_auto_failover group zero of a given formation.

See :ref:`pg_autoctl_create_coordinator` for details.

.. important::

   The default ``--dbname`` is the same as the current system user name,
   which in many case is going to be ``postgres``. Please make sure to use
   the ``--dbname`` option with the actual database that you're going to use
   with your application.

   Citus does not support multiple databases, you have to use the database
   where Citus is created. When using Citus, that is essential to the well
   behaving of worker failover.


pg_autoctl create worker
^^^^^^^^^^^^^^^^^^^^^^^^

This command creates a new Citus worker node, that is to say a Postgres node
with the Citus extensions loaded, and registered to the Citus coordinator
created with the previous command. Because the Citus coordinator is always
given group zero, the pg_auto_failover monitor knows how to reach the Citus
coordinator and automate workers registration.

The default worker creation policy is to assign the primary role to the
first worker registered, then secondary in the same group, then primary in a
new group, etc.

If you want to extend an existing group to have a third worker node in the
same group, enabling multiple-standby capabilities in your setup, then make
sure to use the ``--group`` option to the ``pg_autoctl create worker``
command.

See :ref:`pg_autoctl_create_worker` for details.

pg_autoctl activate
^^^^^^^^^^^^^^^^^^^

This command calls the Citus “activation” API so that a node can be used to
host shards for your reference and distributed tables.

When creating a Citus worker, ``pg_autoctl create worker`` automatically
activates the worker node to the coordinator. You only need this command
when something unexpected have happened and you want to manually make sure
the worker node has been activated at the Citus coordinator level.

Starting with Citus 10 it is also possible to activate the coordinator
itself as a node with shard placement. Use ``pg_autoctl activate`` on your
Citus coordinator node manually to use that feature.

When the Citus coordinator is activated, an extra step is then needed for it
to host shards of distributed tables. If you want your coordinator to have
shards, then have a look at the Citus API citus_set_node_property_ to set
the ``shouldhaveshards`` property to ``true``.

.. _citus_set_node_property:
  http://docs.citusdata.com/en/v10.0/develop/api_udf.html#citus-set-node-property

See :ref:`pg_autoctl_activate` for details.

Citus worker failover
---------------------

When a failover is orchestrated by pg_auto_failover for a Citus worker node,
Citus offers the opportunity to make the failover close to transparent to
the application. Because the application connects to the coordinator, which
in turn connects to the worker nodes, then it is possible with Citus to
_pause_ the SQL write traffic on the coordinator for the shards hosted on a
failed worker node. The Postgres failover then happens while the traffic is
kept on the coordinator, and resumes as soon as a secondary worker node is
ready to accept read-write queries.

This is implemented thanks to Citus smart locking strategy in its
``citus_update_node`` API, and pg_auto_failover takes full benefit with a
special built set of FSM transitions for Citus workers.

.. _citus_secondaries:

Citus Secondaries and read-replica
----------------------------------

Starting with pg_auto_failover 1.5, it is possible to setup Citus read-only
replica. This Citus feature allows using a set of dedicated nodes (both
coordinator and workers) to serve read-only traffic, such as reporting,
analytics, or other parts of your workload that are read-only.

Citus read-replica nodes are a solution for load-balancing. Those nodes
can't be used as HA failover targets, and thus have their
``candidate-priority`` set to zero. This setting of a read-replica can not
be changed later.

This setup is done by setting the Citus property
``citus.use_secondary_nodes`` to ``always`` (it defaults to ``never``), and
the Citus property ``citus.cluster_name`` to your read-only cluster name.

Both of those settings are entirely supported and managed by ``pg_autoctl``
when using the ``--citus-secondary --cluster-name cluster_d`` options to the
``pg_autoctl create coordinator|worker`` commands.

Here is an example where we have created a formation with three nodes for HA
for the coordinator (one primary and two secondary nodes), then a single
worker node with the same three nodes setup for HA, and then one
read-replica environment on-top of that, for a total of 8 nodes::

    $  pg_autoctl show state
        Name |  Node |      Host:Port |       LSN | Reachable |       Current State |      Assigned State
    ---------+-------+----------------+-----------+-----------+---------------------+--------------------
     coord0a |   0/1 | localhost:5501 | 0/5003298 |       yes |             primary |             primary
     coord0b |   0/3 | localhost:5502 | 0/5003298 |       yes |           secondary |           secondary
     coord0c |   0/6 | localhost:5503 | 0/5003298 |       yes |           secondary |           secondary
     coord0d |   0/7 | localhost:5504 | 0/5003298 |       yes |           secondary |           secondary
    worker1a |   1/2 | localhost:5505 | 0/4000170 |       yes |             primary |             primary
    worker1b |   1/4 | localhost:5506 | 0/4000170 |       yes |           secondary |           secondary
    worker1c |   1/5 | localhost:5507 | 0/4000170 |       yes |           secondary |           secondary
    reader1d |   1/8 | localhost:5508 | 0/4000170 |       yes |           secondary |           secondary


Nodes named ``coord0d`` and ``reader1d`` are members of the read-replica
cluster ``cluster_d``. We can see that a read-replica cluster needs a
dedicated coordinator and then one dedicated worker node per group.

.. tip::

   It is possible to name the nodes in a pg_auto_failover formation either
   at creation time or later, using one of those commands::

	 $ pg_autoctl create worker --name ...
	 $ pg_autoctl set node metadata --name ...

   Here ``coord0d`` is the node name for the dedicated coordinator for
   ``cluster_d``, and ``reader1d`` is the node name for the dedicated worker
   for ``cluster_d`` in the worker group 1 (the only worker group in that
   setup).

Now the usual command to show the connection strings for your application is
listing the read-replica setup that way::

	$ pg_autoctl show uri
            Type |      Name | Connection String
    -------------+-----------+-------------------------------
         monitor |   monitor | postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer
       formation |   default | postgres://localhost:5503,localhost:5501,localhost:5502/postgres?target_session_attrs=read-write&sslmode=prefer
    read-replica | cluster_d | postgres://localhost:5504/postgres?sslmode=prefer

Given that setup, your application can now use the formation default
Postgres URI to connect to the highly-available read-write service, or to
the read-replica ``cluster_d`` service to connect to the read-only replica
where you can offload some of your SQL workload.

When connecting to the ``cluster_d`` connection string, the Citus properties
``citus.use_secondary_nodes`` and ``citus.cluster_name`` are automatically
setup to their expected values, of course.
