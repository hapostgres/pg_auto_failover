.. _reference:

pg_autoctl commands reference
=============================

pg_auto_failover comes with a PostgreSQL extension and a service:

  - The *pgautofailover* PostgreSQL extension implements the pg_auto_failover monitor.
  - The ``pg_autoctl`` command manages pg_auto_failover services.

This section lists all the ``pg_autoctl`` commands, and we have a lot of
them. For getting started with the essential set of commands, see the
:ref:`how-to` section.

pg_autoctl command list
-----------------------

The ``pg_autoctl`` command implements a service that is meant to run in the
background. The command line controls the service, and uses the service API
for manual operations.

The pg_auto_failover service implements the steps to set up replication and node
promotion according to instructions from the pg_auto_failover monitor. Every 5
seconds, the keeper service (started by ``pg_autoctl run``) connects to the
PostgreSQL monitor database and runs ``SELECT pg_auto_failover.node_active(...)``
to simultaneously communicate its current state and obtain its goal state. It
stores its current state in its own state file, as configured.

The ``pg_autoctl`` command includes facilities for initializing and operating
both the pg_auto_failover monitor and the PostgreSQL instances with a pg_auto_failover
keeper::

  $ pg_autoctl help
    pg_autoctl
    + create   Create a pg_auto_failover node, or formation
    + drop     Drop a pg_auto_failover node, or formation
    + config   Manages the pg_autoctl configuration
    + show     Show pg_auto_failover information
    + enable   Enable a feature on a formation
    + disable  Disable a feature on a formation
    + get      Get a pg_auto_failover node, or formation setting
    + set      Set a pg_auto_failover node, or formation setting
    + perform  Perform an action orchestrated by the monitor
      run      Run the pg_autoctl service (monitor or keeper)
      stop     signal the pg_autoctl service for it to stop
      reload   signal the pg_autoctl for it to reload its configuration
      status   Display the current status of the pg_autoctl service
      help     print help message
      version  print pg_autoctl version

    pg_autoctl create
      monitor    Initialize a pg_auto_failover monitor node
      postgres   Initialize a pg_auto_failover standalone postgres node
      formation  Create a new formation on the pg_auto_failover monitor

    pg_autoctl drop
      monitor    Drop the pg_auto_failover monitor
      node       Drop a node from the pg_auto_failover monitor
      formation  Drop a formation on the pg_auto_failover monitor

    pg_autoctl config
      check  Check pg_autoctl configuration
      get    Get the value of a given pg_autoctl configuration variable
      set    Set the value of a given pg_autoctl configuration variable

    pg_autoctl show
      uri            Show the postgres uri to use to connect to pg_auto_failover nodes
      events         Prints monitor's state of nodes in a given formation and group
      state          Prints monitor's state of nodes in a given formation and group
      settings       Print replication settings for a formation from the monitor
      standby-names  Prints synchronous_standby_names for a given group
      file           List pg_autoctl internal files (config, state, pid)
      systemd        Print systemd service file for this node

    pg_autoctl enable
      secondary    Enable secondary nodes on a formation
      maintenance  Enable Postgres maintenance mode on this node
      ssl          Enable SSL configuration on this node
      monitor      Enable a monitor for this node to be orchestrated from

    pg_autoctl disable
      secondary    Disable secondary nodes on a formation
      maintenance  Disable Postgres maintenance mode on this node
      ssl          Disable SSL configuration on this node
      monitor      Disable the monitor for this node

    pg_autoctl get
    + node       get a node property from the pg_auto_failover monitor
    + formation  get a formation property from the pg_auto_failover monitor

    pg_autoctl get node
      replication-quorum  get replication-quorum property from the monitor
      candidate-priority  get candidate property from the monitor

    pg_autoctl get formation
      settings              get replication settings for a formation from the monitor
      number-sync-standbys  get number_sync_standbys for a formation from the monitor

    pg_autoctl set
    + node       set a node property on the monitor
    + formation  set a formation property on the monitor

    pg_autoctl set node
      metadata            set metadata on the monitor
      replication-quorum  set replication-quorum property on the monitor
      candidate-priority  set candidate property on the monitor

    pg_autoctl set formation
      number-sync-standbys  set number-sync-standbys for a formation on the monitor

    pg_autoctl perform
      failover    Perform a failover for given formation and group
      switchover  Perform a switchover for given formation and group
      promotion   Perform a failover that promotes a target node

The first step consists of creating a pg_auto_failover monitor thanks to the
command ``pg_autoctl create monitor``, and the command ``pg_autoctl show
uri`` can then be used to find the Postgres connection URI string to use as
the ``--monitor`` option to the ``pg_autoctl create`` command for the other
nodes of the formation.

.. _pg_autoctl_create_monitor:

pg_auto_failover Monitor
------------------------

The main piece of the pg_auto_failover deployment is the monitor. The following
commands are dealing with the monitor:

pg_autoctl create monitor
^^^^^^^^^^^^^^^^^^^^^^^^^

This command initializes a PostgreSQL cluster and installs the
`pgautofailover` extension so that it's possible to use the new instance to
monitor PostgreSQL services::

 $ pg_autoctl create monitor --help
  pg_autoctl create monitor: Initialize a pg_auto_failover monitor node
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

The ``--pgdata`` option is mandatory and defaults to the environment
variable ``PGDATA``. The ``--pgport`` default value is 5432, and the
``--pgctl`` option defaults to the first ``pg_ctl`` entry found in your
`PATH`.

The ``--hostname`` option allows setting the hostname that the other nodes
of the cluster will use to access to the monitor. When not provided, a
default value is computed by running the following algorithm:

  1. We get this machine's "public IP" by opening a connection to the
     8.8.8.8:53 public service. Then we get TCP/IP client address
     that has been used to make that connection.

  2. We then do a reverse DNS lookup on the IP address found in the previous
     step to fetch a hostname for our local machine.

  3. If the reverse DNS lookup is successful , then ``pg_autoctl`` does a
     forward DNS lookup of that hostname.

When the forward DNS lookup response in step 3. is an IP address found in
one of our local network interfaces, then ``pg_autoctl`` uses the hostname
found in step 2. as the default ``--hostname``. Otherwise it uses the IP
address found in step 1.

You may use the ``--hostname`` command line option to bypass the whole DNS
lookup based process and force the local node name to a fixed value.

The ``--auth`` option allows setting up authentication method to be used for
connections from data nodes with ``autoctl_node`` user. When testing
pg_auto_failover for the first time using ``--auth trust`` makes things
easier. When getting production ready, review your options here and choose
at least ``--auth scram-sha-256`` and make sure password is manually set on
the monitor, and appropriate setting is added to `.pgpass` file on data
node. You could also use some of the advanced Postgres authentication
mechanism such as SSL certificates.

See :ref:`security` for notes on `.pgpass`

pg_autoctl run
^^^^^^^^^^^^^^

This commands starts the processes needed to run a monitor node or a keeper
node, depending on the configuration file that belongs to the ``--pgdata``
option or PGDATA environment variable.

In the case of a monitor, ``pg_autoctl run`` starts a Postgres service where
we run the pg_auto_failover database, and a listener process that listens to
the notifications sent by the Postgres instance::

  $ pg_autoctl run --help
  pg_autoctl run: Run the pg_autoctl service (monitor or keeper)
  usage: pg_autoctl run  [ --pgdata --nodename --hostname --pgport ]

    --pgdata      path to data directory
    --nodename    pg_auto_failover node name
    --hostname    hostname used to connect from other nodes
    --pgport      PostgreSQL's port number

The option `--pgdata` (or the environment variable ``PGDATA``) allows
pg_auto_failover to find the monitor configuration file.

pg_autoctl create formation
^^^^^^^^^^^^^^^^^^^^^^^^^^^

This command registers a new formation on the monitor, with the
specified kind::

  $ pg_autoctl create formation --help
  pg_autoctl create formation: Create a new formation on the pg_auto_failover monitor
  usage: pg_autoctl create formation  [ --pgdata --formation --kind --dbname --with-secondary --without-secondary ]

    --pgdata               path to data directory
    --formation            name of the formation to create
    --kind                 formation kind, either "pgsql" or "citus"
    --dbname               name for postgres database to use in this formation
    --enable-secondary     create a formation that has multiple nodes that can be
                           used for fail over when others have issues
    --disable-secondary    create a citus formation without nodes to fail over to
    --number-sync-standbys minimum number of standbys to confirm write

pg_autoctl drop formation
^^^^^^^^^^^^^^^^^^^^^^^^^

This command drops an existing formation on the monitor::

  $ pg_autoctl drop formation --help
  pg_autoctl drop formation: Drop a formation on the pg_auto_failover monitor
  usage: pg_autoctl drop formation  [ --pgdata --formation ]

    --pgdata      path to data directory
    --formation   name of the formation to drop

pg_autoctl show command
-----------------------

To discover current information about a pg_auto_failover setup, the
``pg_autoctl show`` commands can be used, from any node in the setup.

pg_autoctl show uri
^^^^^^^^^^^^^^^^^^^

This command outputs the monitor or the coordinator Postgres URI to use from
an application to connect to the service::

  $ pg_autoctl show uri --help
  pg_autoctl show uri: Show the postgres uri to use to connect to pg_auto_failover nodes
  usage: pg_autoctl show uri  [ --pgdata --monitor --formation --json ]

    --pgdata      path to data directory
    --monitor     show the monitor uri
    --formation   show the coordinator uri of given formation
    --json        output data in the JSON format

The option ``--formation default`` outputs the Postgres URI to use to
connect to the Postgres server.

pg_autoctl show events
^^^^^^^^^^^^^^^^^^^^^^

This command outputs the latest events known to the pg_auto_failover
monitor::

  $ pg_autoctl show events --help
  pg_autoctl show events: Prints monitor's state of nodes in a given formation and group
  usage: pg_autoctl show events  [ --pgdata --formation --group --count ]

    --pgdata      path to data directory
    --formation   formation to query, defaults to 'default'
    --group       group to query formation, defaults to all
    --count       how many events to fetch, defaults to 10
    --json        output data in the JSON format

The events are available in the ``pgautofailover.event`` table in the
PostgreSQL instance where the monitor runs, so the ``pg_autoctl show
events`` command needs to be able to connect to the monitor. To this end,
the ``--pgdata`` option is used either to determine a local PostgreSQL
instance to connect to, when used on the monitor, or to determine the
pg_auto_failover keeper configuration file and read the monitor URI from
there.

See below for more information about ``pg_auto_failover`` configuration
files.

The options ``--formation`` and ``--group`` allow to filter the output to a
single formation, and group. The ``--count`` option limits the output to
that many lines.

pg_autoctl show state
^^^^^^^^^^^^^^^^^^^^^

This command outputs the current state of the formation and groups
registered to the pg_auto_failover monitor::

  $ pg_autoctl show state --help
  pg_autoctl show state: Prints monitor's state of nodes in a given formation and group
  usage: pg_autoctl show state  [ --pgdata --formation --group ]

    --pgdata      path to data directory
    --formation   formation to query, defaults to 'default'
    --group       group to query formation, defaults to all
    --local       show local data, do not connect to the monitor
    --json        output data in the JSON format

For details about the options to the command, see above in the ``pg_autoctl
show events`` command.

The ``--local`` option displays information from the local node cache,
without contacting the monitor. Note that when Postgres is not running the
LSN position is then the `Latest checkpoint location` as taken from the
output of the ``pg_controldata`` command, and might be an earlier location
than the most recent one sent to the monitor.

pg_autoctl show settings
^^^^^^^^^^^^^^^^^^^^^^^^

This command allows to review all the replication settings of a given
formation (defaults to `'default'` as usual)::

   pg_autoctl get formation settings --help
   pg_autoctl get formation settings: get replication settings for a formation from the monitor
   usage: pg_autoctl get formation settings  [ --pgdata ] [ --json ] [ --formation ]

     --pgdata      path to data directory
     --json        output data in the JSON format
     --formation   pg_auto_failover formation

See :ref:`pg_autoctl_get_formation_settings` for details. Both commands are
aliases.

pg_autoctl show file
^^^^^^^^^^^^^^^^^^^^

This command outputs the configuration, state, initial state, and pid files
used by this instance. The files are placed in a path that follows the `XDG
Base Directory Specification
<https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html>`_
and in a way allows to find them when given only ``$PGDATA``, as in
PostgreSQL::

  $ pg_autoctl show file --help
  pg_autoctl show file: List pg_autoctl internal files (config, state, pid)
  usage: pg_autoctl show file  [ --pgdata --all --config | --state | --init | --pid --contents ]

    --pgdata      path to data directory
    --all         show all pg_autoctl files
    --config      show pg_autoctl configuration file
    --state       show pg_autoctl state file
    --init        show pg_autoctl initialisation state file
    --pid         show pg_autoctl PID file
    --contents    show selected file contents
    --json        output data in the JSON format

The command ``pg_auctoctl show file`` outputs a table containing the config
and pid files for a monitor, and the four files config, state, init, and pid
for a keeper. When one of the options with the same name is used, a single
line containing only the file path is printed.

When the option ``--contents`` is used, the contents of the file are printed
instead of the file name. For binary state files, the content of the file is
parsed from binary and displayed in a human friendly way.

When the options ``--contents --json`` are used together, the output is then
formated as a JSON document.

pg_autoctl show systemd
^^^^^^^^^^^^^^^^^^^^^^^

This command outputs a configuration unit that is suitable for registering
``pg_autoctl`` as a systemd service.

.. _pg_autoctl_create_postgres:

pg_auto_failover Postgres Node Initialization
---------------------------------------------

Initializing a pg_auto_failover Postgres node is done with one of the available
``pg_autoctl create`` commands, depending on which kind of node is to be
initialized:

  - monitor

    The pg_auto_failover monitor is a special case and has been documented in the
    previous sections.

  - postgres

    The command ``pg_autoctl create postgres`` initializes a standalone
    Postgres node to a pg_auto_failover monitor. The monitor is then handling
    auto-failover for this Postgres node (as soon as a secondary has been
    registered too, and is known to be healthy).

Here's the full help message for the ``pg_autoctl create postgres`` command.
The other commands accept the same set of options.

::

  $ pg_autoctl create postgres --help
  pg_autoctl create postgres: Initialize a pg_auto_failover standalone postgres node
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
    --candidate-priority    priority of the node to be promoted to become primary
    --replication-quorum    true if node participates in write quorum
    --ssl-self-signed setup network encryption using self signed certificates (does NOT protect against MITM)
    --ssl-mode        use that sslmode in connection strings
    --ssl-ca-file     set the Postgres ssl_ca_file to that file path
    --ssl-crl-file    set the Postgres ssl_crl_file to that file path
    --no-ssl          don't enable network encryption (NOT recommended, prefer --ssl-self-signed)
    --server-key      set the Postgres ssl_key_file to that file path
    --server-cert     set the Postgres ssl_cert_file to that file path

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

When `--hostname` is omitted, it is computed as above (see
:ref:`pg_autoctl_create_monitor`), with the difference that step 1 uses the
monitor IP and port rather than the public service 8.8.8.8:53.

The ``--auth`` option allows setting up authentication method to be used
when monitor node makes a connection to data node with
`pgautofailover_monitor` user. As with the ``pg_autoctl create monitor``
command, you could use ``--auth trust`` when playing with pg_auto_failover
at first and consider something production grade later. Also, consider using
``--skip-pg-hba`` if you already have your own provisioning tools with a
security compliance process.

See :ref:`security` for notes on `.pgpass`

pg_autoctl run
^^^^^^^^^^^^^^

This commands starts the processes needed to run a monitor node or a keeper
node, depending on the configuration file that belongs to the ``--pgdata``
option or PGDATA environment variable.

In the case of a monitor, ``pg_autoctl run`` starts a Postgres service where
we run the pg_auto_failover database, and a listener process that listens to
the notifications sent by the Postgres instance::

  $ pg_autoctl run --help
  pg_autoctl run: Run the pg_autoctl service (monitor or keeper)
  usage: pg_autoctl run  [ --pgdata --nodename --hostname --pgport ]

    --pgdata      path to data directory
    --nodename    pg_auto_failover node name
    --hostname    hostname used to connect from other nodes
    --pgport      PostgreSQL's port number

The option `--pgdata` (or the environment variable ``PGDATA``) allows
pg_auto_failover to find the monitor configuration file.

Replication Settings
--------------------

The following commands allow to get and set the replication settings of
pg_auto_failover nodes. See :ref:`architecture_setup` for details about
those settings.

.. _pg_autoctl_get_formation_settings:

pg_autoctl get formation settings
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This command allows to review all the replication settings of a given
formation (defaults to `'default'` as usual)::

   pg_autoctl get formation settings --help
   pg_autoctl get formation settings: get replication settings for a formation from the monitor
   usage: pg_autoctl get formation settings  [ --pgdata ] [ --json ] [ --formation ]

     --pgdata      path to data directory
     --json        output data in the JSON format
     --formation   pg_auto_failover formation

The output contains setting and values that apply at different contexts, as
shown here with a formation of four nodes, where ``node_4`` is not
participating in the replication quorum and also not a candidate for
failover::

     Context |    Name |                   Setting | Value
   ----------+---------+---------------------------+-------------------------------------------------------------
   formation | default |      number_sync_standbys | 1
     primary |  node_1 | synchronous_standby_names | 'ANY 1 (pgautofailover_standby_3, pgautofailover_standby_2)'
        node |  node_1 |        replication quorum | true
        node |  node_2 |        replication quorum | true
        node |  node_3 |        replication quorum | true
        node |  node_4 |        replication quorum | false
        node |  node_1 |        candidate priority | 50
        node |  node_2 |        candidate priority | 50
        node |  node_3 |        candidate priority | 50
        node |  node_4 |        candidate priority | 0

Three replication settings context are listed:

  1. The `"formation"` context contains a single entry, the value of
     ``number_sync_standbys`` for the target formation.

  2. The `"primary"` context contains one entry per group of Postgres nodes
     in the formation, and shows the current value of the
     ``synchronous_standby_names`` Postgres setting as computed by the
     monitor. It should match what's currently set on the primary node
     unless while applying a change, as show by the primary being in the
     APPLY_SETTING state.

  3. The `"node"` context contains two entry per nodes, one line shows the
     replication quorum setting of nodes, and another line shows the
     candidate priority of nodes.

This command gives an overview of all the settings that apply to the current
formation.

pg_autoctl get formation number-sync-standbys
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl get formation number-sync-standbys --help
   pg_autoctl get formation number-sync-standbys: get number_sync_standbys for a formation from the monitor
   usage: pg_autoctl get formation number-sync-standbys  [ --pgdata ] [ --json ]

     --pgdata      path to data directory

pg_autoctl set formation number-sync-standbys
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl set formation number-sync-standbys --help
   pg_autoctl set formation number-sync-standbys: set number-sync-standbys for a formation on the monitor
   usage: pg_autoctl set formation number-sync-standbys  [ --pgdata ] [ --json ] <number_sync_standbys>

     --pgdata      path to data directory

pg_autoctl get node replication-quorum
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl get node replication-quorum --help
   pg_autoctl get node replication-quorum: get replication-quorum property from the monitor
   usage: pg_autoctl get node replication-quorum  [ --pgdata ] [ --json ]

     --pgdata      path to data directory

pg_autoctl set node replication-quorum
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl set node replication-quorum --help
   pg_autoctl set node replication-quorum: set replication-quorum property on the monitor
   usage: pg_autoctl set node replication-quorum  [ --pgdata ] [ --json ] <true|false>

     --pgdata      path to data directory


pg_autoctl get node candidate-priority
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl get node candidate-priority --help
   pg_autoctl get node candidate-priority: get candidate property from the monitor
   usage: pg_autoctl get node candidate-priority  [ --pgdata ] [ --json ]

     --pgdata      path to data directory

pg_autoctl set node candidate-priority
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

   pg_autoctl set node candidate-priority --help
   pg_autoctl set node candidate-priority: set candidate property on the monitor
   usage: pg_autoctl set node candidate-priority  [ --pgdata ] [ --json ] <priority: 0..100>

     --pgdata      path to data directory

.. _pg_autoctl_configuration:

Configuration and State Files
-----------------------------

When initializing a pg_auto_failover keeper service via pg_autoctl, both a
configuration file and a state file are created. pg_auto_failover follows
the `XDG Base Directory Specification
<https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html>`_.

When initializing a pg_auto_failover keeper with ``--pgdata /data/pgsql``, then:

Sample configuration file
^^^^^^^^^^^^^^^^^^^^^^^^^

The pg_autoctl configuration file for an instance serving the data directory
at ``/data/pgsql`` is found at
``~/.config/pg_autoctl/data/pgsql/pg_autoctl.cfg``, written in the INI
format.

It is possible to get the location of the configuration file by using the
command ``pg_autoctl show file --config --pgdata /data/pgsql`` and to output
its content by using the command ``pg_autoctl show
file --config --content --pgdata /data/pgsql``.

Here's an example of such a configuration file::

  [pg_autoctl]
  role = keeper
  monitor = postgres://autoctl_node@localhost:5000/pg_auto_failover?sslmode=require
  formation = default
  group = 0
  name = node_1
  hostname = localhost
  nodekind = standalone

  [postgresql]
  pgdata = /data/pgsql/
  pg_ctl = /usr/pgsql-12/bin/pg_ctl
  dbname = postgres
  host = /tmp
  port = 5001
  proxyport = 0
  listen_addresses = *
  auth_method = trust

  [ssl]
  active = 1
  sslmode = require
  cert_file = /data/pgsql/server.crt
  key_file = /data/pgsql/server.key

  [replication]
  maximum_backup_rate = 100M
  backup_directory = /private/tmp/pgaf/backup/node_1

  [timeout]
  network_partition_timeout = 20
  prepare_promotion_catchup = 30
  prepare_promotion_walreceiver = 5
  postgresql_restart_failure_timeout = 20
  postgresql_restart_failure_max_retries = 3


It is possible to edit the configuration file with a tooling of your choice,
and with the ``pg_autoctl config`` subcommands, see below.

Editing pg_autoctl configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To output, edit and check entries of the configuration, the following
commands are provided. Both commands need the `--pgdata` option or the
`PGDATA` environment variable to be set in order to find the intended
configuration file::

  pg_autoctl config check [--pgdata <pgdata>]
  pg_autoctl config get [--pgdata <pgdata>] section.option
  pg_autoctl config set [--pgdata <pgdata>] section.option value

Sample state file
^^^^^^^^^^^^^^^^^

The pg_autoctl state file for an instance serving the data directory at
``/data/pgsql`` is found at
``~/.local/share/pg_autoctl/data/pgsql/pg_autoctl.state``, written in a
specific binary format.

This file is not intended to be written by anything else than ``pg_autoctl``
itself. In case of state corruption, see the trouble shooting section of the
documentation.

It is possible to get the location of the state file by using the command
``pg_autoctl show file --state --pgdata /data/pgsql`` and to output its
content by using the command ``pg_autoctl show
file --state --content --pgdata /data/pgsql``. Here's an example of the
output when using that command::

  $ pg_autoctl show file --state --content --pgdata /data/pgsql
  Current Role:             secondary
  Assigned Role:            secondary
  Last Monitor Contact:     Mon Dec 23 13:31:23 2019
  Last Secondary Contact:   0
  pg_autoctl state version: 1
  group:                    0
  node id:                  1
  nodes version:            0
  PostgreSQL Version:       1100
  PostgreSQL CatVersion:    201809051
  PostgreSQL System Id:     6772497431723510412

Init State File
^^^^^^^^^^^^^^^

The pg_autoctl init state file for an instance serving the data directory at
``/data/pgsql`` is found at
``~/.local/share/pg_autoctl/data/pgsql/pg_autoctl.init``, written in a
specific binary format.

This file is not intended to be written by anything else than ``pg_autoctl``
itself. In case of state corruption, see the trouble shooting section of the
documentation.

This initialization state file only exists during the initialization of a
pg_auto_failover node. In normal operations, this file does not exists.

It is possible to get the location of the state file by using the command
``pg_autoctl show file --init --pgdata /data/pgsql`` and to output its
content by using the command ``pg_autoctl show
file --init --content --pgdata /data/pgsql``.

Sample PID file
^^^^^^^^^^^^^^^

The pg_autoctl PID file for an instance serving the data directory at
``/data/pgsql`` is found at ``/tmp/pg_autoctl/data/pgsql/pg_autoctl.pid``,
written in a specific text format.

The PID file is located in a temporary directory by default, or in the
``XDG_RUNTIME_DIR`` directory when this is setup. Here an example file::

  $ pg_autoctl show file --pgdata /data/pgsql --pid --contents
  23651
  /tmp/pgaf/a
  1.3
  1.3
  9437186
  23653 postgres
  63763 node-active

The 1st line contains the PID of the currently running pg_autoctl command,
the 2nd line contains the data directory of this service, the 3rd line
contains the version string of the main pg_autoctl process running, the 4th
line contains the monitor's pgautofailover extension version that this
process is compatible with, the 5th line contains the Posix semaphore id
that is used to protect from race conditions when writing logs, and finally
we have one line per sub-process (or service) that the main pg_autoctl
command is running.

Removing a node from the monitor
--------------------------------

To clean-up an installation and remove a PostgreSQL instance from pg_auto_failover
keeper and monitor, use the following command::

    $ pg_autoctl drop node --help
	pg_autoctl drop node: Drop a node from the pg_auto_failover monitor
    usage: pg_autoctl drop node [ --pgdata --destroy --hostname --pgport ]

      --pgdata      path to data directory
      --destroy     also destroy Postgres database
      --hostname    hostname to remove from the monitor
      --pgport      Postgres port of the node to remove

The ``pg_autoctl drop node`` connects to the monitor and removes the node
from it, then removes the local pg_auto_failover keeper state file. The
configuration file is not removed.

It is possible to run the ``pg_autoctl drop node`` command either from the
node itself and then the ``--destroy`` option is available to wipe out
everything, including configuration files and PGDATA; or to run the command
from the monitor and then use the ``--hostname`` and ``--nodeport`` options
to target a (presumably dead) node to remove from the monitor registration.

.. _pg_autoctl_maintenance:

PG_AUTOCTL_DEBUG commands
-------------------------

When testing pg_auto_failover, it is helpful to be able to play with the
local nodes using the same lower-level API as used by the pg_auto_failover
Finite State Machine transitions. The low-level API is made available
through the following ``pg_autoctl do`` commands, only available in debug
environments::

  $ PG_AUTOCTL_DEBUG=1 pg_autoctl help
  pg_autoctl
  + create   Create a pg_auto_failover node, or formation
  + drop     Drop a pg_auto_failover node, or formation
  + config   Manages the pg_autoctl configuration
  + show     Show pg_auto_failover information
  + enable   Enable a feature on a formation
  + disable  Disable a feature on a formation
  + get      Get a pg_auto_failover node, or formation setting
  + set      Set a pg_auto_failover node, or formation setting
  + perform  Perform an action orchestrated by the monitor
  + do       Manually operate the keeper
    run      Run the pg_autoctl service (monitor or keeper)
    stop     signal the pg_autoctl service for it to stop
    reload   signal the pg_autoctl for it to reload its configuration
    status   Display the current status of the pg_autoctl service
    help     print help message
    version  print pg_autoctl version

  pg_autoctl create
    monitor    Initialize a pg_auto_failover monitor node
    postgres   Initialize a pg_auto_failover standalone postgres node
    formation  Create a new formation on the pg_auto_failover monitor

  pg_autoctl drop
    monitor    Drop the pg_auto_failover monitor
    node       Drop a node from the pg_auto_failover monitor
    formation  Drop a formation on the pg_auto_failover monitor

  pg_autoctl config
    check  Check pg_autoctl configuration
    get    Get the value of a given pg_autoctl configuration variable
    set    Set the value of a given pg_autoctl configuration variable

  pg_autoctl show
    uri            Show the postgres uri to use to connect to pg_auto_failover nodes
    events         Prints monitor's state of nodes in a given formation and group
    state          Prints monitor's state of nodes in a given formation and group
    standby-names  Prints synchronous_standby_names for a given group
    file           List pg_autoctl internal files (config, state, pid)
    systemd        Print systemd service file for this node

  pg_autoctl enable
    secondary    Enable secondary nodes on a formation
    maintenance  Enable Postgres maintenance mode on this node
    ssl          Enable SSL configuration on this node

  pg_autoctl disable
    secondary    Disable secondary nodes on a formation
    maintenance  Disable Postgres maintenance mode on this node
    ssl          Disable SSL configuration on this node

  pg_autoctl get
  + node       get a node property from the pg_auto_failover monitor
  + formation  get a formation property from the pg_auto_failover monitor

  pg_autoctl get node
    replication-quorum  get replication-quorum property from the monitor
    candidate-priority  get candidate property from the monitor

  pg_autoctl get formation
    settings              get replication settings for a formation from the monitor
    number-sync-standbys  get number_sync_standbys for a formation from the monitor

  pg_autoctl set
  + node       set a node property on the monitor
  + formation  set a formation property on the monitor

  pg_autoctl set node
    metadata            set metadata on the monitor
    replication-quorum  set replication-quorum property on the monitor
    candidate-priority  set candidate property on the monitor

  pg_autoctl set formation
    number-sync-standbys  set number-sync-standbys for a formation on the monitor

  pg_autoctl perform
    failover    Perform a failover for given formation and group
    switchover  Perform a switchover for given formation and group
    promotion   Perform a failover that promotes a target node

  pg_autoctl do
  + monitor  Query a pg_auto_failover monitor
  + fsm      Manually manage the keeper's state
  + primary  Manage a PostgreSQL primary server
  + standby  Manage a PostgreSQL standby server
  + show     Show some debug level information
  + pgsetup  Manage a local Postgres setup
  + pgctl    Signal the pg_autoctl postgres service
  + service  Run pg_autoctl sub-processes (services)

  pg_autoctl do monitor
  + get                 Get information from the monitor
    register            Register the current node with the monitor
    active              Call in the pg_auto_failover Node Active protocol
    version             Check that monitor version is 1.3; alter extension update if not
    parse-notification  parse a raw notification message

  pg_autoctl do monitor get
    primary      Get the primary node from pg_auto_failover in given formation/group
    others       Get the other nodes from the pg_auto_failover group of hostname/port
    coordinator  Get the coordinator node from the pg_auto_failover formation

  pg_autoctl do fsm
    init    Initialize the keeper's state on-disk
    state   Read the keeper's state from disk and display it
    list    List reachable FSM states from current state
    gv      Output the FSM as a .gv program suitable for graphviz/dot
    assign  Assign a new goal state to the keeper
    step    Make a state transition if instructed by the monitor

  pg_autoctl do primary
  + slot      Manage replication slot on the primary server
  + syncrep   Manage the synchronous replication setting on the primary server
  + adduser   Create users on primary
    defaults  Add default settings to postgresql.conf

  pg_autoctl do primary slot
    create  Create a replication slot on the primary server
    drop    Drop a replication slot on the primary server

  pg_autoctl do primary syncrep
    enable   Enable synchronous replication on the primary server
    disable  Disable synchronous replication on the primary server

  pg_autoctl do primary adduser
    monitor  add a local user for queries from the monitor
    replica  add a local user with replication privileges

  pg_autoctl do standby
    init        Initialize the standby server using pg_basebackup
    rewind      Rewind a demoted primary server using pg_rewind
    promote     Promote a standby server to become writable
    receivewal  Receivewal in the PGDATA/pg_wal directory

  pg_autoctl do show
    ipaddr    Print this node's IP address information
    cidr      Print this node's CIDR information
    lookup    Print this node's DNS lookup information
    hostname  Print this node's default hostname

  pg_autoctl do pgsetup
    discover  Discover local PostgreSQL instance, if any
    ready     Return true is the local Postgres server is ready
    wait      Wait until the local Postgres server is ready
    logs      Outputs the Postgres startup logs
    tune      Compute and log some Postgres tuning options

  pg_autoctl do pgctl
    on   Signal pg_autoctl postgres service to ensure Postgres is running
    off  Signal pg_autoctl postgres service to ensure Postgres is stopped

  pg_autoctl do service
  + getpid        Get the pid of pg_autoctl sub-processes (services)
  + restart       Restart pg_autoctl sub-processes (services)
    pgcontroller  pg_autoctl supervised postgres controller
    postgres      pg_autoctl service that start/stop postgres when asked
    listener      pg_autoctl service that listens to the monitor notifications
    node-active   pg_autoctl service that implements the node active protocol

  pg_autoctl do service getpid
    postgres     Get the pid of the pg_autoctl postgres controller service
    listener     Get the pid of the pg_autoctl monitor listener service
    node-active  Get the pid of the pg_autoctl keeper node-active service

  pg_autoctl do service restart
    postgres     Restart the pg_autoctl postgres controller service
    listener     Restart the pg_autoctl monitor listener service
    node-active  Restart the pg_autoctl keeper node-active service

  pg_autoctl do tmux
    script   Produce a tmux script for a demo or a test case (debug only)
    session  Run a tmux session for a demo or a test case
    stop     Stop pg_autoctl processes that belong to a tmux session
    wait     Wait until a given node has been registered on the monitor
    clean    Clean-up a tmux session processes and root dir

An easy way to get started with pg_auto_failover in a localhost only
formation with three nodes is to run the following command::

  $ PG_AUTOCTL_DEBUG=1 pg_autoctl do tmux session \
       --root /tmp/pgaf \
	   --first-pgport 9000 \
	   --nodes 4 \
	   --layout tiled

This requires the command ``tmux`` to be available in your PATH. The
``pg_autoctl do tmux session`` commands prepares a self-contained root
directory where to create pg_auto_failover nodes and their configuration,
then prepares a tmux script, and then runs the script with a command such as::

  /usr/local/bin/tmux -v start-server ; source-file /tmp/pgaf/script-9000.tmux

The tmux session contains a single tmux window multiple panes:

 - one pane for the monitor
 - one pane per Postgres nodes, here 4 of them
 - one pane for running ``watch pg_autoctl show state``
 - one extra pane for an interactive shell.

Usually the first two commands to run in the interactive shell, once the
formation is stable (one node is primary, the other ones are all secondary),
are the following::

  $ pg_autoctl get formation settings
  $ pg_autoctl perform failover
