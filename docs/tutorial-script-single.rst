.. _script_single:

Creating the Azure environment for the "single standby" tutorial
================================================================

In the "single standby" tutorial we create a primary and secondary Postgres
node and set up pg_auto_failover to replicate data between them. Then we
simulate failure in the primary node and see how the system smoothly
switches (fails over) to the secondary.

For illustration, we run our databases on virtual machines in the Azure
platform. The techniques here are relevant to any cloud provider or
on-premise network. We use four virtual machines: a primary database, a
secondary database, a monitor, and an "application." The monitor watches the
other nodes’ health, manages global state, and assigns nodes their roles.

This document shows you how to create the Azure environment with the virtual
machines needed for the rest of the "single standby" tutorial. You have two
options:

  1. use a pg_autoctl command that automates all the steps so that you can
     then play with the resulting setup on Azure

  2. or follow along the instructions here and copy/paste all commands so
     that you have a better understanding of what setup we're using in
     details, and may even choose to do things differently.

Using the pg_autoctl Azure integration commands
-----------------------------------------------

The following command creates a new Azure resource group and then deploy
some network settings (vnet, nsg, nsg rules, subnet), and then creates the
target Virtual Machines and provision them:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create region \
      --prefix ha-demo \
      --region paris \
      --location francecentral \
      --monitor \
      --nodes 2

In this command we are using the prefix ``ha-demo`` for our environment, and
are naming our target region ``paris`` for our own reference. We chose to
name it ``paris`` because our target Azure location in this example is
``francecentral``. You could as well name your region ``nyc`` and choose to
deploy your nodes in ``eastus`` for a much better latency if you happen to
be connected from some place in the North American continent:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create region \
      --prefix ha-demo \
      --region nyc \
      --location eastus \
      --monitor \
      --nodes 2

.. warning::

   At the moment we have split the automation in “technical” commands, one
   idea would be to re-use the tutorial name and provide something like the
   following instead::

      PG_AUTOCTL_DEBUG=1 pg_autoctl do azure tutorial single setup
      PG_AUTOCTL_DEBUG=1 pg_autoctl do azure tutorial single nodes

   Or even::

      PG_AUTOCTL_DEBUG=1 pg_autoctl do azure tutorial single all

   This would also fix the current limitation that the azure commands are
   not creating the ``ha-demo-paris-app`` VM at the moment.

Once this script is finished the three virtual machines
``ha-demo-dim-paris-monitor`` and ``ha-demo-dim-paris-a`` and
``ha-demo-dim-paris-b`` are provisioned and running, ready for us to create
our Postgres instances.

The following command runs the ``pg_autoctl`` commands that create our
Psotgres nodes and register our ``pg_autoctl run`` service to ``systemd``:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create nodes \
      --prefix ha-demo-dim \
      --region paris \
      --monitor \
      --nodes 2

It is of course possible to use the first command and then continue from the
:ref:`script_single_nodes` section.

Once the setup is ready, you can review it with the following commands:

.. code-block:: bash

   $ PG_AUTOCTL_DEBUG=1 pg_autoctl do azure ls --prefix ha-demo-dim --region paris
   11:39:33 72761 INFO   /usr/local/bin/az resource list --output table --query [?resourceGroup=='ha-demo-dim-paris'].{ name: name, flavor: kind, resourceType: type, region: location }
   Name                            ResourceType                             Region
   ------------------------------  ---------------------------------------  -------------
   ha-demo-dim-paris-a             Microsoft.Compute/virtualMachines        francecentral
   ha-demo-dim-paris-b             Microsoft.Compute/virtualMachines        francecentral
   ha-demo-dim-paris-monitor       Microsoft.Compute/virtualMachines        francecentral
   ha-demo-dim-paris-aVMNic        Microsoft.Network/networkInterfaces      francecentral
   ha-demo-dim-paris-bVMNic        Microsoft.Network/networkInterfaces      francecentral
   ha-demo-dim-paris-monitorVMNic  Microsoft.Network/networkInterfaces      francecentral
   ha-demo-dim-paris-nsg           Microsoft.Network/networkSecurityGroups  francecentral
   ha-demo-dim-paris-a-ip          Microsoft.Network/publicIPAddresses      francecentral
   ha-demo-dim-paris-b-ip          Microsoft.Network/publicIPAddresses      francecentral
   ha-demo-dim-paris-monitor-ip    Microsoft.Network/publicIPAddresses      f`05rancecentral
   ha-demo-dim-paris-net           Microsoft.Network/virtualNetworks        francecentral

To review the IP address of your VMs, you can use the following command:

.. code-block:: bash

   $ PG_AUTOCTL_DEBUG=1 pg_autoctl do azure show ips --prefix ha-demo-dim --name paris
   12:32:26 74646 INFO   /usr/local/bin/az vm list-ip-addresses --resource-group ha-demo-dim-paris --query [] [] . { name: virtualMachine.name, "public address": virtualMachine.network.publicIpAddresses[0].ipAddress, "private address": virtualMachine.network.privateIpAddresses[0] } -o table
   Name                       Public address    Private address
   -------------------------  ----------------  -----------------
   ha-demo-dim-paris-a        51.103.34.13      10.11.11.5
   ha-demo-dim-paris-b        51.103.34.39      10.11.11.6
   ha-demo-dim-paris-monitor  51.11.244.130     10.11.11.4


To connect to the monitor and run the ``pg_autoctl show state`` command you
can use the following command:

.. code-block:: bash

   $ PG_AUTOCTL_DEBUG=1 pg_autoctl do azure show state --prefix ha-demo-dim --region paris

   19:05:30 98948 INFO   /usr/local/bin/az vm list-ip-addresses --resource-group ha-demo-dim-paris --query [] [] . { name: virtualMachine.name, "public address": virtualMachine.network.publicIpAddresses[0].ipAddress, "private address": virtualMachine.network.privateIpAddresses[0] } -o json
   19:05:32 98948 INFO   /usr/bin/ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile /dev/null -l ha-admin 51.11.243.15 -- pg_autoctl show state --pgdata ./monitor
   Warning: Permanently added '51.11.243.15' (ECDSA) to the list of known hosts.
      Name |  Node |       Host:Port |       LSN | Reachable |       Current State |      Assigned State
   --------+-------+-----------------+-----------+-----------+---------------------+--------------------
   paris-b |     1 | 10.11.11.6:5432 | 0/7000000 |       yes |             primary |             primary
   paris-a |     2 | 10.11.11.5:5432 | 0/7000000 |       yes |           secondary |           secondary

The rest of this document describe the manual way to get to the same point,
with every azure command clearly shown.

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
        "curl https://install.citusdata.com/community/deb.sh | sudo bash" \
        "sudo apt-get install -q -y postgresql-common" \
        "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" \
        "sudo apt-get install -q -y postgresql-11-auto-failover-1.4" \
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

   ssh -l ha-admin `vm_ip monitor` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/monitor > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD


.. _script_single_nodes:

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

   ssh -l ha-admin `vm_ip a` << CMD
     echo 'hostssl "appdb" "ha-admin" ha-demo-app.internal.cloudapp.net trust' \
       >> ~ha-admin/ha/pg_hba.conf
   CMD

At this point the monitor and primary node are created and running. Next we
need to run the keeper. It’s an independent process so that it can continue
operating even if the PostgreSQL process goes terminates on the node. We'll
install it as a service with systemd so that it will resume if the VM restarts.

.. code-block:: bash

   ssh -l ha-admin `vm_ip a` << CMD
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

   ssh -l ha-admin `vm_ip b` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

It discovers from the monitor that a primary exists, and then switches its own
state to be a hot standby and begins streaming WAL contents from the primary.
