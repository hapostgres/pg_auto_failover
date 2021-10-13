.. _pg_autoctl_restore_wal:

pg_autoctl restore wal
======================

pg_autoctl restore wal - Restore a WAL file

Synopsis
--------

This command is a wrapper around the archiver method selected. At the moment
the only archiver method supported by pg_auto_failover is wal-g, so this
command is a wrapper around ``wal-g wal-fetch``.

It is possible to use ``pg_autoctl restore wal`` either in a *standalone*
way, mostly to verify your WAL-G configuration file; or to use the
``pg_autocl restore wal`` command as the Postgres ``restore_command``.

When a Postgres node is created with pg_auto_failover, the archive command
is always set to ``pg_autoctl restore wal %f %p``.

::

   usage: pg_autoctl restore wal  [ --pgdata ] [ --config ] [ --json ] filename

  --pgdata      path to data directory
  --config      restore command configuration

When the ``--config`` option is used, the pg_auto_failover monitor is then
bypassed, and the given WAL file is restored directly.

When the ``--config`` option is omitted (as in the ``restore_command``
integration with Postgres), then ``pg_autoctl restore wal`` first contacts
the pg_auto_failover monitor to get a list of archiver policies.

For each policy registered for the formation of the current node, the WAL
file is retrieved using the archiver policy configuration. If the WAL file
to restore is found on the monitor, then the MD5 of the registered WAL file
on the monitor and of the just retrieved file are compared and must match.

As soon as a policy allows to restore the needed WAL file, the command
stops.

See :ref:`pg_autoctl_create_archiver-policy` for creating an archiver policy
that allows the ``restore_command`` to feed from your archive target.

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

   $ pg_autoctl restore wal --pgdata node2 --config ~/dev/temp/wal-g-simple.json 000000010000000000000002 /tmp/pgaf/000000010000000000000002
   17:41:05 52546 INFO  Restoring WAL file "000000010000000000000002"
   17:41:06 52546 INFO   /Users/dim/dev/go/bin/wal-g wal-fetch --config /Users/dim/dev/temp/wal-g-simple.json 000000010000000000000002 /tmp/pgaf/000000010000000000000002
   17:41:06 52546 INFO  Restored WAL file "000000010000000000000002" successfully

When used as a ``restore_command`` with an archiver-policy defined as in
:ref:`pg_autoctl_create_archiver-policy` the output of the ``pg_autoctl
restore wal`` command can be seen in the Postgres logs directly:

::

   2021-10-13 17:20:54.329 CEST [35846] LOG:  entering standby mode
   17:20:54 35868 INFO  Restoring WAL file "000000010000000000000002"
   17:20:54 35868 INFO   /Users/dim/dev/go/bin/wal-g wal-fetch --config /Users/dim/dev/MS/pg_auto_failover/tmux/run/pg_autoctl/Users/dim/dev/MS/pg_auto_failover/tmux/node2/wal-g.json 000000010000000000000002 pg_wal/RECOVERYXLOG
   17:20:54 35868 INFO  Restored WAL file "000000010000000000000002" successfully
   2021-10-13 17:20:54.713 CEST [35846] LOG:  restored log file "000000010000000000000002" from archive
