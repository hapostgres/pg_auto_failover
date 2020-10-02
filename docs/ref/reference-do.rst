.. _pg_autoctl_maintenance:

PG_AUTOCTL_DEBUG commands
=========================

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
  + azure    manage a set of azure resources for a pg_auto_failover demo

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
    session  Run a a tmux session for a demo or a test case
    stop     Stop pg_autoctl processes that belong to a tmux session
    wait     Wait until a given node has been registered on the monitor
    clean    Clean-up a tmux session processes and root dir

  pg_autoctl do azure
  + create  create azure resources for a pg_auto_failover demo
  + show    show azure resources for a pg_auto_failover demo
    ls      List resources in a given azure region
    ssh     Runs ssh -l ha-admin <public ip address> for a given VM name

  pg_autoctl do azure create
    region    Create an azure region: resource group, network, VMs
    nodes     Create and provision our VM nodes in an azure region
    services  Create pg_autoctl services in a target azure region

  pg_autoctl do azure show
    ips    Show public and private IP addresses for selected VMs
    state  Connect to the monitor node to show the current state

Integration with tmux for local testing
---------------------------------------

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

Integration with azure for QA and testing
-----------------------------------------

The ``pg_autoctl do azure`` commands implement a toolkit that allows to
quickly setup a whole QA environment with several VMs running with
pg_auto_failover.

With the two following commands you can create a full Azure setup with:

  - a resource group,
  - a network vnet,
  - a network security group,
  - a specific rule which allows your own current public IP address to
    connect to ports 22 and 5432 of the Azure VMs,
  - a subnet using those security rules,
  - some number of VMs created in parallel and provisioned with the latest
    packages
  - pg_autoctl monitor and nodes initialized with ``pg_autoctl create ...``
  - pg_autoctl service registered in systemd

Just use the following commands:

.. code-block:: bash

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create region \
      --prefix ha-demo \
      --region paris \
      --location francecentral \
      --monitor \
      --nodes 2

   PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create nodes \
      --prefix ha-demo-dim \
      --region paris \
      --monitor \
      --nodes 2

You can also produce a script for later re-use (such as pasting the commands
in the documentation)::

   $ PG_AUTOCTL_DEBUG=1 pg_autoctl do azure create region \
        --prefix ha-demo-dim \
        --name paris \
        --location francecentral \
        --monitor \
        --nodes 3 \
        --script | cat -n
   11:13:59 69083 INFO  Creating group "ha-demo-dim-paris" in location "francecentral"
   11:13:59 69083 INFO  Creating network vnet "ha-demo-dim-paris-net" using address prefix "10.11.0.0/16"
   11:13:59 69083 INFO  Creating network nsg "ha-demo-dim-paris-nsg"
   11:13:59 69083 INFO  Creating network nsg rules "ha-demo-dim-paris-ssh-and-pg" for our IP address "aa.bbb.ccc.dd" for ports 22 and 5432
   11:13:59 69083 INFO  Creating network subnet "ha-demo-dim-paris-subnet" using address prefix "10.11.11.0/24"
   11:13:59 69083 INFO  Creating Virtual Machines for a monitor and 3 Postgres nodes, in parallel
   11:13:59 69083 INFO  Creating debian virtual machine "ha-demo-dim-paris-monitor" with user "ha-admin"
   11:13:59 69083 INFO  Creating debian virtual machine "ha-demo-dim-paris-a" with user "ha-admin"
   11:13:59 69083 INFO  Creating debian virtual machine "ha-demo-dim-paris-b" with user "ha-admin"
   11:13:59 69083 INFO  Creating debian virtual machine "ha-demo-dim-paris-c" with user "ha-admin"
   11:13:59 69083 INFO  Provisioning 4 Virtual Machines in parallel
   11:13:59 69083 INFO  Provisioning Virtual Machine "ha-demo-dim-paris-monitor"
   11:13:59 69083 INFO  Provisioning Virtual Machine "ha-demo-dim-paris-a"
   11:13:59 69083 INFO  Provisioning Virtual Machine "ha-demo-dim-paris-b"
   11:13:59 69083 INFO  Provisioning Virtual Machine "ha-demo-dim-paris-c"
     1  # azure commands for pg_auto_failover demo
     2   /usr/local/bin/az group create --name ha-demo-dim-paris --location francecentral
     3   /usr/local/bin/az network vnet create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-net --address-prefix 10.11.0.0/16
     4   /usr/local/bin/az network nsg create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-nsg
     5   /usr/local/bin/az network nsg rule create --resource-group ha-demo-dim-paris --nsg-name ha-demo-dim-paris-nsg --name ha-demo-dim-paris-ssh-and-pg --access allow --protocol Tcp --direction Inbound --priority 100 --source-address-prefixes aa.bbb.ccc.dd --source-port-range "*" --destination-address-prefix "*" --destination-port-ranges 22 5432
     6   /usr/local/bin/az network vnet subnet create --resource-group ha-demo-dim-paris --vnet-name ha-demo-dim-paris-net --name ha-demo-dim-paris-subnet --address-prefixes 10.11.11.0/24 --network-security-group ha-demo-dim-paris-nsg
     7   /usr/local/bin/az vm create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-monitor --vnet-name ha-demo-dim-paris-net --subnet ha-demo-dim-paris-subnet --nsg ha-demo-nsg --public-ip-address ha-demo-dim-paris-monitor-ip --image debian --admin-username ha-admin --generate-ssh-keys &
     8   /usr/local/bin/az vm create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-a --vnet-name ha-demo-dim-paris-net --subnet ha-demo-dim-paris-subnet --nsg ha-demo-nsg --public-ip-address ha-demo-dim-paris-a-ip --image debian --admin-username ha-admin --generate-ssh-keys &
     9   /usr/local/bin/az vm create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-b --vnet-name ha-demo-dim-paris-net --subnet ha-demo-dim-paris-subnet --nsg ha-demo-nsg --public-ip-address ha-demo-dim-paris-b-ip --image debian --admin-username ha-admin --generate-ssh-keys &
    10   /usr/local/bin/az vm create --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-c --vnet-name ha-demo-dim-paris-net --subnet ha-demo-dim-paris-subnet --nsg ha-demo-nsg --public-ip-address ha-demo-dim-paris-c-ip --image debian --admin-username ha-admin --generate-ssh-keys &
    11  wait
    12  /usr/local/bin/az vm run-command invoke --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-monitor --command-id RunShellScript --scripts "curl https://install.citusdata.com/community/deb.sh | sudo bash" "sudo apt-get install -q -y postgresql-common" "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" "sudo apt-get install -q -y postgresql-11-auto-failover-1.4" "sudo usermod -a -G postgres ha-admin" &
    13  /usr/local/bin/az vm run-command invoke --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-a --command-id RunShellScript --scripts "curl https://install.citusdata.com/community/deb.sh | sudo bash" "sudo apt-get install -q -y postgresql-common" "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" "sudo apt-get install -q -y postgresql-11-auto-failover-1.4" "sudo usermod -a -G postgres ha-admin" &
    14  /usr/local/bin/az vm run-command invoke --resource-group ha-demo-dim-paris --name ha-demo-dim-paris-b --command-id RunShellScript --scripts "curl https://install.citusdata.com/community/deb.sh | sudo bash" "sudo apt-get install -q -y postgresql-common" "echo 'create_main_cluster = false' | sudo tee -a /etc/postgresql-common/createcluster.conf" "sudo apt-get install -q -y postgresql-11-auto-failover-1.4" "sudo usermod -a -G postgres ha-admin" &
    15 wait
