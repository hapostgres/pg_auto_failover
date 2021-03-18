.. _pg_autoctl_create_monitor:

pg_autoctl create monitor
=========================

pg_autoctl create monitor - Initialize a pg_auto_failover monitor node

Synopsis
--------

This command initializes a PostgreSQL cluster and installs the
`pgautofailover` extension so that it's possible to use the new instance to
monitor PostgreSQL services::

  usage: pg_autoctl create monitor  [ --pgdata --pgport --pgctl --hostname ]

  --pgctl           path to pg_ctl
  --pgdata          path to data directory
  --pgport          PostgreSQL's port number
  --hostname        hostname by which postgres is reachable
  --auth            authentication method for connections from data nodes
  --skip-pg-hba     skip editing pg_hba.conf rules
  --run             create node then run pg_autoctl service
  --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)
  --ssl-mode        use that sslmode in connection strings
  --ssl-ca-file     set the Postgres ssl_ca_file to that file path
  --ssl-crl-file    set the Postgres ssl_crl_file to that file path
  --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)
  --server-key      set the Postgres ssl_key_file to that file path
  --server-cert     set the Postgres ssl_cert_file to that file path

Description
-----------

The pg_autoctl tool is the client tool provided by pg_auto_failover to
create and manage Postgres nodes and the pg_auto_failover monitor node. The
command is built with many sub-commands that each have their own manual
page.

Options
-------

The following options are available to ``pg_autoctl create monitor``:

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

--pgport

  Postgres port to use, defaults to 5432.

--hostname

  Hostname or IP address (both v4 and v6 are supported) to use from any
  other node to connect to this node.

  When not provided, a default value is computed by running the following
  algorithm.

    1. We get this machine's "public IP" by opening a connection to the
       8.8.8.8:53 public service. Then we get TCP/IP client address that
       has been used to make that connection.

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

--run

  Immediately run the ``pg_autoctl`` service after having created this
  node.

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
  
