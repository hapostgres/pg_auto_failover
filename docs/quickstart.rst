.. _postgres_quickstart:

pg_auto_failover Quick Start
============================

In this guide we’ll create a primary and secondary Postgres node and set
up pg_auto_failover to replicate data between them. We’ll simulate failure in
the primary node and see how the system smoothly switches (fails over)
to the secondary.

For illustration, we'll run our databases on virtual machines in the Azure
platform, but the techniques here are relevant to any cloud provider or
on-premise network. We'll use four virtual machines: a primary database, a
secondary database, a monitor, and an "application." The monitor watches the
other nodes’ health, manages global state, and assigns nodes their roles.

.. _quickstart_network:

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
also give ourselves address from our remote IP. We'll do this with a network
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

   # allow VMs to resolve each other via internal DNS
   az network private-dns zone create \
       --resource-group ha-demo \
       --name ha-demo.local
   az network private-dns link vnet create \
       --name ha-demo-zone-link \
       --resource-group ha-demo \
       --virtual-network ha-demo-net \
       --zone-name ha-demo.local \
       --registration-enabled true

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

.. _quickstart_install:

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
        "sudo apt-get install -q -y postgresql-11-auto-failover-1.2" \
        "sudo usermod -a -G postgres ha-admin" &
  done
  wait

.. _quickstart_run_monitor:

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
       --nodename ha-demo-a.internal.cloudapp.net \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --monitor postgres://autoctl_node@ha-demo-monitor.internal.cloudapp.net/pg_auto_failover?sslmode=require

Notice the user and database name in the monitor connection string -- these
are what monitor init created. We also give it the path to pg_ctl so that the
keeper will use the correct version of pg_ctl in future even if other versions
of postgres are installed on the system.

In the example above, the keeper creates a primary database. It chooses to set
up node A as primary because the monitor reports there are no other nodes in
the system yet. This is one example of how the keeper is state-based: it makes
observations and then adjusts its state, in this case from "init" to "single."

At this point the monitor and primary nodes are created and running. Next we
need to run the keeper. It’s an independent process so that it can continue
operating even if the Postgres primary goes down. We'll install it as a service
with systemd so that it will resume if the VM restarts.

.. code-block:: bash

   ssh -l ha-admin `vm_ip a` << CMD
     sudo -i -u postgres \
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
       --nodename ha-demo-b.internal.cloudapp.net \
       --pgctl /usr/lib/postgresql/11/bin/pg_ctl \
       --nodename `hostname -I` \
       --monitor postgres://autoctl_node@ha-demo-monitor/pg_auto_failover

   ssh -l ha-admin `vm_ip b` << CMD
     sudo -i -u postgres \
       pg_autoctl -q show systemd --pgdata ~ha-admin/ha > pgautofailover.service
     sudo mv pgautofailover.service /etc/systemd/system
     sudo systemctl daemon-reload
     sudo systemctl enable pgautofailover
     sudo systemctl start pgautofailover
   CMD

It discovers from the monitor that a primary exists, and then switches its own
state to be a hot standby and begins streaming WAL contents from the primary.

Watch the replication
---------------------

First let’s verify that the monitor knows about our nodes, and see what
states it has assigned them:

.. code-block:: text

   # on the monitor virtual machine

   sudo -i -u postgres \
     pg_autoctl show state \
       --pgdata monitor

                              Name |   Port | Group |  Node |     Current State |    Assigned State
   --------------------------------+--------+-------+-------+-------------------+------------------
   ha-demo-a.internal.cloudapp.net |   5432 |     0 |     1 |           primary |           primary
   ha-demo-b.internal.cloudapp.net |   5432 |     0 |     2 |         secondary |         secondary

This looks good. We can add data to the primary, and watch it get
reflected in the secondary.

.. code-block:: bash

   # on your local machine

   # add data to primary
   psql TODO -p 6010 \
     -c 'create table foo as select generate_series(1,1000000) bar;'

   # query secondary
   psql TODO -p 6011 -c 'select count(*) from foo;'
     count
   ---------
    1000000

Cause a failover
----------------

This plot is too boring, time to introduce a problem. We’ll turn off the
primary and watch the secondary get promoted.

In one terminal let’s keep an eye on events:

.. code-block:: bash

   watch pg_autoctl show events --pgdata ./monitor

In another terminal we’ll turn off the virtual server.

.. code-block:: bash

   az vm stop \
     --resource-group ha-demo \
     --name ha-demo-a

After a number of failed attempts to talk to node A, the monitor determines
the node is unhealthy and puts it into the "demoted" state.  The monitor
promotes node B to be the new primary.

.. code-block:: bash

   TODO
   pg_autoctl show state --pgdata ./monitor
        Name |   Port | Group |  Node |     Current State |    Assigned State
   ----------+--------+-------+-------+-------------------+------------------
   127.0.0.1 |   6010 |     0 |     1 |           demoted |        catchingup
   127.0.0.1 |   6011 |     0 |     2 |      wait_primary |      wait_primary


Node B cannot be considered in full "primary" state since there is no
secondary present. It is marked as "wait_primary" until a secondary
appears.

A client, whether a web server or just psql, can list multiple
hosts in its PostgreSQL connection string, and use the parameter
``target_session_attrs`` to add rules about which server to choose.

To discover the url to use in our case, the following command can be used:

.. code-block:: bash

   pg_autoctl show uri --pgdata ./monitor


            Type |    Name | Connection String
   -----------+---------+-------------------------------
      monitor | monitor | port=6000 dbname=pg_auto_failover host=/tmp user=autoctl_node
    formation | default | postgres://127.0.0.1:6010,127.0.0.1:6011/?target_session_attrs=read-write

Here we ask to connect to either node A or B -- whichever supports reads and
writes:

.. code-block:: bash

   psql \
     'postgres://localhost:6010,localhost:6011/postgres?target_session_attrs=read-write&sslmode=require'

When nodes A and B were both running, psql would connect to node A
because B would be read-only. However now that A is offline and B is
writeable, psql will connect to B. We can insert more data:

.. code-block:: sql

   -- on the prompt from the psql command above:
   insert into foo select generate_series(1000001, 2000000);

Resurrect node A
----------------

Let’s increase the disk space for node A, so it's able to run again.

.. code-block:: bash

   rm /mnt/node_a/bigfile

Now the next time the keeper retries, it brings the node back. Node A
goes through the state "catchingup" while it updates its data to match
B. Once that's done, A becomes a secondary, and B is now a full primary.

.. code-block:: bash

   pg_autoctl show state --pgdata ./monitor
        Name |   Port | Group |  Node |     Current State |    Assigned State
   ----------+--------+-------+-------+-------------------+------------------
   127.0.0.1 |   6010 |     0 |     1 |         secondary |         secondary
   127.0.0.1 |   6011 |     0 |     2 |           primary |           primary


What's more, if we connect directly to node A and run a query we can see
it contains the rows we inserted while it was down.

.. code-block:: bash

  psql -p 6010 -c 'select count(*) from foo;'
    count
  ---------
   2000000
