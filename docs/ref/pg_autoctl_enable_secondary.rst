.. _pg_autoctl_enable_secondary:

pg_autoctl enable secondary
===========================

pg_autoctl enable secondary - Enable secondary nodes on a formation

Synopsis
--------

This feature makes the most sense when using the Enterprise Edition of
pg_auto_failover, which is fully compatible with Citus formations. When
``secondary`` are enabled, then Citus workers creation policy is to assign a
primary node then a standby node for each group. When ``secondary`` is
disabled the Citus workers creation policy is to assign only the primary
nodes.

::

   usage: pg_autoctl enable secondary  [ --pgdata --formation ]

  --pgdata      path to data directory
  --formation   Formation to enable secondary on


Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--formation

  Target formation where to enable secondary feature.

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

  
