.. _pg_autoctl_show_synchronized_standby_slots:

pg_autoctl show synchronized-standby-slots
===========================================

pg_autoctl show synchronized-standby-slots - Prints synchronized_standby_slots for a given group (Postgres 17+ CDC continuity)

Synopsis
--------

This command prints the current value for the Postgres 17+
``synchronized_standby_slots`` setting for the primary Postgres server of
the target group (default ``0``) in the target formation (default
``default``), as computed by the monitor: the comma-separated physical
replication slot names of the group's current failover candidates. See
:ref:`cdc_continuity` for the full picture of what this setting is for::

  usage: pg_autoctl show synchronized-standby-slots  [ --pgdata ] --formation --group

    --pgdata      path to data directory
    --monitor     pg_auto_failover Monitor Postgres URL
    --formation   formation to query, defaults to 'default'
    --group       group to query formation, defaults to all
    --json        output data in the JSON format

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

  Defaults to the value of the environment variable ``PG_AUTOCTL_MONITOR``.

--formation

  Show the current ``synchronized_standby_slots`` value for the given
  formation. Defaults to the ``default`` formation.

--group

  Show the current ``synchronized_standby_slots`` value for the given group
  in the given formation. Defaults to group ``0``.

--json

  Output a JSON formatted data instead of a table formatted list.

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

   $ pg_autoctl show synchronized-standby-slots
   'pgautofailover_standby_2,pgautofailover_standby_3'

   $ pg_autoctl show synchronized-standby-slots --json
   {
       "formation": "default",
       "group": 0,
       "synchronized_standby_slots": "pgautofailover_standby_2,pgautofailover_standby_3"
   }
