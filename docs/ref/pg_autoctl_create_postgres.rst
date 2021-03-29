.. _pg_autoctl_create_postgres:

pg_autoctl create postgres
==========================

pg_autoctl create postgres - Initialize a pg_auto_failover postgres node

Synopsis
--------

The command ``pg_autoctl create postgres`` initializes a standalone Postgres
node to a pg_auto_failover monitor. The monitor is then handling
auto-failover for this Postgres node (as soon as a secondary has been
registered too, and is known to be healthy).

::

   usage: pg_autoctl create postgres

     --pgctl           path to pg_ctl
     --pgdata          path to data directory
     --pghost          PostgreSQL's hostname
     --pgport          PostgreSQL's port number
     --listen          PostgreSQL's listen_addresses
     --username        PostgreSQL's username
     --dbname          PostgreSQL's database name
     --name            pg_auto_failover node name
     --hostname        hostname used to connect from the other nodes
     --formation       pg_auto_failover formation
     --monitor         pg_auto_failover Monitor Postgres URL
     --auth            authentication method for connections from monitor
     --skip-pg-hba     skip editing pg_hba.conf rules
     --pg-hba-lan      edit pg_hba.conf rules for --dbname in detected LAN
     --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)
     --ssl-mode        use that sslmode in connection strings
     --ssl-ca-file     set the Postgres ssl_ca_file to that file path
     --ssl-crl-file    set the Postgres ssl_crl_file to that file path
     --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)
     --server-key      set the Postgres ssl_key_file to that file path
     --server-cert     set the Postgres ssl_cert_file to that file path
     --candidate-priority    priority of the node to be promoted to become primary
     --replication-quorum    true if node participates in write quorum

Description
-----------

Three different modes of initialization are supported by this command,
corresponding to as many implementation strategies.

  1. Initialize a primary node from scratch

     This happens when ``--pgdata`` (or the environment variable ``PGDATA``)
     points to an non-existing or empty directory. Then the given
     ``--hostname`` is registered to the pg_auto_failover ``--monitor`` as a
     member of the ``--formation``.

     The monitor answers to the registration call with a state to assign to
     the new member of the group, either *SINGLE* or *WAIT_STANDBY*. When
     the assigned state is *SINGLE*, then ``pg_autoctl create postgres``
     proceedes to initialize a new PostgreSQL instance.

  2. Initialize an already existing primary server

     This happens when ``--pgdata`` (or the environment variable ``PGDATA``)
     points to an already existing directory that belongs to a PostgreSQL
     instance. The standard PostgreSQL tool ``pg_controldata`` is used to
     recognize whether the directory belongs to a PostgreSQL instance.

     In that case, the given ``--hostname`` is registered to the monitor in
     the tentative *SINGLE* state. When the given ``--formation`` and
     ``--group`` is currently empty, then the monitor accepts the
     registration and the ``pg_autoctl create`` prepares the already existing
     primary server for pg_auto_failover.

  3. Initialize a secondary node from scratch

     This happens when ``--pgdata`` (or the environment variable ``PGDATA``)
     points to a non-existing or empty directory, and when the monitor
     registration call assigns the state *WAIT_STANDBY* in step 1.

     In that case, the ``pg_autoctl create`` command steps through the initial
     states of registering a secondary server, which includes preparing the
     primary server PostgreSQL HBA rules and creating a replication slot.

     When the command ends successfully, a PostgreSQL secondary server has
     been created with ``pg_basebackup`` and is now started, catching-up to
     the primary server.

  4. Initialize a secondary node from an existing data directory

     When the data directory pointed to by the option ``--pgdata`` or the
     environment variable ``PGDATA`` already exists, then pg_auto_failover
     verifies that the system identifier matches the one of the other nodes
     already existing in the same group.

     The system identifier can be obtained with the command
     ``pg_controldata``. All nodes in a physical replication setting must
     have the same system identifier, and so in pg_auto_failover all the
     nodes in a same group have that constraint too.

     When the system identifier matches the already registered system
     identifier of other nodes in the same group, then the node is set-up as
     a standby and Postgres is started with the primary conninfo pointed at
     the current primary.

The ``--auth`` option allows setting up authentication method to be used
when monitor node makes a connection to data node with
``pgautofailover_monitor`` user. As with the
:ref:`pg_autoctl_create_monitor` command, you could use ``--auth trust``
when playing with pg_auto_failover at first and consider something
production grade later. Also, consider using ``--skip-pg-hba`` if you
already have your own provisioning tools with a security compliance process.

See :ref:`security` for notes on `.pgpass`

Options
-------

The following options are available to ``pg_autoctl create postgres``:

--pgctl

  Path to the ``pg_ctl`` tool to use for the version of PostgreSQL you want
  to use.

  Defaults to the ``pg_ctl`` found in the PATH when there is a single entry
  for ``pg_ctl`` in the PATH. Check your setup using ``which -a pg_ctl``.

  When using an RPM based distribution such as RHEL or CentOS, the path
  would usually be ``/usr/pgsql-13/bin/pg_ctl`` for Postgres 13.

  When using a debian based distribution such as debian or ubuntu, the path
  would usually be ``/usr/lib/postgresql/13/bin/pg_ctl`` for Postgres 13.
  Those distributions also use the package ``postgresql-common`` which
  provides ``/usr/bin/pg_config``. This tool can be automatically used by
  ``pg_autoctl`` to discover the default version of Postgres to use on your
  setup.

--pgdata

  Location where to initialize a Postgres database cluster, using either
  ``pg_ctl initdb`` or ``pg_basebackup``. Defaults to the environment
  variable ``PGDATA``.

--pghost

  Hostname to use when connecting to the local Postgres instance from the
  ``pg_autoctl`` process. By default, this field is left blank in the
  connection string, allowing to use Unix Domain Sockets with the default
  path compiled in your ``libpq`` version, usually provided by the Operating
  System. That would be ``/var/run/postgresql`` when using debian or ubuntu.

--pgport

  Postgres port to use, defaults to 5432.

--listen

  PostgreSQL's ``listen_addresses`` to setup. At the moment only one address
  is supported in this command line option.

--username

  PostgreSQL's username to use when connecting to the local Postgres
  instance to manage it.

--dbname

  PostgreSQL's database name to use in your application. Defaults to being
  the same as the ``--username``, or to ``postgres`` when none of those
  options are used.

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

--formation

  Formation to register the node into on the monitor. Defaults to the
  ``default`` formation, that is automatically created in the monitor in the
  :ref:`pg_autoctl_create_monitor` command.

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--auth

  Authentication method used by ``pg_autoctl`` when editing the Postgres HBA
  file to open connections to other nodes. No default value, must be
  provided by the user. The value ``--trust`` is only a good choice for
  testing and evaluation of pg_auto_failover, see :ref:`security` for more
  information.

--skip-pg-hba

  When this option is used then ``pg_autoctl`` refrains from any editing of
  the Postgres HBA file. Please note that editing the HBA file is still
  needed so that other nodes can connect using either read privileges or
  replication streaming privileges.

  When ``--skip-pg-hba`` is used, ``pg_autoctl`` still outputs the HBA
  entries it needs in the logs, it only skips editing the HBA file.

--pg-hba-lan

  When this option is used ``pg_autoctl`` determines the local IP address
  used to connect to the monitor, and retrieves its netmask, and uses that
  to compute your local area network CIDR. This CIDR is then opened for
  connections in the Postgres HBA rules.

  For instance, when the monitor resolves to ``192.168.0.1`` and your local
  Postgres node uses an inferface with IP address
  ``192.168.0.2/255.255.255.0`` to connect to the monitor, then the LAN CIDR
  is computed to be ``192.168.0.0/24``.

--candidate-priority

  Sets this node replication setting for candidate priority to the given
  value (between 0 and 100) at node registration on the monitor. Defaults
  to 50.

--replication-quorum

  Sets this node replication setting for replication quorum to the given
  value (either ``true`` or ``false``) at node registration on the monitor.
  Defaults to ``true``, which enables synchronous replication.

--run

  Immediately run the ``pg_autoctl`` service after having created this node.

--ssl-self-signed

  Generate SSL self-signed certificates to provide network encryption. This
  does not protect against man-in-the-middle kinds of attacks. See
  :ref:`security` for more about our SSL settings.

--ssl-mode

  SSL Mode used by ``pg_autoctl`` when connecting to other nodes,
  including when connecting for streaming replication.

--ssl-ca-file

  Set the Postgres ``ssl_ca_file`` to that file path.

--ssl-crl-file

  Set the Postgres ``ssl_crl_file`` to that file path.

--no-ssl

  Don't enable network encryption. This is not recommended, prefer
  ``--ssl-self-signed``.

--server-key

  Set the Postgres ``ssl_key_file`` to that file path.

--server-cert

  Set the Postgres ``ssl_cert_file`` to that file path.
