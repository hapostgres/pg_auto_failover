.. _pg_autoctl_status:

pg_autoctl status
=================

pg_autoctl status - Display the current status of the pg_autoctl service

Synopsis
--------

This commands outputs the current process status for the ``pg_autoctl``
service running for the given ``--pgdata`` location.

::

  usage: pg_autoctl status  [ --pgdata ] [ --json ]

  --pgdata      path to data directory
  --json        output data in the JSON format

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output a JSON formated data instead of a table formatted list.

Example
-------

::

   $ pg_autoctl status --pgdata node1
   11:26:30 27248 INFO  pg_autoctl is running with pid 26618
   11:26:30 27248 INFO  Postgres is serving PGDATA "/Users/dim/dev/MS/pg_auto_failover/tmux/node1" on port 5501 with pid 26725

   $ pg_autoctl status --pgdata node1 --json
   11:26:37 27385 INFO  pg_autoctl is running with pid 26618
   11:26:37 27385 INFO  Postgres is serving PGDATA "/Users/dim/dev/MS/pg_auto_failover/tmux/node1" on port 5501 with pid 26725
   {
       "postgres": {
           "pgdata": "\/Users\/dim\/dev\/MS\/pg_auto_failover\/tmux\/node1",
           "pg_ctl": "\/Applications\/Postgres.app\/Contents\/Versions\/12\/bin\/pg_ctl",
           "version": "12.3",
           "host": "\/tmp",
           "port": 5501,
           "proxyport": 0,
           "pid": 26725,
           "in_recovery": false,
           "control": {
               "version": 0,
               "catalog_version": 0,
               "system_identifier": "0"
           },
           "postmaster": {
               "status": "ready"
           }
       },
       "pg_autoctl": {
           "pid": 26618,
           "status": "running",
           "pgdata": "\/Users\/dim\/dev\/MS\/pg_auto_failover\/tmux\/node1",
           "version": "1.5.0",
           "semId": 196609,
           "services": [
               {
                   "name": "postgres",
                   "pid": 26625,
                   "status": "running",
                   "version": "1.5.0",
                   "pgautofailover": "1.5.0.1"
               },
               {
                   "name": "node-active",
                   "pid": 26626,
                   "status": "running",
                   "version": "1.5.0",
                   "pgautofailover": "1.5.0.1"
               }
           ]
       }
   }
