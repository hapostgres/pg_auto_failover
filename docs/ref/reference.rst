pg_autoctl commands reference
=============================

pg_auto_failover comes with a PostgreSQL extension and a service:

  - The *pgautofailover* PostgreSQL extension implements the pg_auto_failover monitor.
  - The ``pg_autoctl`` command manages pg_auto_failover services.

pg_autoctl
----------

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
    + create    Create a pg_auto_failover node, or formation
    + drop      Drop a pg_auto_failover node, or formation
    + config    Manages the pg_autoctl configuration
    + show      Show pg_auto_failover information
    + enable    Enable a feature on a formation
    + disable   Disable a feature on a formation
      run       Run the pg_autoctl service (monitor or keeper)
      stop      signal the pg_autoctl service for it to stop
      reload    signal the pg_autoctl for it to reload its configuration
      help      print help message
      version   print pg_autoctl version

    pg_autoctl create
      monitor      Initialize a pg_auto_failover monitor node
      postgres     Initialize a pg_auto_failover standalone postgres node
      formation    Create a new formation on the pg_auto_failover monitor

    pg_autoctl drop
      node       Drop a node from the pg_auto_failover monitor
      formation  Drop a formation on the pg_auto_failover monitor

    pg_autoctl config
      check  Check pg_autoctl configuration
      get    Get the value of a given pg_autoctl configuration variable
      set    Set the value of a given pg_autoctl configuration variable

    pg_autoctl show
      uri     Show the postgres uri to use to connect to pg_auto_failover nodes
      events  Prints monitor's state of nodes in a given formation and group
      state   Prints monitor's state of nodes in a given formation and group

    pg_autoctl enable
      secondary  Enable secondary nodes on a formation

    pg_autoctl disable
      secondary  Disable secondary nodes on a formation

The first step consists of creating a pg_auto_failover monitor thanks to the command
``pg_autoctl create monitor``, and the command ``pg_autoctl show uri`` can then be
used to find the Postgres connection URI string to use as the ``--monitor``
option to the ``pg_autoctl create`` command for the other nodes of the
formation.

.. _pg_autoctl_create_monitor:

pg_auto_failover Monitor
^^^^^^^^^^^^^^^^^^^^^^^^

The main piece of the pg_auto_failover deployment is the monitor. The following
commands are dealing with the monitor:

  - ``pg_autoctl create monitor``

     This command initializes a PostgreSQL cluster and installs the
     `pgautofailover` extension so that it's possible to use the new
     instance to monitor PostgreSQL services::

      $ pg_autoctl create monitor --help
      pg_autoctl create monitor: Initialize a pg_auto_failover monitor node
      usage: pg_autoctl create monitor  [ --pgdata --pgport --pgctl --nodename ]

        --pgctl       path to pg_ctl
        --pgdata      path to data directory
        --pgport      PostgreSQL's port number
        --nodename    hostname by which postgres is reachable
        --auth        authentication method for connections from data nodes

     The ``--pgdata`` option is mandatory and default to the environment
     variable ``PGDATA``. The ``--pgport`` default value is 5432, and the
     ``--pgctl`` option defaults to the first ``pg_ctl`` entry found in your
     `PATH`.

     The ``--nodename`` option allows setting the hostname that the other
     nodes of the cluster will use to access to the monitor. When not
     provided, a default value is computed by running the following
     algorithm:

       1. Open a connection to the 8.8.8.8:53 public service and looks the
          TCP/IP client address that has been used to make that connection.

       2. Do a reverse DNS lookup on this IP address to fetch a hostname for
          our local machine.

       3. If the reverse DNS lookup is successfull , then `pg_autoctl` does
          with a forward DNS lookup of that hostname.

     When the forward DNS lookup repsonse in step 3. is an IP address found
     in one of our local network interfaces, then `pg_autoctl` uses the
     hostname found in step 2. as the default `--nodename`. Otherwise it
     uses the IP address found in step 1.

     You may use the `--nodename` command line option to bypass the whole
     DNS lookup based process and force the local node name to a fixed
     value.
     
     The ``--auth`` option allows setting up authentication method to be used
     for connections from data nodes with ``autoctl_node`` user.
     If this option is used, please make sure password is manually set on
     the monitor, and appropriate setting is added to `.pgpass` file on data node.
     
     See :ref:`pg_auto_failover_security` for notes on `.pgpass`

  - ``pg_autoctl run``

    This command makes sure that the PostgreSQL instance for the monitor is
    running, then connects to it and listens to the monitor notifications,
    displaying them as log messages::

      $ pg_autoctl run --help
      pg_autoctl run: Run the pg_autoctl service (monitor or keeper)
      usage: pg_autoctl run  [ --pgdata ]

        --pgdata      path to data directory

    The option `--pgdata` (or the environment variable ``PGDATA``) allows
    pg_auto_failover to find the monitor configuration file.

  - ``pg_autoctl create formation``

    This command registers a new formation on the monitor, with the
    specified kind::

      $ pg_autoctl create formation --help
      pg_autoctl create formation: Create a new formation on the pg_auto_failover monitor
      usage: pg_autoctl create formation  [ --pgdata --formation --kind --dbname --with-secondary --without-secondary ]

        --pgdata            path to data directory
        --formation         name of the formation to create
        --kind              formation kind, either "pgsql" or "citus"
        --dbname            name for postgres database to use in this formation
        --enable-secondary  create a formation that has multiple nodes that can be
                            used for fail over when others have issues
        --disable-secondary create a citus formation without nodes to fail over to


  - ``pg_autoctl drop formation``

    This command drops an existing formation on the monitor::

      $ pg_autoctl drop formation --help
      pg_autoctl drop formation: Drop a formation on the pg_auto_failover monitor
      usage: pg_autoctl drop formation  [ --pgdata --formation ]

        --pgdata      path to data directory
        --formation   name of the formation to drop


pg_autoctl show command
^^^^^^^^^^^^^^^^^^^^^^^

To discover current information about a pg_auto_failover setup, the ``pg_autoctl show``
commands can be used, from any node in the setup.

  - ``pg_autoctl show uri``

    This command outputs the monitor or the coordinator Postgres URI to use
    from an application to connect to the service::

      $ pg_autoctl show uri --help
      pg_autoctl show uri: Show the postgres uri to use to connect to pg_auto_failover nodes
      usage: pg_autoctl show uri  [ --pgdata --formation ]

        --pgdata      path to data directory
        --formation   show the coordinator uri of given formation

    The option ``--formation default`` outputs the Postgres URI to use to
    connect to the Postgres server.

  - ``pg_autoctl show events``

    This command outputs the latest events known to the pg_auto_failover monitor::

      $ pg_autoctl show events --help
      pg_autoctl show events: Prints monitor's state of nodes in a given formation and group
      usage: pg_autoctl show events  [ --pgdata --formation --group --count ]

        --pgdata      path to data directory
        --formation   formation to query, defaults to 'default'
        --group       group to query formation, defaults to all
        --count       how many events to fetch, defaults to 10

    The events are available in the ``pgautofailover.event`` table in the
    PostgreSQL instance where the monitor runs, so the ``pg_autoctl show
    events`` command needs to be able to connect to the monitor. To this
    end, the ``--pgdata`` option is used either to determine a local
    PostgreSQL instance to connect to, when used on the monitor, or to
    determine the pg_auto_failover keeper configuration file and read the monitor
    URI from there.

    See below for more information about ``pg_auto_failover`` configuration files.

    The options ``--formation`` and ``--group`` allow to filter the output
    to a single formation, and group. The ``--count`` option limits the
    output to that many lines.

  - ``pg_autoctl show state``

    This command outputs the current state of the formation and groups
    registered to the pg_auto_failover monitor::

      $ pg_autoctl show state --help
      pg_autoctl show state: Prints monitor's state of nodes in a given formation and group
      usage: pg_autoctl show state  [ --pgdata --formation --group ]

        --pgdata      path to data directory
        --formation   formation to query, defaults to 'default'
        --group       group to query formation, defaults to all

    For details about the options to the command, see above in the ``pg_autoctl
    show events`` command.

.. _pg_autoctl_create_postgres:

pg_auto_failover Postgres Node Initialization
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

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

    --pgctl       path to pg_ctl
    --pgdata      path to data director
    --pghost      PostgreSQL's hostname
    --pgport      PostgreSQL's port number
    --listen      PostgreSQL's listen_addresses
    --username    PostgreSQL's username
    --dbname      PostgreSQL's database name
    --nodename    pg_auto_failover node
    --formation   pg_auto_failover formation
    --monitor     pg_auto_failover Monitor Postgres URL
    --auth        authentication method for connections from monitor
    --allow-removing-pgdata   Allow pg_autoctl to remove the database directory

Three different modes of initialization are supported by this command,
corresponding to as many implementation strategies.

  1. Initialize a primary node from scratch

     This happens when ``--pgdata`` (or the environment variable ``PGDATA``)
     points to an non-existing or empty directory. Then the given
     ``--nodename`` is registered to the pg_auto_failover ``--monitor`` as a
     member of the ``--formation``.

     The monitor answers to the registration call with a state to assign to
     the new member of the group, either *SINGLE* or *WAIT_STANDBY*. When
     the assigned state is *SINGLE*, then ``pg_autoctl create postgres``
     procedes to initialize a new PostgreSQL instance.

  2. Initialize an already existing primary server

     This happens when ``--pgdata`` (or the environment variable ``PGDATA``)
     points to an already existing directory that belongs to a PostgreSQL
     instance. The standard PostgreSQL tool ``pg_controldata`` is used to
     recognize whether the directory belongs to a PostgreSQL instance.

     In that case, the given ``--nodename`` is registered to the monitor in
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

     When the command ends succesfully, a PostgreSQL secondary server has
     been created with ``pg_basebackup`` and is now started, catching-up to
     the primary server.

Currently, ``pg_autoctl create`` doesn't know how to initialize from an already
running PostgreSQL standby node. In that situation, it is necessary to
prepare a new secondary system from scratch.

When `--nodename` is omitted, it is computed as above (see
:ref:`_pg_autoctl_create_monitor`), with the difference that step 1 uses the
monitor IP and port rather than the public service 8.8.8.8:53.

The ``--auth`` option allows setting up authentication method to be used when
monitor node makes a connection to data node with `pgautofailover_monitor` user.
If this option is, please make sure password is manually set on the data
node, and appropriate setting is added to `.pgpass` file on monitor node.

See :ref:`pg_auto_failover_security` for notes on `.pgpass`

.. _pg_autoctl_configuration:

pg_autoctl configuration and state files
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

When initializing a pg_auto_failover keeper service via pg_autoctl, both a configuration file and a
state file are created. pg_auto_failover follows the `XDG Base Directory
Specification
<https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html>`_.

When initializing a pg_auto_failover keeper with ``--pgdata /data/pgsql``, then:

  - ``~/.config/pg_autoctl/data/pgsql/pg_autoctl.cfg``

    is the configuration file for the PostgreSQL instance located at
    ``/data/pgsql``, written in the INI file format. Here's an example of
    such a configuration file::

      [pg_autoctl]
      role = keeper
      monitor = postgres://autoctl_node@192.168.1.34:6000/pg_auto_failover
      formation = default
      group = 1
      nodename = node1.db

      [postgresql]
      pgdata = /data/pgsql/
      pg_ctl = /usr/pgsql-10/bin/pg_ctl
      dbname = postgres
      host = /tmp
      port = 5000

      [replication]
      slot = pgautofailover_standby
      maximum_backup_rate = 100M

      [timeout]
      network_partition_timeout = 20
      prepare_promotion_catchup = 30
      prepare_promotion_walreceiver = 5
      postgresql_restart_failure_timeout = 20
      postgresql_restart_failure_max_retries = 3

    It is possible to edit the configuration file with a tooling of your
    choice, and with the ``pg_autoctl config`` subcommands, see below.

  - ``~/.local/share/pg_autoctl/data/pgsql/pg_autoctl.state``

    is the state file for the pg_auto_failover keeper service taking care of the
    PostgreSQL instance located at ``/data/pgsql``, written in binary
    format. This file is not intended to be written by anything else than
    ``pg_autoctl`` itself. In case of state corruption, see the trouble
    shooting section of the documentation.

To output, edit and check entries of the configuration, the following
commands are provided. Both commands need the `--pgdata` option or the
`PGDATA` environment variable to be set in order to find the intended
configuration file::

  pg_autoctl config check [--pgdata <pgdata>]
  pg_autoctl config get [--pgdata <pgdata>] section.option
  pg_autoctl config set [--pgdata <pgdata>] section.option value

Running the pg_auto_failover Keeper service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To run the pg_auto_failover keeper as a background service in your OS, use the
following command::

  $ pg_autoctl run --help
  pg_autoctl run: Run the pg_autoctl service (monitor or keeper)
  usage: pg_autoctl run  [ --pgdata ]

    --pgdata      path to data directory

Thanks to using the XDG Base Directory Specification for our configuration
and state file, the only option needed to run the service is ``--pgdata``,
which defaults to the environment variable ``PGDATA``.

Removing a node from the pg_auto_failover monitor
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

To clean-up an installation and remove a PostgreSQL instance from pg_auto_failover
keeper and monitor, use the following command::

  $ pg_autoctl drop node --help
  pg_autoctl drop node: Drop a node from the pg_auto_failover monitor
  usage: pg_autoctl drop node  [ --pgdata ]

    --pgdata      path to data directory

The ``pg_autoctl drop node`` connects to the monitor and removes the
nodename from it, then removes the local pg_auto_failover keeper state file. The
configuration file is not removed.

.. _pg_autoctl_maintenance:

pg_autoctl do
-------------

When testing pg_auto_failover, it is helpful to be able to play with the local nodes
using the same lower-level API as used by the pg_auto_failover Finite State Machine
transitions. The low-level API is made available through the following
commands, only available in debug environments::

  $ PG_AUTOCTL_DEBUG=1 pg_autoctl help
    pg_autoctl
    + create    Create a pg_auto_failover node, or formation
    + drop      Drop a pg_auto_failover node, or formation
    + config    Manages the pg_autoctl configuration
    + show      Show pg_auto_failover information
    + enable    Enable a feature on a formation
    + disable   Disable a feature on a formation
    + do        Manually operate the keeper
      run       Run the pg_autoctl service (monitor or keeper)
      stop      signal the pg_autoctl service for it to stop
      reload    signal the pg_autoctl for it to reload its configuration
      help      print help message
      version   print pg_autoctl version

    pg_autoctl create
      monitor      Initialize a pg_auto_failover monitor node
      postgres     Initialize a pg_auto_failover standalone postgres node
      formation    Create a new formation on the pg_auto_failover monitor

    pg_autoctl drop
      node       Drop a node from the pg_auto_failover monitor
      formation  Drop a formation on the pg_auto_failover monitor

    pg_autoctl config
      check  Check pg_autoctl configuration
      get    Get the value of a given pg_autoctl configuration variable
      set    Set the value of a given pg_autoctl configuration variable

    pg_autoctl show
      uri     Show the postgres uri to use to connect to pg_auto_failover nodes
      events  Prints monitor's state of nodes in a given formation and group
      state   Prints monitor's state of nodes in a given formation and group

    pg_autoctl enable
      secondary    Enable secondary nodes on a formation
      maintenance  Enable Postgres maintenance mode on this node

    pg_autoctl disable
      secondary    Disable secondary nodes on a formation
      maintenance  Disable Postgres maintenance mode on this node

    pg_autoctl do
    + monitor      Query a pg_auto_failover monitor
    + fsm          Manually manage the keeper's state
    + primary      Manage a PostgreSQL primary server
    + standby      Manage a PostgreSQL standby server
      discover     Discover local PostgreSQL instance, if any
      destroy      destroy a node, unregisters it, rm -rf PGDATA

    pg_autoctl do monitor
    + get       Get information from the monitor
      register  Register the current node with the monitor
      active    Call in the pg_auto_failover Node Active protocol
      version   Check that monitor version is 1.0; alter extension update if not

    pg_autoctl do monitor get
      primary      Get the primary node from pg_auto_failover in given formation/group
      other        Get the other node from the pg_auto_failover group of nodename/port
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
      defaults  Add default settings to postgresql.conf
    + adduser   Create users on primary
    + hba       Manage pg_hba settings on the primary server

    pg_autoctl do primary slot
      create  Create a replication slot on the primary server
      drop    Drop a replication slot on the primary server

    pg_autoctl do primary syncrep
      enable   Enable synchronous replication on the primary server
      disable  Disable synchronous replication on the primary server

    pg_autoctl do primary adduser
      monitor  add a local user for queries from the monitor
      replica  add a local user with replication privileges

    pg_autoctl do primary hba
      setup  Make sure the standby has replication access in pg_hba

    pg_autoctl do standby
      init     Initialize the standby server using pg_basebackup
      rewind   Rewind a demoted primary server using pg_rewind
      promote  Promote a standby server to become writable

    pg_autoctl do show
      ipaddr    Print this node's IP address information
      cidr      Print this node's CIDR information
      lookup    Print this node's DNS lookup information
      nodename  Print this node's default nodename
