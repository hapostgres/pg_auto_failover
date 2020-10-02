.. _tutorial:

pg_auto_failover Tutorial
=========================

In this guide we’ll create a primary and secondary Postgres node and set
up pg_auto_failover to replicate data between them. We’ll simulate failure in
the primary node and see how the system smoothly switches (fails over)
to the secondary.

For illustration, we'll run our databases on virtual machines in the Azure
platform, but the techniques here are relevant to any cloud provider or
on-premise network. We'll use four virtual machines: a primary database, a
secondary database, a monitor, and an "application." The monitor watches the
other nodes’ health, manages global state, and assigns nodes their roles.

Create virtual network
----------------------

Please refer to the detailed page :ref:`script_single` to prepare an Azure
setup where to run this tutorial. The detailed page contains a full length
script that you can review and copy/paste, and a simple command that will
automate the whole setup for you.

When the setup is done, the following machines are deployed and ready:

  - a VM running the pg_auto_failover monitor service, registered in
    systemd, initialised with the command ``pg_autoctl create monitor`` and
    started with the command ``pg_autoctl run``,

  - two VMs running the pg_auto_failover Postgres service, registered in
    systemd, initialised with the command ``pg_autoctl create postgres`` and
    started with the command ``pg_autoctl run``,

  - a VM available to represent your application, not running any particular
    service, from where we're going to create some SQL activity using the
    ``psql`` command from Postgres.

Node communication
------------------

For convenience, pg_autoctl modifies each node's ``pg_hba.conf`` file to allow
the nodes to connect to one another. For instance, pg_autoctl added the
following lines to node A:

.. code-block:: ini

   # automatically added to node A

   hostssl "appdb" "ha-admin" ha-demo-a.internal.cloudapp.net trust
   hostssl replication "pgautofailover_replicator" ha-demo-b.internal.cloudapp.net trust
   hostssl "appdb" "pgautofailover_replicator" ha-demo-b.internal.cloudapp.net trust

For ``pg_hba.conf`` on the monitor node pg_autoctl inspects the local network
and makes its best guess about the subnet to allow. In our case it guessed
correctly:

.. code-block:: ini

   # automatically added to the monitor

   hostssl "pg_auto_failover" "autoctl_node" 10.0.1.0/24 trust

If worker nodes have more ad-hoc addresses and are not in the same subnet, it's
better to disable pg_autoctl's automatic modification of pg_hba using the
``--skip-pg-hba`` command line option during creation. You will then need to
edit the hba file by hand. Another reason for manual edits would be to use
special authentication methods.

Watch the replication
---------------------

First let’s verify that the monitor knows about our nodes, and see what
states it has assigned them:

.. code-block:: bash

   ssh -l ha-admin `vm_ip monitor` pg_autoctl show state --pgdata monitor

     Name |  Node |                            Host:Port |       LSN | Reachable |       Current State |      Assigned State
   -------+-------+--------------------------------------+-----------+-----------+---------------------+--------------------
   node_1 |     1 | ha-demo-a.internal.cloudapp.net:5432 | 0/3000060 |       yes |             primary |             primary
   node_2 |     2 | ha-demo-b.internal.cloudapp.net:5432 | 0/3000060 |       yes |           secondary |           secondary


This looks good. We can add data to the primary, and later see it appear in the
secondary. We'll connect to the database from inside our "app" virtual machine,
using a connection string obtained from the monitor.

.. code-block:: bash

   ssh -l ha-admin `vm_ip monitor` pg_autoctl show uri --pgdata monitor

         Type |    Name | Connection String
   -----------+---------+-------------------------------
      monitor | monitor | postgres://autoctl_node@ha-demo-monitor.internal.cloudapp.net:5432/pg_auto_failover?sslmode=require
    formation | default | postgres://ha-demo-b.internal.cloudapp.net:5432,ha-demo-a.internal.cloudapp.net:5432/appdb?target_session_attrs=read-write&sslmode=require

Now we'll get the connection string and store it in a local environment
variable:

.. code-block:: bash

   APP_DB_URI=$( \
     ssh -l ha-admin `vm_ip monitor` \
       pg_autoctl show uri --formation default --pgdata monitor \
   )

The connection string contains both our nodes, comma separated, and includes
the url parameter ``?target_session_attrs=read-write`` telling psql that we
want to connect to whichever of these servers supports reads *and* writes.
That will be the primary server.

.. code-block:: bash

   # connect to database via psql on the app vm and
   # create a table with a million rows
   ssh -l ha-admin -t `vm_ip app` -- \
     psql "'$APP_DB_URI'" \
       -c "'CREATE TABLE foo AS SELECT generate_series(1,1000000) bar;'"

Cause a failover
----------------

Now that we've added data to node A, let's switch which is considered
the primary and which the secondary. After the switch we'll connect again
and query the data, this time from node B.

.. code-block:: bash

   # initiate failover to node B
   ssh -l ha-admin -t `vm_ip monitor` \
     pg_autoctl perform switchover --pgdata monitor

Once node B is marked "primary" (or "wait_primary") we can connect and verify
that the data is still present:

.. code-block:: bash

   # connect to database via psql on the app vm
   ssh -l ha-admin -t `vm_ip app` -- \
     psql "'$APP_DB_URI'" \
       -c "'SELECT count(*) FROM foo;'"

It shows

.. code-block:: bash

    count
  ---------
   1000000

Cause a node failure
--------------------

This plot is too boring, time to introduce a problem. We’ll turn off VM for
node B (currently the primary after our previous failover) and watch node A
get promoted.

In one terminal let’s keep an eye on events:

.. code-block:: bash

   ssh -t -l ha-admin `vm_ip monitor` -- \
     watch -n 1 -d pg_autoctl show state --pgdata monitor

In another terminal we’ll turn off the virtual server.

.. code-block:: bash

   az vm stop \
     --resource-group ha-demo \
     --name ha-demo-b

After a number of failed attempts to talk to node B, the monitor determines
the node is unhealthy and puts it into the "demoted" state.  The monitor
promotes node A to be the new primary.

.. code-block:: bash

     Name |  Node |                            Host:Port |       LSN | Reachable |       Current State |      Assigned State
   -------+-------+--------------------------------------+-----------+-----------+---------------------+--------------------
   node_1 |     1 | ha-demo-a.internal.cloudapp.net:5432 | 0/6D4E068 |       yes |        wait_primary |        wait_primary
   node_2 |     2 | ha-demo-b.internal.cloudapp.net:5432 | 0/6D4E000 |       yes |             demoted |          catchingup

Node A cannot be considered in full "primary" state since there is no secondary
present, but it can still serve client requests. It is marked as "wait_primary"
until a secondary appears, to indicate that it's running without a backup.

Let's add some data while B is offline.

.. code-block:: bash

   # notice how $APP_DB_URI continues to work no matter which node
   # is serving as primary
   ssh -l ha-admin -t `vm_ip app` -- \
     psql "'$APP_DB_URI'" \
       -c "'INSERT INTO foo SELECT generate_series(1000001, 2000000);'"

Resurrect node B
----------------

Run this command to bring node B back online:

.. code-block:: bash

   az vm start \
     --resource-group ha-demo \
     --name ha-demo-b

Now the next time the keeper retries its health check, it brings the node back.
Node B goes through the state "catchingup" while it updates its data to match
A. Once that's done, B becomes a secondary, and A is now a full primary again.

.. code-block:: bash

     Name |  Node |                            Host:Port |        LSN | Reachable |       Current State |      Assigned State
   -------+-------+--------------------------------------+------------+-----------+---------------------+--------------------
   node_1 |     1 | ha-demo-a.internal.cloudapp.net:5432 | 0/12000738 |       yes |             primary |             primary
   node_2 |     2 | ha-demo-b.internal.cloudapp.net:5432 | 0/12000738 |       yes |           secondary |           secondary

What's more, if we connect directly to the database again, all two million rows
are still present.

.. code-block:: bash

   ssh -l ha-admin -t `vm_ip app` -- \
     psql "'$APP_DB_URI'" \
       -c "'SELECT count(*) FROM foo;'"

It shows

.. code-block:: bash

    count
  ---------
   2000000
