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
