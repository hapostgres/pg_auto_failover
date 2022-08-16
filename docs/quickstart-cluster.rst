.. _citus_quickstart:

Citus Cluster Quick Start
=========================

In this guide we’ll create a Citus cluster with a coordinator node and two workers. Every node will have a secondary for failover. We’ll simulate failure in the coordinator and worker nodes and see how the system continues to function.

For simplicity we’ll run all the nodes on a single machine, but in production the primary and secondary servers for each PostgreSQL node would run on independent machines.

Before attempting this quickstart, you should try the :ref:`tutorial` for plain PostgreSQL nodes. The current guide relies on concepts explained there.

Install pg_auto_failover and run a Monitor
------------------------------------------

pg_auto_failover requires a monitor node to keep track of global state and orchestrate actions on the other nodes. Follow the first two steps from the previous quickstart to get started:

* :ref:`install`
* :ref:`tutorial_run_monitor`

.. note::

  If you created a monitor *and registered nodes* in it as part of the previous quickstart guide, do not reuse the monitor. Create a new one with no nodes so that the cluster can be designated as type "citus" rather than type "postgres."

Create a Citus Cluster
----------------------

pg_auto_failover can not only create nodes (as demonstrated in the previous quickstart), it can also configure them with the Citus extension. In this guide we'll use pg_auto_failover to create a coordinator node and two workers.

First, install the Citus extension onto the system so pg_auto_failover can enable it on nodes:

.. code-block:: bash

  sudo yum install -y citus-rebalancer81_11

Start a coordinator and its secondary. These commands will set up Citus on the node, and allow connections from the workers-to-be on the local network.

.. code-block:: bash

  pg_autoctl create coordinator  \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./coord_a        \
    --pgport 6010             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./coord_a > coord_a.log 2>&1 &

  pg_autoctl create coordinator  \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./coord_b        \
    --pgport 6011             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./coord_b > coord_b.log 2>&1 &

Start a worker and its secondary. This configures Citus on the node, and also registers the node with the coordinator.

.. code-block:: bash

  pg_autoctl create worker       \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./w1_a           \
    --pgport 6020             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./w1_a > w1_a.log 2>&1 &

  pg_autoctl create worker       \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./w1_b           \
    --pgport 6021             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./w1_b > w1_b.log 2>&1 &

Start another worker and its secondary.

.. code-block:: bash

  pg_autoctl create worker       \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./w2_a           \
    --pgport 6030             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./w2_a > w2_a.log 2>&1 &

  pg_autoctl create worker       \
    --auth trust              \
    --ssl-self-signed         \
    --pgdata ./w2_b           \
    --pgport 6031             \
    --nodename 127.0.0.1      \
    --pgctl /usr/pgsql-11/bin/pg_ctl \
    --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover?sslmode=require

  pg_autoctl run --pgdata ./w2_b > w2_b.log 2>&1 &

At this point we should see three groups, each with a primary and secondary node.

.. code-block:: bash

  pg_autoctl show state --pgdata ./monitor
       Name |   Port | Group |  Node |     Current State |    Assigned State
  ----------+--------+-------+-------+-------------------+------------------
  127.0.0.1 |   6010 |     0 |     1 |           primary |           primary
  127.0.0.1 |   6011 |     0 |     2 |         secondary |         secondary
  127.0.0.1 |   6020 |     1 |     3 |           primary |           primary
  127.0.0.1 |   6021 |     1 |     4 |         secondary |         secondary
  127.0.0.1 |   6030 |     2 |     5 |           primary |           primary
  127.0.0.1 |   6031 |     2 |     6 |         secondary |         secondary

You can see from the above that the coordinator node has a primary and secondary instance for high availability. When connecting to the coordinator, clients should try connecting to whichever instance is running and supports reads and writes. They can do this with a special connection string like this:

.. code-block:: bash

  # The "pg_autoctl show uri" produces a URI which will allow clients to
  # connect to the primary or secondary coordinator as needed

  export COORD=`pg_autoctl show uri --pgdata coord_a --formation default`

  # sets COORD to:
  # postgres://127.0.0.1:6010,127.0.0.1:6011/postgres?target_session_attrs=read-write

Using that connection string, we can check that the worker nodes have been registered:

.. code-block:: bash

  psql $COORD -c 'select * from master_get_active_worker_nodes();'

   node_name | node_port
  -----------+-----------
   127.0.0.1 |      6020
   127.0.0.1 |      6030

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
