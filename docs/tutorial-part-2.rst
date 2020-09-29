.. _tutorial:

pg_auto_failover Tutorial: Part II
==================================

In this guide we'll create a primary and two secondary Postgres nodes in the
Paris Azure region (`francecentral`). Then because our business requirements
are evolving, we prepare a new region in Amsterdam (`westeurope`) with
another set of 3 nodes that we intend to use as a fallback in case the whole
`francecentral` region would go down.

On-top of those two regions, we also create a lone standby node in the
Dublin area (`northeurope` Azure region) to have another copy of our data
set.

Once this setup is all in place, we're going to orchestrate a migration of
the Postgres production from the Paris region to the Amsterdam region: when
using pg_auto_failover, that's a simple `switchover` command. Some
preparation is still necessary before doing the switchover though, we want
to have the replication quorum and candidate priorities just right.

Create the Azure Network and Connect them
-----------------------------------------

This time, preparing the Azure components is going to be a long story in
itself. We need three resource groups, and in each of them we need a gateway
with its own public IP address. And then we need to connect our regions with
a VNet-to-VNet VPN service. Finally we'll create the virtual machines,
install our Postgres and pg_auto_failover packages, and get started with the
actual failover management parts.

Create virtual network in Paris (francecentral)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Our database machines need to talk to each other and to the monitor node, so
let's create a virtual network.

.. code-block:: bash

   az group create \
       --name ha-demo-dim-paris \
       --location francecentral

   az network vnet create \
       --resource-group ha-demo-dim-paris \
       --name ha-demo-dim-paris-net \
       --address-prefix 10.11.0.0/16

We need to open ports 5432 (Postgres) and 22 (SSH) between the machines, and
also give ourselves access from our remote IP. We'll do this with a network
security group and a subnet.

.. code-block:: bash

   az network nsg create \
       --resource-group ha-demo-dim-paris \
       --name ha-demo-dim-paris-nsg

   az network nsg rule create \
       --resource-group ha-demo-dim-paris \
       --nsg-name ha-demo-dim-paris-nsg \
       --name ha-demo-dim-paris-ssh-and-pg \
       --access allow \
       --protocol Tcp \
       --direction Inbound \
       --priority 100 \
       --source-address-prefixes `curl ifconfig.me` 10.11.1.0/24 \
       --source-port-range "*" \
       --destination-address-prefix "*" \
       --destination-port-ranges 22 5432

   az network vnet subnet create \
       --resource-group ha-demo-dim-paris \
       --vnet-name ha-demo-dim-paris-net \
       --name ha-demo-dim-paris-subnet \
       --address-prefixes 10.11.1.0/24 \
       --network-security-group ha-demo-dim-paris-nsg

Create second region group and network in Amsterdam (westeurope)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Now we create a resource group in Amsterdam and add a network there, that we
will connect to the network in Paris thanks to a VNet-to-VNet gateway.

.. code-block:: bash

   az group create \
       --name ha-demo-dim-amsterdam \
       --location westeurope

   az network vnet create \
       --resource-group ha-demo-dim-amsterdam \
       --name ha-demo-dim-amsterdam-net \
       --address-prefix 10.51.0.0/16

Again, we need to open ports 5432 (Postgres) and 22 (SSH) between the
machines and give ourselves access from our remote IP.

.. code-block:: bash

   az network nsg create \
       --resource-group ha-demo-dim-amsterdam \
       --name ha-demo-dim-amsterdam-nsg

   az network nsg rule create \
       --resource-group ha-demo-dim-amsterdam \
       --nsg-name ha-demo-dim-amsterdam-nsg \
       --name ha-demo-dim-amsterdam-ssh-and-pg \
       --access allow \
       --protocol Tcp \
       --direction Inbound \
       --priority 100 \
       --source-address-prefixes `curl ifconfig.me` 10.51.1.0/24 \
       --source-port-range "*" \
       --destination-address-prefix "*" \
       --destination-port-ranges 22 5432

   az network vnet subnet create \
       --resource-group ha-demo-dim-amsterdam \
       --vnet-name ha-demo-dim-amsterdam-net \
       --name ha-demo-dim-amsterdam-subnet \
       --address-prefixes 10.51.1.0/24 \
       --network-security-group ha-demo-dim-amsterdam-nsg

Create third region group and network in Dublin (northeurope)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Now we create a resource group in Dublin and add a network there, that we
will connect to the network in Paris thanks to a VNet-to-VNet gateway.

.. code-block:: bash

   az group create \
       --name ha-demo-dim-dublin \
       --location northeurope

   az network vnet create \
       --resource-group ha-demo-dim-dublin \
       --name ha-demo-dim-dublin-net \
       --address-prefix 10.61.0.0/16

Again, we need to open ports 5432 (Postgres) and 22 (SSH) between the
machines and give ourselves access from our remote IP.

.. code-block:: bash

   az network nsg create \
       --resource-group ha-demo-dim-dublin \
       --name ha-demo-dim-dublin-nsg

   az network nsg rule create \
       --resource-group ha-demo-dim-dublin \
       --nsg-name ha-demo-dim-dublin-nsg \
       --name ha-demo-dim-dublin-ssh-and-pg \
       --access allow \
       --protocol Tcp \
       --direction Inbound \
       --priority 100 \
       --source-address-prefixes `curl ifconfig.me` 10.61.1.0/24 \
       --source-port-range "*" \
       --destination-address-prefix "*" \
       --destination-port-ranges 22 5432

   az network vnet subnet create \
       --resource-group ha-demo-dim-dublin \
       --vnet-name ha-demo-dim-dublin-net \
       --name ha-demo-dim-dublin-subnet \
       --address-prefixes 10.61.1.0/24 \
       --network-security-group ha-demo-dim-dublin-nsg

Connect the three virtual networks together with gateways
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Create the gateway subnet. Notice that the gateway subnet is named
'GatewaySubnet'. This name is required. Then request a public IP address to
be allocated to the gateway you will create for your VNet. Finally create
the virtual network gateway.

Here's the script for the Paris gateway:

.. code-block:: bash

   az network vnet subnet create \
       --resource-group ha-demo-dim-paris \
       --vnet-name ha-demo-dim-paris-net \
       --name GatewaySubnet \
       --address-prefix 10.11.255.0/27

   az network public-ip create \
       --resource-group ha-demo-dim-paris \
       --name ha-demo-dim-paris-gw-ip \
       --allocation-method Dynamic

   az network vnet-gateway create \
       --resource-group ha-demo-dim-paris \
       --location francecentral \
       --name ha-demo-dim-paris-gw \
       --public-ip-address ha-demo-dim-paris-gw-ip \
       --vnet ha-demo-dim-paris-net \
       --gateway-type Vpn \
       --sku VpnGw1 \
       --vpn-type RouteBased \
       --no-wait

Here's the script for the Amsterdam gateway:

.. code-block:: bash

   az network vnet subnet create \
       --resource-group ha-demo-dim-amsterdam \
       --vnet-name ha-demo-dim-amsterdam-net \
       --name GatewaySubnet \
       --address-prefix 10.51.255.0/27

   az network public-ip create \
       --resource-group ha-demo-dim-amsterdam \
       --name ha-demo-dim-amsterdam-gw-ip \
       --allocation-method Dynamic

   az network vnet-gateway create \
       --resource-group ha-demo-dim-amsterdam \
       --location westeurope \
       --name ha-demo-dim-amsterdam-gw \
       --public-ip-address ha-demo-dim-amsterdam-gw-ip \
       --vnet ha-demo-dim-amsterdam-net \
       --gateway-type Vpn \
       --sku VpnGw1 \
       --vpn-type RouteBased \
       --no-wait

Here's the script for the Dublin gateway:

.. code-block:: bash

   az network vnet subnet create \
       --resource-group ha-demo-dim-dublin \
       --vnet-name ha-demo-dim-dublin-net \
       --name GatewaySubnet \
       --address-prefix 10.61.255.0/27

   az network public-ip create \
       --resource-group ha-demo-dim-dublin \
       --name ha-demo-dim-dublin-gw-ip \
       --allocation-method Dynamic

   az network vnet-gateway create \
       --resource-group ha-demo-dim-dublin \
       --location northeurope \
       --name ha-demo-dim-dublin-gw \
       --public-ip-address ha-demo-dim-dublin-gw-ip \
       --vnet ha-demo-dim-dublin-net \
       --gateway-type Vpn \
       --sku VpnGw1 \
       --vpn-type RouteBased \
       --no-wait

Connect the three regions VNet together
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Now we have to connect the gateways we created together with a VPN
connection, using a shared key. First, let's connect Paris and Amsterdam:

.. code-block:: bash

   az network vpn-connection create \
       --resource-group ha-demo-dim-paris \
       --name paris-to-amsterdam-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-paris -n ha-demo-dim-paris-gw --query id -o tsv) \
       --location francecentral \
       --shared-key "paris-amsterdam-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-amsterdam -n ha-demo-dim-amsterdam-gw --query id -o tsv)

   az network vpn-connection create \
       --resource-group ha-demo-dim-amsterdam \
       --name amsterdam-to-paris-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-amsterdam -n ha-demo-dim-amsterdam-gw --query id -o tsv) \
       --location westeurope \
       --shared-key "paris-amsterdam-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-paris -n ha-demo-dim-paris-gw --query id -o tsv)

Then, let's connect Paris and Dublin:

.. code-block:: bash

   az network vpn-connection create \
       --resource-group ha-demo-dim-paris \
       --name paris-to-dublin-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-paris -n ha-demo-dim-paris-gw --query id -o tsv) \
       --location francecentral \
       --shared-key "paris-dublin-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-dublin -n ha-demo-dim-dublin-gw --query id -o tsv)

   az network vpn-connection create \
       --resource-group ha-demo-dim-dublin \
       --name dublin-to-paris-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-dublin -n ha-demo-dim-dublin-gw --query id -o tsv) \
       --location northeurope \
       --shared-key "paris-dublin-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-paris -n ha-demo-dim-paris-gw --query id -o tsv)


Lastly, let's connect Amsterdam and Dublin:

.. code-block:: bash

   az network vpn-connection create \
       --resource-group ha-demo-dim-amsterdam \
       --name amsterdam-to-dublin-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-amsterdam -n ha-demo-dim-amsterdam-gw --query id -o tsv) \
       --location westeurope \
       --shared-key "amsterdam-dublin-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-dublin -n ha-demo-dim-dublin-gw --query id -o tsv)

   az network vpn-connection create \
       --resource-group ha-demo-dim-dublin \
       --name dublin-to-amsterdam-vpn \
       --vnet-gateway1 $(az network vnet-gateway show -g ha-demo-dim-dublin -n ha-demo-dim-dublin-gw --query id -o tsv) \
       --location northeurope \
       --shared-key "amsterdam-dublin-vpn-key" \
       --vnet-gateway2 $(az network vnet-gateway show -g ha-demo-dim-amsterdam -n ha-demo-dim-amsterdam-gw --query id -o tsv)

With those settings, we are now able to connect Postgres nodes running in
all three regions directly, using our VPN connections, physically routed in
Azure backbone.

Create Virtual Machines
-----------------------

Finally add four virtual machines (ha-demo-a, ha-demo-dim-paris-b,
ha-demo-dim-paris-monitor, and ha-demo-app). For speed we background the
``az vm create`` processes and run them in parallel:

.. code-block:: bash

   # create Paris VMs in parallel
   for node in monitor paris-a paris-b paris-c paris-app
   do
   az vm create \
       --resource-group ha-demo-dim-paris \
       --name ha-demo-dim-${node} \
       --vnet-name ha-demo-dim-paris-net \
       --subnet ha-demo-dim-paris-subnet \
       --nsg ha-demo-dim-paris-nsg \
       --public-ip-address ha-demo-dim-${node}-ip \
       --image debian \
       --admin-username ha-admin \
       --generate-ssh-keys &
   done
   wait

Now create our VMs in Amsterdam:

.. code-block:: bash

   # create Amsterdam VMs in parallel
   for node in amsterdam-a amsterdam-b amsterdam-c
   do
   az vm create \
       --resource-group ha-demo-dim-amsterdam \
       --name ha-demo-dim-${node} \
       --vnet-name ha-demo-dim-amsterdam-net \
       --subnet ha-demo-dim-amsterdam-subnet \
       --nsg ha-demo-dim-amsterdam-nsg \
       --public-ip-address ha-demo-dim-${node}-ip \
       --image debian \
       --admin-username ha-admin \
       --generate-ssh-keys &
   done
   wait

And finally create our VMs in Dublin:

.. code-block:: bash

   # create Dublin VMs in parallel
   for node in dublin-a
   do
   az vm create \
       --resource-group ha-demo-dim-dublin \
       --name ha-demo-dim-${node} \
       --vnet-name ha-demo-dim-dublin-net \
       --subnet ha-demo-dim-dublin-subnet \
       --nsg ha-demo-dim-dublin-nsg \
       --public-ip-address ha-demo-dim-${node}-ip \
       --image debian \
       --admin-username ha-admin \
       --generate-ssh-keys &
   done
   wait

To make it easier to SSH into these VMs in future steps, let's make a shell
function to retrieve their IP addresses:

.. code-block:: bash

  # run this in your local shell as well

  azip () {
    az vm list-ip-addresses -n ha-demo-dim-$1 -o tsv \
      --query '[] [] .virtualMachine.network.publicIpAddresses[0].ipAddress'
  }

Let's review what we created so far.

.. code-block:: bash

  az resource list --output table  \
     --query "[?(resourceGroup=='ha-demo-dim-paris'  \
                 || resourceGroup=='ha-demo-dim-amsterdam' \
                 || resourceGroup=='ha-demo-dim-dublin') \
              ].{ name: name, flavor: kind, resourceType: type, region: location }"

This shows the following resources:

::

   Name                          ResourceType                              Region
   ----------------------------  ----------------------------------------  -------------
   ha-demo-dim-amsterdam-a       Microsoft.Compute/virtualMachines         westeurope
   ha-demo-dim-amsterdam-b       Microsoft.Compute/virtualMachines         westeurope
   ha-demo-dim-amsterdam-c       Microsoft.Compute/virtualMachines         westeurope
   amsterdam-to-dublin-vpn       Microsoft.Network/connections             westeurope
   amsterdam-to-paris-vpn        Microsoft.Network/connections             westeurope
   ha-demo-dim-amsterdam-aVMNic  Microsoft.Network/networkInterfaces       westeurope
   ha-demo-dim-amsterdam-bVMNic  Microsoft.Network/networkInterfaces       westeurope
   ha-demo-dim-amsterdam-cVMNic  Microsoft.Network/networkInterfaces       westeurope
   ha-demo-dim-amsterdam-nsg     Microsoft.Network/networkSecurityGroups   westeurope
   ha-demo-dim-amsterdam-a-ip    Microsoft.Network/publicIPAddresses       westeurope
   ha-demo-dim-amsterdam-b-ip    Microsoft.Network/publicIPAddresses       westeurope
   ha-demo-dim-amsterdam-c-ip    Microsoft.Network/publicIPAddresses       westeurope
   ha-demo-dim-amsterdam-gw-ip   Microsoft.Network/publicIPAddresses       westeurope
   ha-demo-dim-amsterdam-gw      Microsoft.Network/virtualNetworkGateways  westeurope
   ha-demo-dim-amsterdam-net     Microsoft.Network/virtualNetworks         westeurope
   ha-demo-dim-dublin-a          Microsoft.Compute/virtualMachines         northeurope
   dublin-to-amsterdam-vpn       Microsoft.Network/connections             northeurope
   dublin-to-paris-vpn           Microsoft.Network/connections             northeurope
   ha-demo-dim-dublin-aVMNic     Microsoft.Network/networkInterfaces       northeurope
   ha-demo-dim-dublin-nsg        Microsoft.Network/networkSecurityGroups   northeurope
   ha-demo-dim-dublin-a-ip       Microsoft.Network/publicIPAddresses       northeurope
   ha-demo-dim-dublin-gw-ip      Microsoft.Network/publicIPAddresses       northeurope
   ha-demo-dim-dublin-gw         Microsoft.Network/virtualNetworkGateways  northeurope
   ha-demo-dim-dublin-net        Microsoft.Network/virtualNetworks         northeurope
   ha-demo-dim-monitor           Microsoft.Compute/virtualMachines         francecentral
   ha-demo-dim-paris-a           Microsoft.Compute/virtualMachines         francecentral
   ha-demo-dim-paris-app         Microsoft.Compute/virtualMachines         francecentral
   ha-demo-dim-paris-b           Microsoft.Compute/virtualMachines         francecentral
   ha-demo-dim-paris-c           Microsoft.Compute/virtualMachines         francecentral
   paris-to-amsterdam-vpn        Microsoft.Network/connections             francecentral
   paris-to-dublin-vpn           Microsoft.Network/connections             francecentral
   ha-demo-dim-monitorVMNic      Microsoft.Network/networkInterfaces       francecentral
   ha-demo-dim-paris-appVMNic    Microsoft.Network/networkInterfaces       francecentral
   ha-demo-dim-paris-aVMNic      Microsoft.Network/networkInterfaces       francecentral
   ha-demo-dim-paris-bVMNic      Microsoft.Network/networkInterfaces       francecentral
   ha-demo-dim-paris-cVMNic      Microsoft.Network/networkInterfaces       francecentral
   ha-demo-dim-paris-nsg         Microsoft.Network/networkSecurityGroups   francecentral
   ha-demo-dim-monitor-ip        Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-a-ip        Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-app-ip      Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-b-ip        Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-c-ip        Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-gw-ip       Microsoft.Network/publicIPAddresses       francecentral
   ha-demo-dim-paris-gw          Microsoft.Network/virtualNetworkGateways  francecentral
   ha-demo-dim-paris-net         Microsoft.Network/virtualNetworks         francecentral


Install the "pg_autoctl" executable
-----------------------------------

This guide uses Debian Linux, but similar steps will work on other
distributions. All that differs are the packages and paths. See :ref:`install`.

The pg_auto_failover system is distributed as a single ``pg_autoctl`` binary
with subcommands to initialize and manage a replicated PostgreSQL service.
We’ll install the binary with the operating system package manager on all
nodes. It will help us run and observe PostgreSQL.

.. code-block:: bash

  az vm run-command invoke \
     --resource-group ha-demo-dim-monitor \
     --name ha-demo-dim-${node} \
     --command-id RunShellScript \
     --scripts \
        "curl https://install.citusdata.com/community/deb.sh | sudo bash" \
        "sudo apt-get install -q -y postgresql-common" \
        "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" \
        "sudo apt-get install -q -y postgresql-11-auto-failover-1.4" \
        "sudo usermod -a -G postgres ha-admin"

  for node in paris-a paris-b paris-c paris-app amsterdam-a amsterdam-b amsterdam-c dublin-a
  do
  az vm run-command invoke \
     --resource-group ha-demo-dim-$(echo $node | cut -f1 -d-) \
     --name ha-demo-dim-${node} \
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

   ssh -l ha-admin `azip monitor` -- \
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

   ssh -l ha-admin `azip monitor` << CMD
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

   ssh -l ha-admin `azip paris-a` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

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

   ssh -l ha-admin `azip paris-a` << CMD
     echo 'hostssl "appdb" "ha-admin" 10.11.1.7/32 trust' \
       >> ~ha-admin/ha/pg_hba.conf
   CMD

At this point the monitor and primary node are created and running. Next we
need to run the keeper. It’s an independent process so that it can continue
operating even if the PostgreSQL process goes terminates on the node. We'll
install it as a service with systemd so that it will resume if the VM restarts.

.. code-block:: bash

   ssh -l ha-admin `azip paris-a` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Next connect to node B and do the same process. We'll do both steps at once:

.. code-block:: bash

   ssh -l ha-admin `azip paris-b` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip paris-b` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

It discovers from the monitor that a primary exists, and then switches its own
state to be a hot standby and begins streaming WAL contents from the primary.

And we can now start node C the same way:

.. code-block:: bash

   ssh -l ha-admin `azip paris-c` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip paris-c` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD


Node communication
------------------

For convenience, pg_autoctl modifies each node's ``pg_hba.conf`` file to allow
the nodes to connect to one another. For instance, pg_autoctl added the
following lines to node A:

.. code-block:: ini

   # automatically added to node A

   hostssl "appdb" "ha-admin" 10.11.1.6/32 trust
   hostssl replication "pgautofailover_replicator" 10.11.1.8/32 trust
   hostssl "appdb" "pgautofailover_replicator" 10.11.1.8/32 trust

For ``pg_hba.conf`` on the monitor node pg_autoctl inspects the local network
and makes its best guess about the subnet to allow. In our case it guessed
correctly:

.. code-block:: ini

   # automatically added to the monitor

   hostssl "pg_auto_failover" "autoctl_node" 10.11.1.0/24 trust

   # manually added by us for amsterdam and dublin
   hostssl "pg_auto_failover" "autoctl_node" 10.51.1.0/24 trust
   hostssl "pg_auto_failover" "autoctl_node" 10.61.1.0/24 trust

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

   ssh -l ha-admin `azip monitor` -- pg_autoctl show state --pgdata monitor

          Name |  Node |      Host:Port |        LSN | Reachable |       Current State |      Assigned State
   ------------+-------+----------------+------------+-----------+---------------------+--------------------
       paris-a |     4 | 10.11.1.6:5432 | 0/26000108 |       yes |             primary |             primary
       paris-b |     5 | 10.11.1.8:5432 | 0/26000108 |       yes |           secondary |           secondary
       paris-c |     6 | 10.11.1.5:5432 | 0/26000108 |       yes |           secondary |           secondary

This looks good. We can add data to the primary, and later see it appear in the
secondary. We'll connect to the database from inside our "app" virtual machine,
using a connection string obtained from the monitor.

.. code-block:: bash

   ssh -l ha-admin `azip monitor` pg_autoctl show uri --pgdata monitor

         Type |    Name | Connection String
   -----------+---------+-------------------------------
      monitor | monitor | postgres://autoctl_node@ha-demo-dim-monitor.internal.cloudapp.net:5432/pg_auto_failover?sslmode=require
    formation | default | postgres://10.11.1.4:5432,10.11.1.5:5432,10.11.1.6:5432:5432/appdb?target_session_attrs=read-write&sslmode=require

Now we'll get the connection string and store it in a local environment
variable:

.. code-block:: bash

   APP_DB_URI=$( \
     ssh -l ha-admin `azip monitor` \
       pg_autoctl show uri --formation default --pgdata monitor \
   )

The connection string contains both our nodes, comma separated, and includes
the url parameter ``?target_session_attrs=read-write`` telling psql that we
want to connect to whichever of these servers supports reads *and* writes.
That will be the primary server.

.. code-block:: bash

   # connect to database via psql on the app vm and
   # create a table with a million rows
   ssh -l ha-admin -t `azip paris-app` -- \
     psql "\"$APP_DB_URI\"" \
       -c "\"CREATE TABLE foo AS SELECT generate_series(1,1000000) bar;\""

Add nodes in Amsterdam
----------------------

And we can now start Amsterdam nodes the same way:

.. code-block:: bash

   ssh -l ha-admin `azip amsterdam-a` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --name amsterdam-a \
       --candidate-priority 0 \
       --replication-quorum false \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip amsterdam-a` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Then node B in Amsterdam:

.. code-block:: bash

   ssh -l ha-admin `azip amsterdam-b` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --name amsterdam-b \
       --candidate-priority 0 \
       --replication-quorum false \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip amsterdam-b` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Then node C in Amsterdam:

.. code-block:: bash

   ssh -l ha-admin `azip amsterdam-c` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --username ha-admin \
       --dbname appdb \
       --name amsterdam-c \
       --candidate-priority 0 \
       --replication-quorum false \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip amsterdam-c` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Add nodes in Dublin
-------------------

And we can now start Amsterdam nodes the same way:

.. code-block:: bash

   ssh -l ha-admin `azip dublin-a` -- \
     pg_autoctl create postgres \
       --pgdata ha \
       --auth trust \
       --ssl-self-signed \
       --name dublin-a \
       --candidate-priority 0 \
       --replication-quorum false \
       --username ha-admin \
       --dbname appdb \
       --hostname \`hostname -I\` \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor 'postgres://autoctl_node@10.11.1.4/pg_auto_failover?sslmode=require'

   ssh -l ha-admin `azip dublin-a` << CMD
     pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

Check the current state
-----------------------

.. code-block:: bash

   ssh -l ha-admin `azip monitor` -- pg_autoctl show state --pgdata monitor

          Name |  Node |      Host:Port |        LSN | Reachable |       Current State |      Assigned State
   ------------+-------+----------------+------------+-----------+---------------------+--------------------
       paris-a |     4 | 10.11.1.6:5432 | 0/26000108 |       yes |             primary |             primary
       paris-b |     5 | 10.11.1.8:5432 | 0/26000108 |       yes |           secondary |           secondary
       paris-c |     6 | 10.11.1.5:5432 | 0/26000108 |       yes |           secondary |           secondary
   amsterdam-a |     7 | 10.51.1.5:5432 | 0/26000108 |       yes |           secondary |           secondary
   amsterdam-b |     8 | 10.51.1.6:5432 | 0/26000108 |       yes |           secondary |           secondary
   amsterdam-c |    11 | 10.51.1.4:5432 | 0/26000108 |       yes |           secondary |           secondary
      dublin-a |    12 | 10.61.1.4:5432 | 0/26000108 |       yes |           secondary |           secondary


Prepare settings to failover from Paris to Amsterdam
----------------------------------------------------

Say we want to redirect production to Amsterdam. Our primary zone would then
have to migrate from Paris as it is now to Amsterdam, with its local nodes
as sync replicas and the Paris nodes as async ones.

First, we need to change the replication-quorum property for the nodes in
Amsterdam so that they all participate in the quorum, preparing for the
failover: this gives more chances that the nodes have the lastest data when
orchestrating the failover.

.. code-block:: bash

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum true --name amsterdam-a

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum true --name amsterdam-b

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum true --name amsterdam-c

We also want to make the nodes in Amsterdam the candidates for the next
failover. Let's give them priority 65:

.. code-block:: bash

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node candidate-priority 65 --name amsterdam-a

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node candidate-priority 65 --name amsterdam-b

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node candidate-priority 65 --name amsterdam-c

We can verify that our migration settings are in place with the following
summary command:

.. code-block:: bash

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl get formation settings

     Context |        Name |                   Setting | Value
   ----------+-------------+---------------------------+----------------------------------------------------------------------------------------------------------------------------------------------
   formation |     default |      number_sync_standbys | 1
     primary |     paris-a | synchronous_standby_names | 'FIRST 1 (pgautofailover_standby_7, pgautofailover_standby_8, pgautofailover_standby_11, pgautofailover_standby_5, pgautofailover_standby_6)'
        node |     paris-a |        replication quorum | true
        node |     paris-b |        replication quorum | true
        node |     paris-c |        replication quorum | true
        node | amsterdam-b |        replication quorum | true
        node | amsterdam-c |        replication quorum | true
        node |     paris-a |        candidate priority | 50
        node |     paris-b |        candidate priority | 50
        node |     paris-c |        candidate priority | 50
        node | amsterdam-a |        candidate priority | 65
        node | amsterdam-b |        candidate priority | 65
        node | amsterdam-c |        candidate priority | 65

Migrate Postgres production from Paris to Amsterdam in a single command
-----------------------------------------------------------------------

Now that we've added data to node A, let's switch which is considered
the primary and which the secondary. After the switch we'll connect again
and query the data, this time from node B.

.. code-block:: bash

   # initiate failover to node B
   ssh -l ha-admin -t `azip monitor` \
     pg_autoctl perform switchover --pgdata monitor

And when the switchover is done here is the new state of the formation:

.. code-block:: bash

   ssh -l ha-admin `azip monitor` -- pg_autoctl show state --pgdata monitor

          Name |  Node |      Host:Port |        LSN | Reachable |       Current State |      Assigned State
   ------------+-------+----------------+------------+-----------+---------------------+--------------------
       paris-a |     4 | 10.11.1.6:5432 | 0/26000108 |       yes |           secondary |           secondary
       paris-b |     5 | 10.11.1.8:5432 | 0/26000108 |       yes |           secondary |           secondary
       paris-c |     6 | 10.11.1.5:5432 | 0/26000108 |       yes |           secondary |           secondary
   amsterdam-a |     7 | 10.51.1.5:5432 | 0/26000108 |       yes |             primary |             primary
   amsterdam-b |     8 | 10.51.1.6:5432 | 0/26000108 |       yes |           secondary |           secondary
   amsterdam-c |    11 | 10.51.1.4:5432 | 0/26000108 |       yes |           secondary |           secondary
      dublin-a |    12 | 10.61.1.4:5432 | 0/26000108 |       yes |           secondary |           secondary

We should now remove the nodes in Paris from the replication quorum:

.. code-block:: bash

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum false --name paris-a

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum false --name paris-b

   ssh -l ha-admin -t `azip monitor` -- \
    pg_autoctl set node replication-quorum false --name paris-c

Once those nodes are not in the replication quorum anymore, depending on the
application needs it might be time to either remove them entirely (use
``pg_autoctl drop node --destroy`` once ssh'ed into each VM), or you may
want to keep them around.
