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
* Feature add --run option to create commmand #110
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
