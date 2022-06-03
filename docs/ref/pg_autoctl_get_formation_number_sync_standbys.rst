.. _pg_autoctl_get_formation_number_sync_standbys:

pg_autoctl get formation number-sync-standbys
=============================================

pg_autoctl get formation number-sync-standbys - get number_sync_standbys for a formation from the monitor

Synopsis
--------

This command prints a ``pg_autoctl`` replication settings for number sync
standbys::

  usage: pg_autoctl get formation number-sync-standbys  [ --pgdata ] [ --json ] [ --formation ]

  --pgdata      path to data directory
  --json        output data in the JSON format
  --formation   pg_auto_failover formation

Description
-----------

See also :ref:`pg_autoctl_show_settings` for the full list of replication
settings.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--json

  Output JSON formatted data.

--formation

  Show replication settings for given formation. Defaults to ``default``.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

  
Examples
--------

::

   $ pg_autoctl get formation number-sync-standbys
   1

   $ pg_autoctl get formation number-sync-standbys --json
   {
       "number-sync-standbys": 1
   }
