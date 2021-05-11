.. _pg_autoctl_do_pgsetup:

pg_autoctl do pgsetup
=====================

pg_autoctl do pgsetup - Manage a local Postgres setup

Synopsis
--------

The main ``pg_autoctl`` commands implement low-level management tooling for
a local Postgres instance. Some of the low-level Postgres commands can be
used as their own tool in some cases.

pg_autoctl do pgsetup provides the following commands::

    pg_autoctl do pgsetup
      pg_ctl    Find a non-ambiguous pg_ctl program and Postgres version
      discover  Discover local PostgreSQL instance, if any
      ready     Return true is the local Postgres server is ready
      wait      Wait until the local Postgres server is ready
      logs      Outputs the Postgres startup logs
      tune      Compute and log some Postgres tuning options

pg_autoctl do pgsetup pg_ctl
----------------------------

In a similar way to ``which -a``, this commands scans your PATH for
``pg_ctl`` commands. Then it runs the ``pg_ctl --version`` command and
parses the output to determine the version of Postgres that is available in
the path.

::

   $ pg_autoctl do pgsetup pg_ctl --pgdata node1
   16:49:18 69684 INFO  Environment variable PG_CONFIG is set to "/Applications/Postgres.app//Contents/Versions/12/bin/pg_config"
   16:49:18 69684 INFO  `pg_autoctl create postgres` would use "/Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl" for Postgres 12.3
   16:49:18 69684 INFO  `pg_autoctl create monitor` would use "/Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl" for Postgres 12.3


pg_autoctl do pgsetup discover
------------------------------

Given a PGDATA or ``--pgdata`` option, the command discovers if a running
Postgres service matches the pg_autoctl setup, and prints the information
that ``pg_autoctl`` typically needs when managing a Postgres instance.

::

   $ pg_autoctl do pgsetup discover --pgdata node1
   pgdata:                /Users/dim/dev/MS/pg_auto_failover/tmux/node1
   pg_ctl:                /Applications/Postgres.app/Contents/Versions/12/bin/pg_ctl
   pg_version:            12.3
   pghost:                /tmp
   pgport:                5501
   proxyport:             0
   pid:                   21029
   is in recovery:        no
   Control Version:       1201
   Catalog Version:       201909212
   System Identifier:     6942422768095393833
   Latest checkpoint LSN: 0/4059C18
   Postmaster status:     ready


pg_autoctl do pgsetup ready
---------------------------

Similar to the `pg_isready`__ command, though uses the Postgres
specifications found in the pg_autoctl node setup.

__ https://www.postgresql.org/docs/current/app-pg-isready.html

::

   $ pg_autoctl do pgsetup ready --pgdata node1
   16:50:08 70582 INFO  Postgres status is: "ready"


pg_autoctl do pgsetup wait
--------------------------

When ``pg_autoctl do pgsetup ready`` would return false because Postgres is
not ready yet, this command continues probing every second for 30 seconds,
and exists as soon as Postgres is ready.

::

   $ pg_autoctl do pgsetup wait --pgdata node1
   16:50:22 70829 INFO  Postgres is now serving PGDATA "/Users/dim/dev/MS/pg_auto_failover/tmux/node1" on port 5501 with pid 21029
   16:50:22 70829 INFO  Postgres status is: "ready"


pg_autoctl do pgsetup logs
--------------------------

Outputs the Postgres logs from the most recent log file in the
``PGDATA/log`` directory.

::

   $ pg_autoctl do pgsetup logs --pgdata node1
   16:50:39 71126 WARN  Postgres logs from "/Users/dim/dev/MS/pg_auto_failover/tmux/node1/startup.log":
   16:50:39 71126 INFO  2021-03-22 14:43:48.911 CET [21029] LOG:  starting PostgreSQL 12.3 on x86_64-apple-darwin16.7.0, compiled by Apple LLVM version 8.1.0 (clang-802.0.42), 64-bit
   16:50:39 71126 INFO  2021-03-22 14:43:48.913 CET [21029] LOG:  listening on IPv6 address "::", port 5501
   16:50:39 71126 INFO  2021-03-22 14:43:48.913 CET [21029] LOG:  listening on IPv4 address "0.0.0.0", port 5501
   16:50:39 71126 INFO  2021-03-22 14:43:48.913 CET [21029] LOG:  listening on Unix socket "/tmp/.s.PGSQL.5501"
   16:50:39 71126 INFO  2021-03-22 14:43:48.931 CET [21029] LOG:  redirecting log output to logging collector process
   16:50:39 71126 INFO  2021-03-22 14:43:48.931 CET [21029] HINT:  Future log output will appear in directory "log".
   16:50:39 71126 WARN  Postgres logs from "/Users/dim/dev/MS/pg_auto_failover/tmux/node1/log/postgresql-2021-03-22_144348.log":
   16:50:39 71126 INFO  2021-03-22 14:43:48.937 CET [21033] LOG:  database system was shut down at 2021-03-22 14:43:46 CET
   16:50:39 71126 INFO  2021-03-22 14:43:48.937 CET [21033] LOG:  entering standby mode
   16:50:39 71126 INFO  2021-03-22 14:43:48.942 CET [21033] LOG:  consistent recovery state reached at 0/4022E88
   16:50:39 71126 INFO  2021-03-22 14:43:48.942 CET [21033] LOG:  invalid record length at 0/4022E88: wanted 24, got 0
   16:50:39 71126 INFO  2021-03-22 14:43:48.946 CET [21029] LOG:  database system is ready to accept read only connections
   16:50:39 71126 INFO  2021-03-22 14:43:49.032 CET [21038] LOG:  fetching timeline history file for timeline 4 from primary server
   16:50:39 71126 INFO  2021-03-22 14:43:49.037 CET [21038] LOG:  started streaming WAL from primary at 0/4000000 on timeline 3
   16:50:39 71126 INFO  2021-03-22 14:43:49.046 CET [21038] LOG:  replication terminated by primary server
   16:50:39 71126 INFO  2021-03-22 14:43:49.046 CET [21038] DETAIL:  End of WAL reached on timeline 3 at 0/4022E88.
   16:50:39 71126 INFO  2021-03-22 14:43:49.047 CET [21033] LOG:  new target timeline is 4
   16:50:39 71126 INFO  2021-03-22 14:43:49.049 CET [21038] LOG:  restarted WAL streaming at 0/4000000 on timeline 4
   16:50:39 71126 INFO  2021-03-22 14:43:49.210 CET [21033] LOG:  redo starts at 0/4022E88
   16:50:39 71126 INFO  2021-03-22 14:52:06.692 CET [21029] LOG:  received SIGHUP, reloading configuration files
   16:50:39 71126 INFO  2021-03-22 14:52:06.906 CET [21029] LOG:  received SIGHUP, reloading configuration files
   16:50:39 71126 FATAL 2021-03-22 15:34:24.920 CET [21038] FATAL:  terminating walreceiver due to timeout
   16:50:39 71126 INFO  2021-03-22 15:34:24.973 CET [21033] LOG:  invalid record length at 0/4059CC8: wanted 24, got 0
   16:50:39 71126 INFO  2021-03-22 15:34:25.105 CET [35801] LOG:  started streaming WAL from primary at 0/4000000 on timeline 4
   16:50:39 71126 FATAL 2021-03-22 16:12:56.918 CET [35801] FATAL:  terminating walreceiver due to timeout
   16:50:39 71126 INFO  2021-03-22 16:12:57.086 CET [38741] LOG:  started streaming WAL from primary at 0/4000000 on timeline 4
   16:50:39 71126 FATAL 2021-03-22 16:23:39.349 CET [38741] FATAL:  terminating walreceiver due to timeout
   16:50:39 71126 INFO  2021-03-22 16:23:39.497 CET [41635] LOG:  started streaming WAL from primary at 0/4000000 on timeline 4


pg_autoctl do pgsetup tune
--------------------------

Outputs the pg_autoclt automated tuning options. Depending on the number of
CPU and amount of RAM detected in the environment where it is run,
``pg_autoctl`` can adjust some very basic Postgres tuning knobs to get
started.

::

   $ pg_autoctl do pgsetup tune --pgdata node1 -vv
   13:25:25 77185 DEBUG pgtuning.c:85: Detected 12 CPUs and 16 GB total RAM on this server
   13:25:25 77185 DEBUG pgtuning.c:225: Setting autovacuum_max_workers to 3
   13:25:25 77185 DEBUG pgtuning.c:228: Setting shared_buffers to 4096 MB
   13:25:25 77185 DEBUG pgtuning.c:231: Setting work_mem to 24 MB
   13:25:25 77185 DEBUG pgtuning.c:235: Setting maintenance_work_mem to 512 MB
   13:25:25 77185 DEBUG pgtuning.c:239: Setting effective_cache_size to 12 GB
   # basic tuning computed by pg_auto_failover
   track_functions = pl
   shared_buffers = '4096 MB'
   work_mem = '24 MB'
   maintenance_work_mem = '512 MB'
   effective_cache_size = '12 GB'
   autovacuum_max_workers = 3
   autovacuum_vacuum_scale_factor = 0.08
   autovacuum_analyze_scale_factor = 0.02
