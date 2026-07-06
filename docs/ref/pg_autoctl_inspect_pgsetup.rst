.. _pg_autoctl_inspect_pgsetup:

pg_autoctl inspect pgsetup
==========================

pg_autoctl inspect pgsetup - Inspect the local Postgres setup

Synopsis
--------

``pg_autoctl inspect pgsetup`` provides low-level management tooling for a
local Postgres instance. These commands are always available without
``PG_AUTOCTL_DEBUG``.

::

    pg_autoctl inspect pgsetup
      pg_ctl    Find a non-ambiguous pg_ctl program and Postgres version
      discover  Discover local PostgreSQL instance, if any
      ready     Return true if the local Postgres server is ready
      wait      Wait until the local Postgres server is ready
      logs      Outputs the Postgres startup logs
      tune      Compute and log some Postgres tuning options

pg_autoctl inspect pgsetup pg_ctl
---------------------------------

In a similar way to ``which -a``, this command scans your PATH for
``pg_ctl`` commands. Then it runs ``pg_ctl --version`` and parses the output
to determine the version of Postgres available.

::

   $ pg_autoctl inspect pgsetup pg_ctl --pgdata node1
   16:49:18 69684 INFO  `pg_autoctl create postgres` would use "/usr/lib/postgresql/17/bin/pg_ctl" for Postgres 17


pg_autoctl inspect pgsetup discover
------------------------------------

Given a PGDATA or ``--pgdata`` option, the command discovers whether a
running Postgres service matches the pg_autoctl setup and prints the
information that ``pg_autoctl`` typically needs.

::

   $ pg_autoctl inspect pgsetup discover --pgdata node1
   pgdata:                /home/postgres/node1
   pg_ctl:                /usr/lib/postgresql/17/bin/pg_ctl
   pg_version:            17
   pghost:                /tmp
   pgport:                5501
   pid:                   21029
   is in recovery:        no
   Postmaster status:     ready


pg_autoctl inspect pgsetup ready
---------------------------------

Similar to `pg_isready`__, but uses the Postgres specifications found in the
pg_autoctl node setup.

__ https://www.postgresql.org/docs/current/app-pg-isready.html

::

   $ pg_autoctl inspect pgsetup ready --pgdata node1
   16:50:08 70582 INFO  Postgres status is: "ready"


pg_autoctl inspect pgsetup wait
--------------------------------

When ``pg_autoctl inspect pgsetup ready`` would return false because Postgres
is not ready yet, this command probes every second for up to 30 seconds and
exits as soon as Postgres is ready.

::

   $ pg_autoctl inspect pgsetup wait --pgdata node1
   16:50:22 70829 INFO  Postgres is now serving PGDATA "/home/postgres/node1" on port 5501 with pid 21029
   16:50:22 70829 INFO  Postgres status is: "ready"


pg_autoctl inspect pgsetup logs
--------------------------------

Outputs the Postgres logs from the most recent log file in the
``PGDATA/log`` directory.

::

   $ pg_autoctl inspect pgsetup logs --pgdata node1
   16:50:39 71126 WARN  Postgres logs from "/home/postgres/node1/startup.log":
   ...


pg_autoctl inspect pgsetup tune
--------------------------------

Outputs the pg_autoctl automated tuning options. Depending on the number of
CPUs and amount of RAM detected, ``pg_autoctl`` adjusts basic Postgres tuning
knobs.

::

   $ pg_autoctl inspect pgsetup tune --pgdata node1 -vv
   13:25:25 77185 DEBUG Detected 12 CPUs and 16 GB total RAM on this server
   13:25:25 77185 DEBUG Setting shared_buffers to 4096 MB
   # basic tuning computed by pg_auto_failover
   shared_buffers = '4096 MB'
   work_mem = '24 MB'
   maintenance_work_mem = '512 MB'
   effective_cache_size = '12 GB'
   autovacuum_max_workers = 3
