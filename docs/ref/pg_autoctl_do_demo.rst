.. _pg_autoctl_do_demo:

pg_autoctl do demo
==================

pg_autoctl do demo - Use a demo application for pg_auto_failover

Synopsis
--------

pg_autoctl do demo provides the following commands::

   pg_autoctl do demo
    run      Run the pg_auto_failover demo application
    uri      Grab the application connection string from the monitor
    ping     Attempt to connect to the application URI
    summary  Display a summary of the previous demo app run

To run a demo, use ``pg_autoctl do demo run``::

  usage: pg_autoctl do demo run [option ...]

  --monitor        Postgres URI of the pg_auto_failover monitor
  --formation      Formation to use (default)
  --group          Group Id to failover (0)
  --username       PostgreSQL's username
  --clients        How many client processes to use (1)
  --duration       Duration of the demo app, in seconds (30)
  --first-failover Timing of the first failover (10)
  --failover-freq  Seconds between subsequent failovers (45)

Description
-----------

The ``pg_autoctl`` debug tooling includes a demo application.

The demo prepare its Postgres schema on the target database, and then starts
several clients (see ``--clients``) that concurrently connect to the target
application URI and record the time it took to establish the Postgres
connection to the current read-write node, with information about the retry
policy metrics.

Example
-------

::

   $ pg_autoctl do demo run --monitor 'postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer' --clients 10
   14:43:35 19660 INFO  Using application connection string "postgres://localhost:5502,localhost:5503,localhost:5501/demo?target_session_attrs=read-write&sslmode=prefer"
   14:43:35 19660 INFO  Using Postgres user PGUSER "dim"
   14:43:35 19660 INFO  Preparing demo schema: drop schema if exists demo cascade
   14:43:35 19660 WARN  NOTICE:  schema "demo" does not exist, skipping
   14:43:35 19660 INFO  Preparing demo schema: create schema demo
   14:43:35 19660 INFO  Preparing demo schema: create table demo.tracking(ts timestamptz default now(), client integer, loop integer, retries integer, us bigint, recovery bool)
   14:43:36 19660 INFO  Preparing demo schema: create table demo.client(client integer, pid integer, retry_sleep_ms integer, retry_cap_ms integer, failover_count integer)
   14:43:36 19660 INFO  Starting 10 concurrent clients as sub-processes
   14:43:36 19675 INFO  Failover client is started, will failover in 10s and every 45s after that
   ...

   $ pg_autoctl do demo summary --monitor 'postgres://autoctl_node@localhost:5500/pg_auto_failover?sslmode=prefer' --clients 10
   14:44:27 22789 INFO  Using application connection string "postgres://localhost:5503,localhost:5501,localhost:5502/demo?target_session_attrs=read-write&sslmode=prefer"
   14:44:27 22789 INFO  Using Postgres user PGUSER "dim"
   14:44:27 22789 INFO  Summary for the demo app running with 10 clients for 30s
           Client        | Connections | Retries | Min Connect Time (ms) |   max    |   p95   |   p99
   ----------------------+-------------+---------+-----------------------+----------+---------+---------
    Client 1             |         136 |      14 |                58.318 | 2601.165 | 244.443 | 261.809
    Client 2             |         136 |       5 |                55.199 | 2514.968 | 242.362 | 259.282
    Client 3             |         134 |       6 |                55.815 | 2974.247 | 241.740 | 262.908
    Client 4             |         135 |       7 |                56.542 | 2970.922 | 238.995 | 251.177
    Client 5             |         136 |       8 |                58.339 | 2758.106 | 238.720 | 252.439
    Client 6             |         134 |       9 |                58.679 | 2813.653 | 244.696 | 254.674
    Client 7             |         134 |      11 |                58.737 | 2795.974 | 243.202 | 253.745
    Client 8             |         136 |      12 |                52.109 | 2354.952 | 242.664 | 254.233
    Client 9             |         137 |      19 |                59.735 | 2628.496 | 235.668 | 253.582
    Client 10            |         133 |       6 |                57.994 | 3060.489 | 242.156 | 256.085
    All Clients Combined |        1351 |      97 |                52.109 | 3060.489 | 241.848 | 258.450
   (11 rows)

    Min Connect Time (ms) |   max    | freq |                      bar
   -----------------------+----------+------+-----------------------------------------------
                   52.109 |  219.105 | 1093 | ▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒
                  219.515 |  267.168 |  248 | ▒▒▒▒▒▒▒▒▒▒
                 2354.952 | 2354.952 |    1 |
                 2514.968 | 2514.968 |    1 |
                 2601.165 | 2628.496 |    2 |
                 2758.106 | 2813.653 |    3 |
                 2970.922 | 2974.247 |    2 |
                 3060.489 | 3060.489 |    1 |
   (8 rows)
