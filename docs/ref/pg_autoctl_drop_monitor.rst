.. _pg_autoctl_drop_monitor:

pg_autoctl drop monitor
=======================

pg_autoctl drop monitor - Drop the pg_auto_failover monitor

Synopsis
--------

This command allows to review all the replication settings of a given
formation (defaults to `'default'` as usual)::

  usage: pg_autoctl drop monitor [ --pgdata --destroy ]

  --pgdata      path to data directory
  --destroy     also destroy Postgres database

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--destroy

  By default the ``pg_autoctl drop monitor`` commands does not remove the
  Postgres database for the monitor. When using ``--destroy``, the Postgres
  installation is also deleted.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

  
