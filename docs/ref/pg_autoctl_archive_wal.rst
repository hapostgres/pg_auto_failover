.. _pg_autoctl_archive_wal:

pg_autoctl archive wal
======================

pg_autoctl archive wal - Archive a WAL file

Synopsis
--------

This command is a wrapper around the archiver method selected. At the moment
the only archiver method supported by pg_auto_failover is wal-g, so this
command is a wrapper around ``wal-g wal-push``.

It is possible to use ``pg_autoctl archive wal`` either in a *standalone*
way, mostly to verify your WAL-G configuration file; or to use the
``pg_autocl archive wal`` command as the Postgres ``archive_command``.

When a Postgres node is created with pg_auto_failover, the archive command
is always set to ``pg_autoctl archive wal %f``.

::

   usage: pg_autoctl archive wal  [ --pgdata ] [ --config ] [ --json ] filename

  --pgdata      path to data directory
  --config      archive command configuration

When the ``--config`` option is used, the pg_auto_failover monitor is then
bypassed, and the given WAL file is archived directly.

When the ``--config`` option is omitted (as in the ``archive_command``
integration with Postgres), then ``pg_autoctl archive wal`` first contacts
the pg_auto_failover monitor to get a list of archiver policies.

For each policy registered for the formation of the current node, the WAL
file is registered on the monitor, and if no other node is currently
archiving this file already, then the command proceeds to call WAL-G, and
then registers that the WAL file has been successfully archived. This allows
using ``archive_mode = 'always'`` in the Postgres configuration.

See :ref:`pg_autoctl_create_archiver-policy` for creating an archiver policy
that allows the ``archive_command`` to feed your archive target.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--config

  Pathname to the archiver method configuration files, expected to be in the
  JSON format.

Examples
--------

When used with the ``--config`` option, we can test that the configuration
file works as intended:

::

   $ pg_autoctl archive wal --pgdata node1 --config wal-g.json 000000010000000000000001
   17:08:03 23992 INFO  Archiving WAL file "000000010000000000000001" with a local archiving configuration, skipping WAL registration on the monitor
   17:08:03 23992 INFO   /Users/dim/dev/go/bin/wal-g wal-push --config /Users/dim/dev/temp/wal-g-simple.json /Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_wal/000000010000000000000001
   17:08:03 23992 INFO  wal-g: INFO: 2021/10/13 17:08:03.879025 FILE PATH: 000000010000000000000001.lz4
   17:08:03 23992 INFO  Archived WAL file "000000010000000000000001" successfully


Now, we can also create a policy as in
:ref:`pg_autoctl_create_archiver-policy` and then we would find the
following output in our Postgres logs:

::

   17:20:45 35759 INFO  Archiving WAL file "000000010000000000000001" for node 1 "node1" in formation "default" and group 0 for target "minio"
   17:20:45 35759 INFO   /Users/dim/dev/go/bin/wal-g wal-push --config /Users/dim/dev/MS/pg_auto_failover/tmux/run/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node1/wal-g.json /Users/dim/dev/MS/pg_auto_failover/tmux/node1/pg_wal/000000010000000000000001
   17:20:45 35759 INFO  wal-g: INFO: 2021/10/13 17:20:45.421710 FILE PATH: 000000010000000000000001.lz4
   17:20:45 35759 INFO  Archived WAL file "000000010000000000000001" successfully at 2021-10-13 17:20:45.490938+02 for target "minio"

We can also try and archive a WAL file manually:

::

   $ pg_autoctl archive wal --pgdata node1  000000010000000000000001
   17:22:46 37457 INFO  Archiving WAL file "000000010000000000000001" for node 1 "node1" in formation "default" and group 0 for target "minio"
   17:22:46 37457 INFO  WAL file "000000010000000000000001" with MD5 "32f274aca6096cb3db348c6018d276eb" was finished achiving for target "minio" at 2021-10-13 17:20:45.490938+02

We can see that the WAL file was already registered on the monitor, so it
was skipped rather than archived again. This allows every Postgres node in a
pg_auto_failover group to archive WAL files.

On the monitor, we see:

::

   $ psql -X  -d $(pg_autoctl show uri --formation monitor) -c '\x auto' -c 'table pgautofailover.pg_wal'
   Expanded display is used automatically.
   -[ RECORD 1 ]------+-----------------------------------------
   archiver_policy_id | 1
   groupid            | 0
   nodeid             | 1
   filename           | 000000010000000000000001
   filesize           | 16777216
   md5                | 32f274ac-a609-6cb3-db34-8c6018d276eb
   start_time         | 2021-10-13 17:20:45.119978+02
   finish_time        | 2021-10-13 17:20:45.490938+02
   -[ RECORD 2 ]------+-----------------------------------------
   archiver_policy_id | 1
   groupid            | 0
   nodeid             | 1
   filename           | 000000010000000000000002
   filesize           | 16777216
   md5                | dd4dda08-c135-8f54-5c5f-54d805b7df2d
   start_time         | 2021-10-13 17:20:52.853974+02
   finish_time        | 2021-10-13 17:20:53.082426+02
   -[ RECORD 3 ]------+-----------------------------------------
   archiver_policy_id | 1
   groupid            | 0
   nodeid             | 1
   filename           | 000000010000000000000002.00000028.backup
   filesize           | 339
   md5                | fa365ad4-1bd0-b709-e8a5-ba8d70909986
   start_time         | 2021-10-13 17:20:53.275864+02
   finish_time        | 2021-10-13 17:20:53.41548+02
