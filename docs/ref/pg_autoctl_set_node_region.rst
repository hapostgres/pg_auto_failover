.. _pg_autoctl_set_node_region:

pg_autoctl set node region
===========================

pg_autoctl set region - set region property on the monitor

Synopsis
--------

This command sets the ``pg_autoctl`` region label for a given node::

  usage: pg_autoctl set node region  [ --pgdata ] [ --json ] [ --formation ] [ --name ] <region>

  --pgdata      path to data directory
  --formation   pg_auto_failover formation
  --name        pg_auto_failover node name
  --json        output data in the JSON format

Description
-----------

Sets the data-centre or availability-zone label for a node that is already
registered on the monitor. Unlike :ref:`pg_autoctl_set_node_candidate_priority`
and :ref:`pg_autoctl_set_node_replication_quorum`, this never triggers a
failover or a replication settings change on the primary: region is purely
informational, displayed by :ref:`pg_autoctl_watch` in the verbose and
higher policies. The command takes effect immediately, with no wait.

This is also the mechanism ``pg_autoctl node run`` uses to apply a live edit
of the ``region`` property in a ``pg_autoctl_node.ini`` file — see
:ref:`pg_autoctl_node` for the full ``[settings]`` reference.

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

--name

  Show replication settings for given node, selected by name.

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

   $ pg_autoctl set node region --name node1 dc2
   dc2

   $ pg_autoctl set node region --name node1 dc2 --json
   {
       "region": "dc2"
   }
