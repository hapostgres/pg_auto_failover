
.. _pg_autoctl_perform_switchover:

pg_autoctl perform switchover
=============================

pg_autoctl perform switchover - Perform a switchover for given formation and group

Synopsis
--------

This command starts a Postgres switchover orchestration from the
pg_auto_switchover monitor::

  usage: pg_autoctl perform switchover  [ --pgdata --formation --group ]

  --pgdata      path to data directory
  --formation   formation to target, defaults to 'default'
  --group       group to target, defaults to 0

Description
-----------

The pg_auto_switchover monitor can be used to orchestrate a manual switchover,
sometimes also known as a switchover. When doing so, split-brain are
prevented thanks to intermediary states being used in the Finite State
Machine.

The ``pg_autoctl perform switchover`` command waits until the switchover is
known complete on the monitor, or until the hard-coded 60s timeout has
passed.

The switchover orchestration is done in the background by the monitor, so even
if the ``pg_autoctl perform switchover`` stops on the timeout, the switchover
orchestration continues at the monitor.

See also :ref:`pg_autoctl_perform_failover`, a synonym for this command.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--formation

  Formation to target for the operation. Defaults to ``default``.

--group

  Postgres group to target for the operation. Defaults to ``0``, only Citus
  formations may have more than one group.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

PG_CONFIG

  Can be set to the absolute path to the `pg_config`__ Postgres tool. This
  is mostly used in the context of building extensions, though it can be a
  useful way to select a Postgres version when several are installed on the
  same system.

  __ https://www.postgresql.org/docs/current/app-pgconfig.html

PATH

  Used the usual way mostly. Some entries that are searched in the PATH by
  the ``pg_autoctl`` command are expected to be found only once, to avoid
  mistakes with Postgres major versions.

PGHOST, PGPORT, PGDATABASE, PGUSER, PGCONNECT_TIMEOUT, ...

  See the `Postgres docs about Environment Variables`__ for details.
  
  __ https://www.postgresql.org/docs/current/libpq-envars.html

TMPDIR

  The pgcopydb command creates all its work files and directories in
  ``${TMPDIR}/pgcopydb``, and defaults to ``/tmp/pgcopydb``.

XDG_CONFIG_HOME

  The pg_autoctl command stores its configuration files in the standard
  place XDG_CONFIG_HOME. See the `XDG Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
  
XDG_DATA_HOME

  The pg_autoctl command stores its internal states files in the standard
  place XDG_DATA_HOME, which defaults to ``~/.local/share``. See the `XDG
  Base Directory Specification`__.

  __ https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
