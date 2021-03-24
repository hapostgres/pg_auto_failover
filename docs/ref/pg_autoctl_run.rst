.. _pg_autoctl_run:

pg_autoctl run
==============

pg_autoctl run - Run the pg_autoctl service (monitor or keeper)

Synopsis
--------

This commands starts the processes needed to run a monitor node or a keeper
node, depending on the configuration file that belongs to the ``--pgdata``
option or ``PGDATA`` environment variable.

::

  usage: pg_autoctl run  [ --pgdata --name --hostname --pgport ]

  --pgdata      path to data directory
  --name        pg_auto_failover node name
  --hostname    hostname used to connect from other nodes
  --pgport      PostgreSQL's port number

Description
-----------

When registering Postgres nodes to the pg_auto_failover monitor using the
:ref:`pg_autoctl_create_postgres` command, the nodes are registered with
metadata: the node name, hostname and Postgres port.

The node name is used mostly in the logs and :ref:`pg_autoctl_show_state`
commands and helps human administrators of the formation.

The node hostname and pgport are used by other nodes, including the
pg_auto_failover monitor, to open a Postgres connection.

Both the node name and the node hostname and port can be changed after the
node registration by using either this command (``pg_autoctl run``) or the
:ref:`pg_autoctl_config_set` command.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--name

  Node name used on the monitor to refer to this node. The hostname is a
  technical information, and given Postgres requirements on the HBA setup
  and DNS resolution (both forward and reverse lookups), IP addresses are
  often used for the hostname.

  The ``--name`` option allows using a user-friendly name for your Postgres
  nodes.

--hostname

  Hostname or IP address (both v4 and v6 are supported) to use from any
  other node to connect to this node.

  When not provided, a default value is computed by running the following
  algorithm.

    1. We get this machine's "public IP" by opening a connection to the
       given monitor hostname or IP address. Then we get TCP/IP client
       address that has been used to make that connection.

    2. We then do a reverse DNS lookup on the IP address found in the
       previous step to fetch a hostname for our local machine.

    3. If the reverse DNS lookup is successful , then ``pg_autoctl`` does a
       forward DNS lookup of that hostname.

  When the forward DNS lookup response in step 3. is an IP address found in
  one of our local network interfaces, then ``pg_autoctl`` uses the hostname
  found in step 2. as the default ``--hostname``. Otherwise it uses the IP
  address found in step 1.

  You may use the ``--hostname`` command line option to bypass the whole DNS
  lookup based process and force the local node name to a fixed value.

--pgport

  Postgres port to use, defaults to 5432.
