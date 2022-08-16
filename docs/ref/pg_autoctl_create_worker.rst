.. _pg_autoctl_create_worker:

pg_autoctl create worker
=============================

pg_autoctl create worker - Initialize a pg_auto_failover worker node

Synopsis
--------

The command ``pg_autoctl create worker`` initializes a pg_auto_failover
Worker node for a Citus formation. The worker is special in a
Citus formation: that's where the client application connects to either to
manage the formation and the sharding of the tables, or for its normal SQL
traffic.

The worker also has to register every worker in the formation.

::

   usage: pg_autoctl create worker

     --pgctl           path to pg_ctl
     --pgdata          path to data director
     --pghost          PostgreSQL's hostname
     --pgport          PostgreSQL's port number
     --hostname        hostname by which postgres is reachable
     --listen          PostgreSQL's listen_addresses
     --proxyport       Proxy's port number
     --username        PostgreSQL's username
     --dbname          PostgreSQL's database name
     --name            pg_auto_failover node name
     --formation       pg_auto_failover formation
     --group           pg_auto_failover group Id
     --monitor         pg_auto_failover Monitor Postgres URL
     --auth            authentication method for connections from monitor
     --skip-pg-hba     skip editing pg_hba.conf rules
     --citus-secondary when used, this worker node is a citus secondary
     --citus-cluster   name of the Citus Cluster for read-replicas
     --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)
     --ssl-mode        use that sslmode in connection strings
     --ssl-ca-file     set the Postgres ssl_ca_file to that file path
     --ssl-crl-file    set the Postgres ssl_crl_file to that file path
     --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)
     --server-key      set the Postgres ssl_key_file to that file path
     --server-cert     set the Postgres ssl_cert_file to that file path

Description
-----------

This commands works the same as the :ref:`pg_autoctl_create_postgres`
command and implements the following extra steps:

  1. adds ``shared_preload_libraries = citus`` to the local PostgreSQL
     instance configuration.

  2. creates the extension ``citus`` in the target database.

  3. gets the coordinator node hostname from the pg_auto_failover monitor.

     This operation is retried when it fails, as the coordinator might
     appear later than some of the workers when the whole formation is
     initialized at once, in parallel, on multiple nodes.

  4. adds node to the coordinator

     This is done in two steps. First, we call the SQL function
     ``master_add_inactive_node`` on the coordinator, and second, we call
     the SQL function ``master_activate_node``.

     This way allows for easier diagnostics in case things go wrong. In the
     first step, the network and authentication setup needs to allow for
     nodes to connect to each other. In the second step, the Citus
     `reference tables` are distributed to the new node, and this operation
     has its own set of failure cases to handle.

.. important::

   The default ``--dbname`` is the same as the current system user name,
   which in many case is going to be ``postgres``. Please make sure to use
   the ``--dbname`` option with the actual database that you're going to use
   with your application.

   Citus does not support multiple databases, you have to use the database
   where Citus is created. When using Citus, that is essential to the well
   behaving of worker failover.

Options
-------

See the manual page for :ref:`pg_autoctl_create_postgres` for the common
options. This section now lists the options that are specific to
``pg_autoctl create worker``:

--proxyport

  The ``--proxyport`` option allows pg_auto_failover to register the proxy
  port in the ``pg_dist_poolinfo`` entry for the worker node in its
  Coordinator, rather than the ``--pgport`` entry as would usually be done.

--citus-secondary

  Use this option to create a worker dedicated to a Citus Secondary
  cluster.

  See :ref:`citus_secondaries` for more information.

--citus-cluster

  Use this option to name the Citus Secondary cluster that this worker
  node belongs to. Use the same cluster name again for the worker nodes that
  are part of this cluster.

  See :ref:`citus_secondaries` for more information.
