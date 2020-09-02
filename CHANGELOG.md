### pg_auto_failover v1.4.0 (September 2, 2020) ###

* Make pg_autoctl the parent process for Postgres [#265, #267, #284]
* Allow adding a standby node from an exiting PGDATA directory [#276]
* Improve output for many pg_autoctl commands, including JSON output
* Fix pg_autoctl perform failover/switchover default --group [#289]
* Fix pgautofailover extension upgrade script from 1.2 to 1.3 [#288]
* Command pg_autoctl perform failover now waits until failover is done [#290]
* Improve connection retry attempts [#299, #302]
* Skip network DNS probes on pg_autoctl create monitor --skip-pg-hba [#298]
* Fix pg_autoctl perform failover [#294, #307]
* Implement pg_autoctl enable maintenance --allow-failover [#306]
* Do not always require --ssl-ca-file for custom SSL [#303]
* Review the registering process & transaction [#309]
* Edit the HBA file as soon as a new node is added to the group [#311]
* Check for monitor extension version every time we connect [#318]
* Fix usage of replication slots to avoid a Postgres bug [#321]
* Log and notify node health changes [#334]
* Rename --nodename to --hostname, and nodename to nodehost [#273]
* Implement a node name [#340]
* Test primary_conninfo before starting Postgres standby [#350]
* Allow multiple standbys in a Postgres group [#326]
* Compute some memory/cpu Postgres tuning for the host [335]
* Fix fatal auth pgautofailover monitor [#361]
* Provide a better pg_auto_failover upgrade experience [#296]
* Add support for building against Postgres 13 [#312]
* Set default password encryption based on --auth [#383]

### pg_auto_failover v1.3.1 (May 7, 2020) ###

* Fix build system for packaging extension files [#258]

### pg_auto_failover v1.3 (April 30, 2020) ###

* Fix systemd integration [#212]
* Change default TLS cipher list [#214]
* SSL certificates management fixes [#228]
* Improve replication slots handling [#222]
* Implement pg_autoctl enable|disable ssl [#233]
* Implement pg_autoctl show uri --monitor [#238]
* Implement pg_autoctl stop|reload for the monitor [#241]
* Don't create pgautofailover_monitor user anymore [#247]

### pg_auto_failover v1.2 (March 17, 2020) ###

* Feature implement an option (--skip-pg-hba) to skip editing of HBA rules by pg_autoctl (#169)
* Feature implement support for SSL connections (--ssl-mode)  (#171)
* Feature implement pg_autoctl drop monitor and drop node --nodename --nodeport (#179)
* Feature implement SSL support with self-signed certificates
* Feature make --auth option mandatory when creating nodes
* Fix error out when the pgautofailover is not in shared_preload_libraries
* Fixes for warnings found in static analysis

### pg_auto_failover v1.0.6 (Feb 13, 2020) ###

* Fix permissions missing in monitor database #94 via $141
* Fix creating a secondary server in an already existing directory. (#96)
* Fix unable to get --pgdata value in pg_autoctl get config command #99
* Fix registering a pre-existing Postgres cluster to the monitor #111
* Fix demoted primary cannot catchup, wrong working directory? #129
* Fix review main loop chatter, make it less verbose by default. (#97)
* Fix refrain from using PGDATA as the systemd service WorkingDirectory. (#123)
* Fix behaviour with stale postmaster pid (#152)
* Feature add perform destroy command, and -destroy option to drop node command #141
* Feature support debian/ubuntu style PostgreSQLclusters #135
* Feature add files option to show command
* Feature add --run option to create command #110
* Feature add --json option to all commands for json output #106
* Feature report current LSN of Postgres nodes rather than WAL lag. (#53)

### pg_auto_failover v1.0.5 (Sep 20, 2019) ###

* Fix not being able to create monitor/postgres nodes on mac (#60)
* Add man page entry (#61)

### pg_auto_failover v1.0.4 (Sep 5, 2019) ###

* Add PG 12 support

### pg_auto_failover v1.0.3 (Jul 30, 2019) ###

* Add support for systemd integration
* Allow pg_auto_failover extension upgrade
* Add enable/disable maintenance command in CLI
* Add --auth option to configure authentication method
* Fix crash when ip address can not be resolved in network interface (#40)
* Fix replication slot being left open after a failover (#42)
* Other minor fixes

### pg_auto_failover v1.0.2 (May 23, 2019) ###

* Implement a default value for --nodename (#6, #16)
* Code cleanup

### pg_auto_failover v1.0.1 (May 6, 2019) ###

* Fix a problem where the Postgres service was not restarted when shutdown (#2)
* Clarify name in background workers for the monitor (#3)
* Show full version number in `pg_autoctl version` (#4)
* Warn the user when the primary node is not running pg_autoctl as a service while initializing the secondary (#11)
* Improve documentation (#10)

### pg_auto_failover v1.0.0 (May 6, 2019) ###

* First release.
