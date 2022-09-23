.. _citus_quickstart:

Citus Cluster Quick Start
=========================

In this guide we’ll create a Citus cluster with a coordinator node and three
workers. Every node will have a secondary for failover. We’ll simulate
failure in the coordinator and worker nodes and see how the system continues
to function.

This tutorial uses `docker-compose`__ in order to separate the architecture
design from some of the implementation details. This allows reasonning at
the architecture level within this tutorial, and better see which software
component needs to be deployed and run on which node.

__ https://docs.docker.com/compose/

Pre-requisites
--------------

When using `docker-compose` we describe a list of services, each service may
run on one or more nodes, and each service just runs a single isolated
process in a container.

Within the context of a tutorial, or even a development environment, this
matches very well to provisioning separate physical machines on-prem, or
Virtual Machines either on-prem on in a Cloud service.

The docker image used in this tutorial is named `pg_auto_failover:cluster`.
It can be built locally when using the attached `Dockerfile`__ found within
the GitHub repository for pg_auto_failover.

__ https://github.com/citusdata/pg_auto_failover/blob/master/docs/cluster/Dockerfile

To build the image, either use the provided Makefile and run ``make build``,
or run the docker build command directly:

::

   $ git clone https://github.com/citusdata/pg_auto_failover
   $ cd pg_auto_failover/docs/cluster
   $ docker build -t $(CONTAINER_NAME) -f Dockerfile ../..

Create a Citus Cluster
----------------------

To create a cluster we use the following docker-compose definition:

.. literalinclude:: cluster/docker-compose.yml
   :language: yaml
   :emphasize-lines: 5,15,27
   :linenos:

To run the full Citus cluster with HA from this definition, we can use the
following command:

::

   $ docker-compose up --scale coord=2 --scale worker=6

The command above starts the services up. The command also specifies a
``--scale`` option that is different for each service. We need:

 - one monitor node, and the default scale for a service is 1,

 - one primary Citus coordinator node and one secondary Cituscoordinator
   node, which is to say two coordinator nodes,

 - and three Citus worker nodes, each worker with both a primary Postgres
   node and a secondary Postgres node, so that's a scale of 6 here.

The default policy for the pg_auto_failover monitor is to assign a primary
and a secondary per auto failover :ref:`group`. In our case, every node
being provisioned with the same command, we benefit from that default policy::

  $ pg_autoctl create worker --ssl-self-signed --auth trust --pg-hba-lan --run

When provisioning a production cluster, it is often required to have a
better control over which node participates in which group, then using the
``--group N`` option in the ``pg_autoctl create worker`` command line.

Within a given group, the first node that registers is a primary, and the
other nodes are secondary nodes. The monitor takes care of that in a way
that we don't have to. In a High Availability setup, every node should be
ready to be promoted primary at any time, so knowing which node in a group
is assigned primary first is not very interesting.

While the cluster is being provisionned by docker-compose, you can run the
following command and have a dynamic dashboard to follow what's happening.
The following command is like ``top`` for pg_auto_failover::

  $ docker-compose exec monitor pg_autoctl watch

Because the ``pg_basebackup`` operation that is used to create the secondary
nodes takes some time when using Citus, because of the first CHECKPOINT
which is quite slow. So at first when inquiring about the cluster state you
might see the following output:

.. code-block:: bash

   $ docker-compose exec monitor pg_autoctl show state
       Name |  Node |         Host:Port |       TLI: LSN |   Connection |      Reported State |      Assigned State
   ---------+-------+-------------------+----------------+--------------+---------------------+--------------------
    coord0a |   0/1 | cd52db444544:5432 |   1: 0/200C4A0 |   read-write |        wait_primary |        wait_primary
    coord0b |   0/2 | 66a31034f2e4:5432 |         1: 0/0 |       none ! |        wait_standby |          catchingup
   worker1a |   1/3 | dae7c062e2c1:5432 |   1: 0/2003B18 |   read-write |        wait_primary |        wait_primary
   worker1b |   1/4 | 397e6069b09b:5432 |         1: 0/0 |       none ! |        wait_standby |          catchingup
   worker2a |   2/5 | 5bf86f9ef784:5432 |   1: 0/2006AB0 |   read-write |        wait_primary |        wait_primary
   worker2b |   2/6 | 23498b801a61:5432 |         1: 0/0 |       none ! |        wait_standby |          catchingup
   worker3a |   3/7 | c23610380024:5432 |   1: 0/2003B18 |   read-write |        wait_primary |        wait_primary
   worker3b |   3/8 | 2ea8aac8a04a:5432 |         1: 0/0 |       none ! |        wait_standby |          catchingup

After a while though (typically around a minute or less), you can run that
same command again for stable result:

.. code-block:: bash

   $ docker-compose exec monitor pg_autoctl show state

       Name |  Node |         Host:Port |       TLI: LSN |   Connection |      Reported State |      Assigned State
   ---------+-------+-------------------+----------------+--------------+---------------------+--------------------
    coord0a |   0/1 | cd52db444544:5432 |   1: 0/3127AD0 |   read-write |             primary |             primary
    coord0b |   0/2 | 66a31034f2e4:5432 |   1: 0/3127AD0 |    read-only |           secondary |           secondary
   worker1a |   1/3 | dae7c062e2c1:5432 |   1: 0/311B610 |   read-write |             primary |             primary
   worker1b |   1/4 | 397e6069b09b:5432 |   1: 0/311B610 |    read-only |           secondary |           secondary
   worker2a |   2/5 | 5bf86f9ef784:5432 |   1: 0/311B610 |   read-write |             primary |             primary
   worker2b |   2/6 | 23498b801a61:5432 |   1: 0/311B610 |    read-only |           secondary |           secondary
   worker3a |   3/7 | c23610380024:5432 |   1: 0/311B648 |   read-write |             primary |             primary
   worker3b |   3/8 | 2ea8aac8a04a:5432 |   1: 0/311B648 |    read-only |           secondary |           secondary

You can see from the above that the coordinator node has a primary and
secondary instance for high availability. When connecting to the
coordinator, clients should try connecting to whichever instance is running
and supports reads and writes.

We can review the available Postgres URIs with the
:ref:`pg_autoctl_show_uri` command::

  $ docker-compose exec monitor pg_autoctl show uri
           Type |    Name | Connection String
   -------------+---------+-------------------------------
        monitor | monitor | postgres://autoctl_node@552dd89d5d63:5432/pg_auto_failover?sslmode=require
      formation | default | postgres://66a31034f2e4:5432,cd52db444544:5432/citus?target_session_attrs=read-write&sslmode=require

To check that Citus worker nodes have been registered to the coordinator, we
can run a psql session right from the coordinator container:

.. code-block:: bash

   $ docker-compose exec coord psql -d citus -c 'select * from citus_get_active_worker_nodes();'
     node_name   | node_port
   --------------+-----------
    dae7c062e2c1 |      5432
    5bf86f9ef784 |      5432
    c23610380024 |      5432
   (3 rows)

We are now reaching the limits of using a simplified docker-compose setup.
When using the ``--scale`` option, it is not possible to give a specific
hostname to each running node, and then we get a randomly generated string
instead or useful node names such as ``worker1a`` or ``worker3b``.

Create a Citus Cluster, take two
--------------------------------

This time we create a cluster using the following docker-compose definition:

.. literalinclude:: cluster/docker-compose-anchors.yml
   :language: yaml
   :emphasize-lines: 3,15,40,44,48,52,56,60,64,68
   :linenos:

This definition is a little more involved than the previous one. We take
benefit from `YAML anchors and aliases`__ to define a *template* for our
coordinator nodes and worker nodes, and then apply that template to the
actual nodes.

__ https://yaml101.com/anchors-and-aliases/

We start this cluster as in the previous section:

::

   $ docker-compose up --scale coord=2 --scale worker=6

And this time we get the following cluster as a result:

::

   $ docker-compose exec monitor pg_autoctl show state

       Name |  Node |     Host:Port |       TLI: LSN |   Connection |      Reported State |      Assigned State
   ---------+-------+---------------+----------------+--------------+---------------------+--------------------
    coord0a |   0/4 |  coord0b:5432 |   1: 0/3000110 |   read-write |             primary |             primary
    coord0b |   0/7 |  coord0a:5432 |   1: 0/3000110 |    read-only |           secondary |           secondary
   worker1a |   1/1 | worker1b:5432 |   1: 0/3000110 |   read-write |             primary |             primary
   worker1b |   1/8 | worker1a:5432 |   1: 0/3000110 |    read-only |           secondary |           secondary
   worker2a |   2/2 | worker2b:5432 |   1: 0/3000110 |   read-write |             primary |             primary
   worker2b |   2/5 | worker2a:5432 |   1: 0/3000110 |    read-only |           secondary |           secondary
   worker3a |   3/3 | worker3a:5432 |   1: 0/3000110 |   read-write |             primary |             primary
   worker3b |   3/6 | worker3b:5432 |   1: 0/3000110 |    read-only |           secondary |           secondary


And then we have the following application connection string to use:

::

   $ docker-compose exec monitor pg_autoctl show uri
           Type |    Name | Connection String
   -------------+---------+-------------------------------
        monitor | monitor | postgres://autoctl_node@f0135b83edcd:5432/pg_auto_failover?sslmode=require
      formation | default | postgres://coord0b:5432,coord0a:5432/citus?target_session_attrs=read-write&sslmode=require

And finally, the nodes being registered as Citus worker nodes also make more
sense:

::

   $ docker-compose exec coord0a psql -d citus -c 'select * from citus_get_active_worker_nodes()'
    node_name | node_port
   -----------+-----------
    worker3a  |      5432
    worker1b  |      5432
    worker2b  |      5432
   (3 rows)


.. important::

   At this point, it is important to note that the Citus coordinator only
   knows about the primary nodes in each group. The High Availability
   mechanisms are all implemented in pg_auto_failover, which mostly uses the
   Citus API ``citus_update_node`` during worker node failovers.

Our first Citus worker failover
-------------------------------

We see that in the ``citus_get_active_worker_nodes()`` output we have
``worker3a``, ``worker1b``, and ``worker2b``. As mentionned before, that
should have no impact on the operations of the Citus cluster when nodes are
all dimensionned the same.

That said, some readers among you will prefer to have the *A* nodes as
primaries to get started with. So let's implement our first worker failover
then. With pg_auto_failover, this is as easy as doing:

::

   $ docker-compose exec monitor pg_autoctl perform failover --group 2
   15:40:03 9246 INFO  Waiting 60 secs for a notification with state "primary" in formation "default" and group 2
   15:40:03 9246 INFO  Listening monitor notifications about state changes in formation "default" and group 2
   15:40:03 9246 INFO  Following table displays times when notifications are received
       Time |     Name |  Node |     Host:Port |       Current State |      Assigned State
   ---------+----------+-------+---------------+---------------------+--------------------
   15:40:03 | worker2a |   2/2 | worker2b:5432 |             primary |            draining
   15:40:03 | worker2b |   2/5 | worker2a:5432 |           secondary |   prepare_promotion
   15:40:03 | worker2a |   2/2 | worker2b:5432 |            draining |            draining
   15:40:03 | worker2b |   2/5 | worker2a:5432 |   prepare_promotion |   prepare_promotion
   15:40:03 | worker2b |   2/5 | worker2a:5432 |   prepare_promotion |        wait_primary
   15:40:03 | worker2a |   2/2 | worker2b:5432 |            draining |             demoted
   15:40:03 | worker2a |   2/2 | worker2b:5432 |             demoted |             demoted
   15:40:04 | worker2b |   2/5 | worker2a:5432 |        wait_primary |        wait_primary
   15:40:04 | worker2a |   2/2 | worker2b:5432 |             demoted |          catchingup
   15:40:07 | worker2a |   2/2 | worker2b:5432 |          catchingup |          catchingup
   15:40:07 | worker2a |   2/2 | worker2b:5432 |          catchingup |           secondary
   15:40:07 | worker2a |   2/2 | worker2b:5432 |           secondary |           secondary
   15:40:07 | worker2b |   2/5 | worker2a:5432 |        wait_primary |             primary
   15:40:07 | worker2b |   2/5 | worker2a:5432 |             primary |             primary

So it took around 5 seconds to do a full worker failover in worker group 2.
Now we'll do the same on the group 1 to fix the other situation, and review
the resulting cluster state.

::

   $ docker-compose exec monitor pg_autoctl perform failover --group 2
   ...
   $ docker-compose exec monitor pg_autoctl show state
          Name |  Node |     Host:Port |       TLI: LSN |   Connection |      Reported State |      Assigned State
   ---------+-------+---------------+----------------+--------------+---------------------+--------------------
    coord0a |   0/4 |  coord0b:5432 |   1: 0/312C6F8 |   read-write |             primary |             primary
    coord0b |   0/7 |  coord0a:5432 |   1: 0/312C6F8 |    read-only |           secondary |           secondary
   worker1a |   1/1 | worker1b:5432 |   2: 0/50000D8 |    read-only |           secondary |           secondary
   worker1b |   1/8 | worker1a:5432 |   2: 0/50000D8 |   read-write |             primary |             primary
   worker2a |   2/2 | worker2b:5432 |   2: 0/5032838 |    read-only |           secondary |           secondary
   worker2b |   2/5 | worker2a:5432 |   2: 0/5032838 |   read-write |             primary |             primary
   worker3a |   3/3 | worker3a:5432 |   1: 0/311C838 |   read-write |             primary |             primary
   worker3b |   3/6 | worker3b:5432 |   1: 0/311C838 |    read-only |           secondary |           secondary

Which seen from the Citus coordinator, looks like the following:

::

   $ docker-compose exec coord0a psql -d citus -c 'select * from citus_get_active_worker_nodes()'
    node_name | node_port
   -----------+-----------
    worker1a  |      5432
    worker3a  |      5432
    worker2a  |      5432
   (3 rows)

Distribute Data to Workers
--------------------------

Let's create a database schema with a single distributed table.

.. code-block:: bash

  psql $COORD

.. code-block:: psql

  -- in psql

  CREATE TABLE companies (
    id bigserial PRIMARY KEY,
    name text NOT NULL,
    image_url text,
    created_at timestamp without time zone NOT NULL,
    updated_at timestamp without time zone NOT NULL
  );

  SELECT create_distributed_table('companies', 'id');

Next download and ingest some sample data.

.. code-block:: bash

  curl -O https://examples.citusdata.com/mt_ref_arch/companies.csv
  psql $COORD -c "\copy companies from 'companies.csv' with csv"
  # ( COPY 75 )

Handle Worker Failure
---------------------

Now we'll intentionally crash a worker's primary node and observe how the pg_auto_failover monitor unregisters that node in the coordinator and registers the secondary instead.

.. code-block:: bash

  # the pg_auto_failover keeper process will be unable to resurrect
  # the worker node if pg_control has been removed
  rm w1_a/global/pg_control

  # shut it down
  pg_ctl stop -D w1_a

The keeper will attempt to start worker 1a three times and then report the failure to the monitor, who promotes worker 1b to replace 1a. Worker 1a is unregistered with the coordinator node, and 1b is registered in its stead.

Asking the coordinator for active worker nodes now shows 1b and 2a (ports 6021 and 6030).

.. code-block:: bash

  psql $COORD -c 'select * from master_get_active_worker_nodes();'

   node_name | node_port
  -----------+-----------
   127.0.0.1 |      6021
   127.0.0.1 |      6030

Finally, verify that all rows of data are still present:

.. code-block:: bash

  psql $COORD -c 'select count(*) from companies;'

   count
  -------
      75

Meanwhile, the keeper on worker 1a heals the node. It runs pg_basebackup on restore from worker 1b. This restores, among other things, a new copy of the file we removed. After streaming replication completes, worker 1b becomes a full-fledged primary and 1a its secondary.

Handle Coordinator Failure
--------------------------

Because our ``$COORD`` connection string includes both ``127.0.0.1:6010`` and ``127.0.0.1:6011`` with the provision that ``target_session_attrs=read-write``, the database client will connect to whichever of these servers supports both reads and writes. Secondary nodes do not support writes, so the client will connect to the primary. This is currently the node listening on port 6010:

.. code-block:: text

  psql $COORD -c \
    "SELECT setting FROM pg_settings WHERE name = 'port';"

   setting
  ---------
   6010

However if we use the same trick with the pg_control file to crash our primary coordinator, we can watch how the monitor promotes the secondary.

.. code-block:: bash

  rm coord_a/global/pg_control
  pg_ctl stop -D coord_a

  # then observe the switch

  psql $COORD -c \
    "SELECT setting FROM pg_settings WHERE name = 'port';"

   setting
  ---------
   6011

After some time, coordinator A's keeper heals it, and the cluster converges in this state:

.. code-block:: bash

  pg_autoctl show state --pgdata ./monitor
       Name |   Port | Group |  Node |     Current State |    Assigned State
  ----------+--------+-------+-------+-------------------+------------------
  127.0.0.1 |   6010 |     0 |     1 |         secondary |         secondary
  127.0.0.1 |   6011 |     0 |     2 |           primary |           primary
  127.0.0.1 |   6020 |     1 |     3 |         secondary |         secondary
  127.0.0.1 |   6021 |     1 |     4 |           primary |           primary
  127.0.0.1 |   6030 |     2 |     5 |           primary |           primary
  127.0.0.1 |   6031 |     2 |     6 |         secondary |         secondary
