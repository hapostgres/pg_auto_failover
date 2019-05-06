.. _postgres_quickstart:

High Availability Quick Start
=============================

In this guide we’ll create a primary and secondary Postgres node and set
up pg_auto_failover to replicate data between them. We’ll simulate failure in
the primary node and see how the system smoothly switches (fails over)
to the secondary.

For simplicity we’ll run all the pieces on a single machine, but in
production there would be three independent machines involved for each
managed PostgreSQL service. One machine for the primary, another for
the secondary, and the last as a monitor which watches the other nodes’
health, manages global state, and assigns nodes their roles.

.. _quickstart_install:

Install the "pg_autoctl" executable
--------------------------------

This guide uses Red Hat Linux, but similar steps will work on other distributions. All that differs are the packages and paths. See :ref:`install`.

pg_auto_failover is distributed as a single binary with subcommands to
initialize and manage a replicated PostgreSQL service. We’ll install the
binary with the operating system package manager.

.. code-block:: bash

  curl https://install.citusdata.com/community/rpm.sh | sudo bash

  sudo yum install -y pg-auto-failover10_11

.. _quickstart_run_monitor:

Run a monitor
-------------

The pg_auto_failover monitor is the first component to run. It periodically attempts
to contact the other nodes and watches their health. It also maintains
global state that “keepers” on each node consult to determine their own
roles in the system.

Because we’re running everything on a single machine, we give each
PostgreSQL instance a separate TCP port. We’ll give the monitor a
distinctive port (6000):

.. code-block:: bash

   sudo su - postgres
   export PATH="/usr/pgsql-11/bin:$PATH"

   pg_autoctl create monitor   \
     --pgdata ./monitor     \
     --pgport 6000          \
     --nodename `pg_autoctl show ipaddr`

This command initializes a PostgreSQL cluster at the location pointed
by the ``--pgdata`` option. When ``--pgdata`` is omitted, ``pg_autoctl``
attempts to use the ``PGDATA`` environment variable. If a PostgreSQL
instance had already existing in the destination directory, this command
would have configured it to serve as a monitor.

In our case, ``pg_autoctl create monitor`` creates a database called
``pg_auto_failover``, installs the ``pgautofailover`` Postgres extension, and grants access
to a new ``autoctl_node`` user.

Bring up the nodes
------------------

We’ll create the primary database using the ``pg_autoctl create`` subcommand.
However in order to simulate what happens if a node runs out of disk space,
we’ll store the primary node’s data files in a small temporary filesystem.

.. code-block:: bash

   # create intentionally small disk for node A
   sudo mkdir /mnt/node_a
   sudo mount -t tmpfs -o size=400m tmpfs /mnt/node_a
   sudo mkdir /mnt/node_a/data
   sudo chown postgres -R /mnt/node_a

   # initialize on that disk
   pg_autoctl create postgres     \
     --pgdata /mnt/node_a/data \
     --pgport 6010             \
     --nodename 127.0.0.1      \
     --pgctl /usr/pgsql-11/bin/pg_ctl \
     --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover

It creates the database in "/mnt/node_a/data" using the port and node
specified. Notice the user and database name in the monitor connection
string -- these are what monitor init created. We also give it the path
to pg_ctl so that the keeper will use the correct version of pg_ctl in
future even if other versions of postgres are installed on the system.

In the example above, the keeper creates a primary database. It chooses
to set up node A as primary because the monitor reports there are no
other nodes in the system yet. This is one example of how the keeper is
state-based: it makes observations and then adjusts its state, in this
case from "init" to "single."

At this point the monitor and primary nodes are created and
running. Next we need to run the keeper. It’s an independent process so
that it can continue operating even if the Postgres primary goes down:

.. code-block:: bash

  pg_autoctl run --pgdata /mnt/node_a/data

This will remain running in the terminal, outputting logs. We can open
another terminal and start a secondary database similarly to how we
created the primary:

.. code-block:: bash

   pg_autoctl create postgres  \
     --pgdata ./node_b      \
     --pgport 6011          \
     --nodename 127.0.0.1   \
     --pgctl /usr/pgsql-11/bin/pg_ctl \
     --monitor postgres://autoctl_node@127.0.0.1:6000/pg_auto_failover

   pg_autoctl run --pgdata ./node_b

All that differs here is we’re running it on another port and pointing
at another data directory. It discovers from the monitor that a primary
exists, and then switches its own state to be a hot standby and begins
streaming WAL contents from the primary.

Watch the replication
---------------------

First let’s verify that the monitor knows about our nodes, and see what
states it has assigned them:

.. code-block:: text

   pg_autoctl show state --pgdata ./monitor
        Name |   Port | Group |  Node |     Current State |    Assigned State
   ----------+--------+-------+-------+-------------------+------------------
   127.0.0.1 |   6010 |     0 |     1 |           primary |           primary
   127.0.0.1 |   6011 |     0 |     2 |         secondary |         secondary

This looks good. We can add data to the primary, and watch it get
reflected in the secondary.

.. code-block:: bash

   # add data to primary
   psql -p 6010 \
     -c 'create table foo as select generate_series(1,1000000) bar;'

   # query secondary
   psql -p 6011 -c 'select count(*) from foo;'
     count
   ---------
    1000000

Cause a failover
----------------

This plot is too boring, time to introduce a problem. We’ll run the
primary out of disk space and watch the secondary get promoted.

In one terminal let’s keep an eye on events:

.. code-block:: bash

   watch pg_autoctl show events --pgdata ./monitor

In another terminal we’ll consume node A’s disk space and try to restart
the database. It will refuse to start up.

.. code-block:: bash

   # postgres went to sleep one night and didn’t wake up…
   pg_ctl -D /mnt/node_a/data stop &&
     dd if=/dev/zero of=/mnt/node_a/bigfile bs=300MB count=1

   # it will refuse to start back up with no free disk space
   df /mnt/node_a
   Filesystem     1K-blocks   Used Available Use% Mounted on
   tmpfs             409600 409600         0 100% /mnt/node_a

After a number of failed attempts to restart node A, its keeper signals
that the node is unhealthy and the node is put into the "demoted" state.
The monitor promotes node B to be the new primary.

.. code-block:: bash

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

   pg_autoctl show uri --formation default --pgdata ./monitor
   postgres://127.0.0.1:6010,127.0.0.1:6011/?target_session_attrs=read-write

Here we ask to connect to either node A or B -- whichever supports reads and
writes:

.. code-block:: bash

   psql \
     'postgres://127.0.0.1:6010,127.0.0.1:6011/?target_session_attrs=read-write'

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
