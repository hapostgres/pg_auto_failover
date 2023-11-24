### pg_auto_failover v2.1 (November 24, 2022) ###

This release incorporates support for Postgres major version 16, some bug fixes,
documentation updates, and usual code maintenance work.

### Added
* Support for Postgres major version 16. (#1013, #1006, )
* Improve on documentation, Docker images, and tutorials. (#964, #954, #947)
* PGDATABASE as default create node --dbname. (#956)
* Add chapters to the documentation navigation. (#954)

### Fixed
* Makefile .PHONY target is defined throughout where needed. (#1008)
* History file parsing allowing for files longer than 1024 lines. (#995)
* A mistake in setup description (#984)
* Typo in pg_autoctl_do_pgsetup.rst (#961)
* pg_autoctl version Postgres compatibility string. (#951)
* Creating the PGUSER at pg_autoctl create postgres time. (#950)
* Dockerfile compatibility for Citus before 11. (#946)
* Punctuation in README.md. (#945)

### Changed
* Main Makefile is reorganized to split out citus and azure specifics. (#1008)
* Citus version matrix with new Citus releases. (#990)
* Update Citus from 11.1.2 to 11.1.3 in the build system. (#957)

### pg_auto_failover v2.0 (October 7, 2022) ###

This new release includes support for Citus in pg_auto_failover, the usual
amount of bug fixes, and update documentation with new tutorials and
diagrams.

The source code repository branch "master" was renamed to "main". This
impacts the URLs of the docs for the not-yet released versions, and if
you're using your own local git clone you might need to change your remote
settings to follow the "main" branch now.

### Added
* Implement Citus support in pg_auto_failover. (#933)
* Improve our Citus support for modern Citus compatibility. (#939)

### Fixed
* Fix documentation of health_check_period to match code (#926)
* Bug fix: if process is stopped, refrain from signaling PID 0. (#921)
* Fix monitor extension for new Postgres 15 shared memory hooks. (#922)
* Refrain from using PGPASSWORD for pg_basebackup connection. (#768)
* Fix build on FreeBSD (sempahore union support). (#901)
* Improve tests stability around wait-until-pg-is-running. (#900)
* Avoid NULL pointer check (#898)
* Fix incorrect order of arguments in prototype (#887)
* Fix check for negative pid (#888)
* Fix potential out of bounds array access (#889)
* Fix incorrect indentation of foreach block (#884)
* docs: fix various typos (#885)
* Fix tests for a non-interactive environment. (#894)
* Fix ssl test by copying root client-side certificates. (#893)

### Changed
* Review the README, introduce a new first simple tutorial. (#942)
* Use our docker test infrastructure on GitHub Actions. (#937)
* Include Postgres 15 in our CI testing. (#923)
* Improve our docker compose file a little. (#724)
* Improve documentation thanks to user feedback. (#909)
* Trigger github action workflow on pull requests to master. (#895)

### pg_auto_failover v1.6.4 (January 22, 2022) ###

This is a bug fix release for the 1.6 series.

### Added
* Compat with Postgres 15devel. (#838, #842)
* Add support for more environment variables. (#846)
* Add support for tablespaces (#870)

### Fixed
* Handle return events for poll() (#836)
* No need to checkpoint and restart if pg is not running (#839)
* Fix a race condition in node registration. (#847)
* Couple of fixes to the demo app. (#860)
* Check return value of strdup calls (#862)

### pg_auto_failover v1.6.3 (November 5, 2021) ###

This is a bug fix release for the 1.6 series.

This release also introduces a new ncurses interactive dashboard that makes
it easier to understand the current state (and transitions) of a formation.
The new command `pg_autoctl watch` can be used to monitor pg_auto_failover
activity.

#### Added
* New ncurses dashboard with command pg_autoctl watch (#809)

#### Changed
* Allow setting maximum-backup-rate on create postgres step (#812)

#### Fixed
* Work around pg_replication_slot_advance xmin maintenance bug. (#815)
* Fix "Current State" to "Reported State", and a docs cross-ref.
* Monitor config set postgresql.pg_ctl bug fix (#818)
* Fix how we clean-up our logs semaphore. (#811)
* Fix synchronous_standby_names return value when there is no primary (#807)

### pg_auto_failover v1.6.2 (September 8, 2021) ###

This is a bug fix release for the 1.6 series.

#### Added
* Also retry libpq connections to a local host. (#793)

#### Changed
* Only exit at upgrade when the on-disk binary is ready. (#771)
* Only use wait_maintenance to wait for wait_primary (#794)
* Get rid of the JOIN_PRIMARY state. (#796)
* Make sure to disable sync rep when initializing a primary. (#801)

#### Fixed
* Avoid re-electing primary during a switchover. (#772)
* Improve error messages for missing configuration files. (#779)
* Fix replication slot maintenance on secondary nodes. (#781)
* Fix problems with bad migration to 1.5 (#792)
* Fix maintenance state related transitions. (#786)
* Per valgrind, fix some memory leaks. (#799)
* When creating from an existing PGDATA, fix missing initialization. (#802)

### pg_auto_failover v1.6.1 (July 7, 2021) ###

This release contains monitor schema changes, so the monitor extension gets
a version bump from 1.5 to 1.6, and this is the first release in the 1.6
series.

In this release we introduce a new state in the FSM: "dropped". This state
allows a node to realise it's been dropped from the monitor, and act
accordingly (mostly, stops cleanly and register it's been dropped).

This means that the command `pg_autoctl drop node` now only completes when
the node that is being dropped is still reachable. To drop a node that is
unreachable (e.g. machine died), you should now use the command `pg_autoctl
drop node --force`.

#### Added
* Feature crash recovery before pg_rewind (#656)
* Allow building pg_auto_failover with Postgres 14. (#716)
* Track nodes current timeline on the monitor and use it during election. (#730)
* Implement drop-at-a-distance semantics. (#734)
* Add the reported timeline id to the monitor events table. (#753)

#### Changed
* Fix how many nodes need to report their LSN to perform a failover. (#707)
* Allow an old primary node (demoted/catchingup) to join another election. (#727)
* Have pg_autoctl drop node command wait until the node has been removed. (#748)

#### Fixed
* Fix/consider timeline when reaching secondary (#695)
* Install btree_gist when upgrade to >= 1.4, not just 1.4. (#714)
* Fix a race condition issue around health check updates. (#720)
* Not all the monitor code got the memo about nodeid being a bigint. (#729)
* Use sysctl(3) on BSD (#733)
* Fix transaction begin failure handling (#751)
* Fix a connection leak at SIGINT. (#759)

### pg_auto_failover v1.5.2 (May 20, 2021) ###

This is a bugfix release for the v1.5 series.

In addition to bug fixes, this release also contains a lift of the
restriction to always have at least two nodes with a non-zero candidate
priority in a group. It is now possible to use pg_auto_failover and only
have manual failover.

If you're using the output from the command `pg_autoctl show settings
--json` please notice that we changed the JSON format we use in the output.
See #697 for details.

#### Added
* Check that a "replication" connection is possible before pg_rewind. [#665]
* Allow manual promotion of nodes with candidate priority zero. [#661]
* Implement a new configuration option listen_notifications_timeout. [#677]
* Log monitor health changes as events. [#703]

#### Changed
* Use PGDATA owner for the systemd service file. [#666]
* Remove logging of connection password in monitor string [#512]
* Improve docs color contrast for accessibility [#674]
* Fix pg_autoctl show settings --json output. [#697]

#### Fixed
* Docs: typo fix for Postgres client certificate file (postgresql.crt). [#652]
* Plug connection leaks found during profiling [#582]
* Review find_extension_control_file[) error handling. (#659]
* Fix/identify system before pg basebackup [#658]
* Fix a pipe issue and return code [#619]
* Fix memory leak allocated by createPQExpBuffer() (#671]
* Fix parsing pg version string for replication slots support on standby. [#676]
* Fix/debian cluster for the monitor [#681]
* Fix a memory leak in uri_contains_password. [#687]
* Fix a memory leak in BuildNodesArrayValues. [#693]
* Complete transition of a second [or greater) failed primary (#706]


### pg_auto_failover v1.5.1 (March 24, 2021) ###

This release contains monitor schema changes, so the monitor extension gets
a version bump from 1.4 to 1.5, and this is the first release in the 1.5
series.

#### Added
* Add support for systemd ExecReload option. [#623]
* Implement online enable/disable monitor support. [#591]
* Add individual pages for the pg_autoctl commands. [#632]
* Implement a demo application showing client-side reconnections. [#568]

#### Changed

The main change in the CLI is that `pg_autoctl show uri --monitor` does not
display the connection string to the monitor anymore, instead it allows
passing the URI to the monitor, same as with the other `pg_autoctl show
commands`. To display the monitor connection string, use `pg_autoctl show
uri --formation monitor` now.

* Allow using --monitor uri for a lot of commands [#576]
* Review pg_autoctl show state output, and docs. [#617]
* Avoid using synchronous standby name wildcard [#629]

#### Fixed
* Fix supervisor messages about exited services. [#589]
* Fix memory management issue in monitor_register_node. [#590]
* Fix a buffer overlap instruction that macOs libc fails to process. [#610]
* Add pg_logging_init for PG version 12 and above [#612]
* Fix skip hba propagation [#588, #609]
* Improve DNS lookup error handling. [#615]
* Do not leak psycopg2 connections during testing [#628]

### pg_auto_failover v1.4.2 (February 3, 2021) ###

This is a bugfix release for v1.4 series

#### Added
* Implement pg_autoctl do azure commands (QA tooling). [#544]
* pg autoctl show settings. [#549]
* Improve docker images (build, release). [#556]
* Run monitor extension test suite in the CI. [#553]
* Implement pg_autoctl create postgres --pg-hba-lan option. [#561]

#### Fixed
* Deduplicate PATH entries, following symlinks. [#547]
* Review consequences of pg_autoctl set formation number-sync-standbys 0. [#535]
* Fix bytes mangling opportunity. [#550]
* Allow setting replication settings to their current value. [#570]
* Fix the return code when stop the node that is started by pg_autoctl. [#572]
* Set formation kind when expected. [#577]
* Fix retry loops with wait time to stop using 100% CPU. [#578]

### pg_auto_failover v1.4.1 (December 3, 2020) ###

This is a bugfix release for v1.4.0

#### Added
* Implement HBA hostname DNS checks, and warn users when needed. [#458]
* Make it obvious when an extension is missing. [#475]

#### Changed
* Refrain from using FIRST %d in synchronous_standby_names. [#461]
* Always use node id and node name in messages. [#462]

#### Fixed
* Force closing the connection used to check replication slots. [#451]
* Fix when to ProceedWithMSFailover after a candidate is selected. [#439]
* Ensure that the monitor owns the Postgres (sub-)process. [#455]
* Avoid a race condition between file_exists() and read_file(). [#460]
* Review memory/malloc errors and potential leaks. [#478]
* Review APPLY_SETTINGS transitions in cases including node removal. [#480]
* Fix when we switch synchronous_standby_names to '*'. [#488]
* Fix hostname discovery [#479]
* Fix non default formation [#489]
* Review when to assign catching-up on unhealthy secondary. [#493]
* Fix race conditions in multiple-standby Group FSM for failover. [#499]
* Fix synchronous_standby_names when disabling maintenance. [#502]
* Fix debian stats temp directory [#504]
* Use PG_CONFIG has a hint to choose one of multiple Postgres versions. [#510]
* Fix build for *BSD [#519]
* Refrain from considering a WAIT_STANDBY node unhealthy. [#524]
* Allow a DEMOTED primary to come back to a new wait/join/primary. [#524]
* Fix pg_autoctl show standby-names. [#524]
* Fix unhealthy nodes processing when wait_primary. [#521]
* Fix FSM when a non-candidate node is back online. [#533]
* Fix assigning catchingup to unhealthy secondaries. [#534]
* Fix pg_autoctl set formation number-sync-standbys log output. [#536]

### pg_auto_failover v1.4.0 (September 23, 2020) ###

The main focus of this release is the new capability of pg_auto_failover to
manage any number of standby nodes, where it used to manage only a single
secondary before. This comes with new documentation coverage and Postgres
production architectures support.

The main changes of this release are:

  - A Postgres group allows any number of Postgres nodes, any of them can be
    setup to participate in the replication quorum (it's then a sync
    standby) or not (it's then an async standby).

  - Any node can be setup with a candidate priority of zero, and then a
    failover will never pick this node as the new primary. Two nodes with
    non-zero candidate priority are expected in any group, and the monitor
    enforces that.

  - The --nodename option is removed, replaced by the new option --hostname.

	At upgrade time your configuration file is migrated to the new format
    automatically.

    Adding to that, it is now possible to give names to your nodes. This
    change breaks scripts that you may have: replace --nodename with
    --hostname, add --name if needed.

    It is also possible to edit a node's metadata at runtime or when
    starting a node. The command `pg_autoctl run` now supports options
    `--name`, `--hostname`, and `--pgport`. That's useful in environments
    where IP addresses must be used and can change at each reboot.

  - The `pg_autoctl` process is now the parent process of Postgres.

    As of `pg_autoctl` 1.4 Postgres is running only when `pg_autoctl` is
    running. This simplifies systemd integration and allows a better upgrade
    process. It also makes `pg_autoctl` easier to run in containers.

  - It is possible to `pg_autoctl create postgres` when PGDATA is an already
    existing data_directory: as usual the monitor decides if that node is
    going to be a primary or a standby. All the Postgres nodes in the same
    group must have the same Postgres system identifier, and that is now
    enforced on the monitor.

#### Added
* Allow multiple standbys in a Postgres group [#326]
* Allow adding a standby node from an exiting PGDATA directory [#276]
* Edit the HBA file as soon as a new node is added to the group [#311]
* Check for monitor extension version every time we connect [#318]
* Command pg_autoctl perform failover now waits until failover is done [#290]
* Implement pg_autoctl enable maintenance --allow-failover [#306]
* Implement a node name [#340]
* Test primary_conninfo before starting Postgres standby [#350]
* Compute some memory/cpu Postgres tuning for the host [#335]
* Provide a better pg_auto_failover upgrade experience [#296]
* Add support for building against Postgres 13 [#312]
* Allow registering multiple standbys concurrently [#395]
* Implement a retry policy for deadlocks and other transient failures [#359]
* Implement support for interactive tmux sessions [#409]
* Use monitor notifications to wake up from sleep in the main keeper loop [#387]

#### Changed
* Make pg_autoctl the parent process for Postgres [#265, #267, #284]
* Rename --nodename to --hostname, and nodename to nodehost [#273]
* Improve output for many pg_autoctl commands, including JSON output
* Log and notify node health changes [#334]
* Set default password encryption based on --auth [#383]

#### Fixed
* Fix pg_autoctl perform failover/switchover default --group [#289]
* Fix pgautofailover extension upgrade script from 1.2 to 1.3 [#288]
* Improve connection retry attempts [#299, #302]
* Skip network DNS probes on pg_autoctl create monitor --skip-pg-hba [#298]
* Fix pg_autoctl perform failover [#294, #307]
* Do not always require --ssl-ca-file for custom SSL [#303]
* Review the registering process & transaction [#309]
* Fix usage of replication slots to avoid a Postgres bug [#321]
* Fix fatal auth pgautofailover monitor [#361]

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
