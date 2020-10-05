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

Create virtual network
----------------------

In this section we create all the pieces “manually” and you can copy/paste
the commands, change the resource group name if you need to, and by the end
of copy/pasting all the commands you have the needed test environment for
the main :ref:`tutorial`.

If you'd rather use the all automated solution that ships in
pg_auto_failover 1.4.1, see :ref:`script_single_automated`.

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

.. _script_single_automated:

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

Adding to the command line shown above the option ``--script`` (not used in
the previous example) allows a dry-run of the command that outputs a script
that you can re-use later, or audit for details.

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

Finaly, the option ``--from-source`` allows to provision the VM nodes from
your current local pg_auto_failover source tree, using ``rsync`` to upload
your local sources to the VMs:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create region \
      --prefix ha-demo-dim \
	  --region paris \
	  --location francecentral \
	  --monitor \
	  --nodes 2 \
	  --from-source

Once this script is finished the three virtual machines
``ha-demo-dim-paris-monitor`` and ``ha-demo-dim-paris-a`` and
``ha-demo-dim-paris-b`` and ``ha-demo-dim-paris-app`` are provisioned and
running, ready for us to create our Postgres instances.

When provisioning from sources, it is possible to re-sync and re-build
pg_auto_failover on the Azure VM nodes with the following command:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure sync \
      --prefix ha-demo-dim \
	  --region paris \
	  --monitor \
	  --nodes 2

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
      Name |  Node |       Host:Port |       LSN | Reachable |       Current State |      Assigned State
   --------+-------+-----------------+-----------+-----------+---------------------+--------------------
   paris-b |     1 | 10.11.11.6:5432 | 0/7000000 |       yes |             primary |             primary
   paris-a |     2 | 10.11.11.5:5432 | 0/7000000 |       yes |           secondary |           secondary

The rest of this document describe the manual way to get to the same point,
with every azure command clearly shown.


The command ``pg_autoctl do azure show state`` also accepts the ``--watch``
option, and then will use the ``watch`` command once connected on the remote
node and update the state five times a seconds (``watch -n 0.2 ...``).
