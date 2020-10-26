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

Our database machines need to talk to each other and to the monitor node, so
let's create a virtual network.

.. code-block:: bash

   az group create \
       --name ha-demo \
       --location eastus

   az network vnet create \
       --resource-group ha-demo \
       --name ha-demo-net \
       --address-prefix 10.0.0.0/16

We need to open ports 5432 (Postgres) and 22 (SSH) between the machines, and
also give ourselves access from our remote IP. We'll do this with a network
security group and a subnet.

.. code-block:: bash

   az network nsg create \
       --resource-group ha-demo \
       --name ha-demo-nsg

   az network nsg rule create \
       --resource-group ha-demo \
       --nsg-name ha-demo-nsg \
       --name ha-demo-ssh-and-pg \
       --access allow \
       --protocol Tcp \
       --direction Inbound \
       --priority 100 \
       --source-address-prefixes `curl ifconfig.me` 10.0.1.0/24 \
       --source-port-range "*" \
       --destination-address-prefix "*" \
       --destination-port-ranges 22 5432

   az network vnet subnet create \
       --resource-group ha-demo \
       --vnet-name ha-demo-net \
       --name ha-demo-subnet \
       --address-prefixes 10.0.1.0/24 \
       --network-security-group ha-demo-nsg

Finally add four virtual machines (ha-demo-a, ha-demo-b, ha-demo-monitor, and
ha-demo-app). For speed we background the ``az vm create`` processes and run
them in parallel:

.. code-block:: bash

   # create VMs in parallel
   for node in monitor a b app
   do
   az vm create \
       --resource-group ha-demo \
       --name ha-demo-${node} \
       --vnet-name ha-demo-net \
       --subnet ha-demo-subnet \
       --nsg ha-demo-nsg \
       --public-ip-address ha-demo-${node}-ip \
       --image debian \
       --admin-username ha-admin \
       --generate-ssh-keys &
   done
   wait

To make it easier to SSH into these VMs in future steps, let's make a shell
function to retrieve their IP addresses:

.. code-block:: bash

  # run this in your local shell as well

  vm_ip () {
    az vm list-ip-addresses -g ha-demo -n ha-demo-$1 -o tsv \
      --query '[] [] .virtualMachine.network.publicIpAddresses[0].ipAddress'
  }

  # for convenience with ssh

  for node in monitor a b app
  do
  ssh-keyscan -H `vm_ip $node` >> ~/.ssh/known_hosts
  done

Let's review what we created so far.

.. code-block:: bash

  az resource list --output table --query \
    "[?resourceGroup=='ha-demo'].{ name: name, flavor: kind, resourceType: type, region: location }"

This shows the following resources:

::

    Name                             ResourceType                                           Region
    -------------------------------  -----------------------------------------------------  --------
    ha-demo-a                        Microsoft.Compute/virtualMachines                      eastus
    ha-demo-app                      Microsoft.Compute/virtualMachines                      eastus
    ha-demo-b                        Microsoft.Compute/virtualMachines                      eastus
    ha-demo-monitor                  Microsoft.Compute/virtualMachines                      eastus
    ha-demo-appVMNic                 Microsoft.Network/networkInterfaces                    eastus
    ha-demo-aVMNic                   Microsoft.Network/networkInterfaces                    eastus
    ha-demo-bVMNic                   Microsoft.Network/networkInterfaces                    eastus
    ha-demo-monitorVMNic             Microsoft.Network/networkInterfaces                    eastus
    ha-demo-nsg                      Microsoft.Network/networkSecurityGroups                eastus
    ha-demo-a-ip                     Microsoft.Network/publicIPAddresses                    eastus
    ha-demo-app-ip                   Microsoft.Network/publicIPAddresses                    eastus
    ha-demo-b-ip                     Microsoft.Network/publicIPAddresses                    eastus
    ha-demo-monitor-ip               Microsoft.Network/publicIPAddresses                    eastus
    ha-demo-net                      Microsoft.Network/virtualNetworks                      eastus

Install the "pg_autoctl" executable
-----------------------------------

This guide uses Debian Linux, but similar steps will work on other
distributions. All that differs are the packages and paths. See :ref:`install`.

The pg_auto_failover system is distributed as a single ``pg_autoctl`` binary
with subcommands to initialize and manage a replicated PostgreSQL service.
We’ll install the binary with the operating system package manager on all
nodes. It will help us run and observe PostgreSQL.

.. code-block:: bash

  for node in monitor a b app
  do
  az vm run-command invoke \
     --resource-group ha-demo \
     --name ha-demo-${node} \
     --command-id RunShellScript \
     --scripts \
        "sudo touch /home/ha-admin/.hushlogin" \
        "curl https://install.citusdata.com/community/deb.sh | sudo bash" \
        "sudo DEBIAN_FRONTEND=noninteractive apt-get install -q -y postgresql-common" \
        "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" \
        "sudo DEBIAN_FRONTEND=noninteractive apt-get install -q -y postgresql-11-auto-failover-1.4" \
        "sudo usermod -a -G postgres ha-admin" &
  done
  wait

Run a monitor
-------------

The pg_auto_failover monitor is the first component to run. It periodically
attempts to contact the other nodes and watches their health. It also
maintains global state that “keepers” on each node consult to determine their
own roles in the system.

.. code-block:: bash

   # on the monitor virtual machine

   ssh -l ha-admin `vm_ip monitor` -- \
     pg_autoctl create monitor \
       --auth trust \
       --ssl-self-signed \
       --pgdata monitor \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl

This command initializes a PostgreSQL cluster at the location pointed
by the ``--pgdata`` option. When ``--pgdata`` is omitted, ``pg_autoctl``
attempts to use the ``PGDATA`` environment variable. If a PostgreSQL
instance had already existing in the destination directory, this command
would have configured it to serve as a monitor.

``pg_auto_failover``, installs the ``pgautofailover`` Postgres extension, and
grants access to a new ``autoctl_node`` user.

In the Quick Start we use ``--auth trust`` to avoid complex security settings.
The Postgres `trust authentication method`__ is not considered a reasonable
choice for production environments. Consider either using the ``--skip-pg-hba``
option or ``--auth scram-sha-256`` and then setting up passwords yourself.

__ https://www.postgresql.org/docs/current/auth-trust.html_

At this point the monitor is created. Now we'll install it as a service with
systemd so that it will resume if the VM restarts.

.. code-block:: bash

   ssh -T -l ha-admin `vm_ip monitor` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/monitor > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD


Bring up the nodes
------------------

We’ll create the primary database using the ``pg_autoctl create`` subcommand.

.. code-block:: bash

   ssh -l ha-admin `vm_ip a` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --hostname ha-demo-a.internal.cloudapp.net \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@ha-demo-monitor.internal.cloudapp.net/pg_auto_failover?sslmode=require'

Notice the user and database name in the monitor connection string -- these
are what monitor init created. We also give it the path to pg_ctl so that the
keeper will use the correct version of pg_ctl in future even if other versions
of postgres are installed on the system.

In the example above, the keeper creates a primary database. It chooses to set
up node A as primary because the monitor reports there are no other nodes in
the system yet. This is one example of how the keeper is state-based: it makes
observations and then adjusts its state, in this case from "init" to "single."

Also add a setting to trust connections from our "application" VM:

.. code-block:: bash

   ssh -T -l ha-admin `vm_ip a` << CMD
     echo 'hostssl "appdb" "ha-admin" ha-demo-app.internal.cloudapp.net trust' \
       >> ~ha-admin/ha/pg_hba.conf
   CMD

At this point the monitor and primary node are created and running. Next we
need to run the keeper. It’s an independent process so that it can continue
operating even if the PostgreSQL process goes terminates on the node. We'll
install it as a service with systemd so that it will resume if the VM restarts.

.. code-block:: bash

   ssh -T -l ha-admin `vm_ip a` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Next connect to node B and do the same process. We'll do both steps at once:

.. code-block:: bash

   ssh -l ha-admin `vm_ip b` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --hostname ha-demo-b.internal.cloudapp.net \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@ha-demo-monitor.internal.cloudapp.net/pg_auto_failover?sslmode=require'

   ssh -T -l ha-admin `vm_ip b` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

It discovers from the monitor that a primary exists, and then switches its own
state to be a hot standby and begins streaming WAL contents from the primary.

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
