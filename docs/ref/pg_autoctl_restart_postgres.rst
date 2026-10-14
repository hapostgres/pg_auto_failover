.. _pg_autoctl_restart_postgres:

pg_autoctl restart postgres
============================

pg_autoctl restart postgres - Restart Postgres on this node, going
through pg_autoctl's own supervised process

Synopsis
--------

Some Postgres changes -- a ``PGC_POSTMASTER`` setting such as
``wal_level``, or an on-disk property such as data checksums on versions
before Postgres 18 -- can only take effect after a full restart. This
command performs that restart through pg_autoctl's own supervised
Postgres process, the same careful way some FSM transitions already
restart Postgres before a planned shutdown: a couple of ``CHECKPOINT``\
s first, so the final shutdown checkpoint Postgres itself issues has as
little work left to do as possible.

By default the node must already be in maintenance (see
:ref:`pg_autoctl_enable_maintenance`) -- the command refuses otherwise,
so a restart can never happen by accident to a node that's still serving
traffic. Pass ``--maintenance`` to have the command enter and leave
maintenance mode around the restart itself, which is all that's needed
when the change is a configuration edit made ahead of time (nothing
needs to happen while Postgres is stopped). When something must run
while Postgres is down -- an offline on-disk tool, say -- pass it to
``--exec``: the command runs between the stop and the start, and
Postgres only comes back up once it succeeds.

See :ref:`restart_postgres` in the Operations guide for worked examples,
including enabling data checksums.

::

   usage: pg_autoctl restart postgres  [ --pgdata ] [ --maintenance [ --allow-failover ] | --force ] [ --exec <command> ]

  --pgdata          path to data directory
  --maintenance     enable/disable maintenance around the restart
  --allow-failover  with --maintenance, allow restarting the primary
  --force           skip state checks and restart unconditionally
  --exec            shell command to run while Postgres is stopped

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``.

--maintenance

  Enable maintenance mode before stopping Postgres, and disable it again
  once Postgres is back up. Without this option, the node must already
  be in maintenance mode.

--allow-failover

  Together with ``--maintenance``, allow restarting a primary: the
  monitor promotes a standby first, the same way
  ``pg_autoctl enable maintenance --allow-failover`` does. Without it,
  ``--maintenance`` on a primary is refused. Requires ``--maintenance``.

--force

  Skip every state check (maintenance state, primary or standby) and
  restart Postgres unconditionally, without touching maintenance mode at
  all. Mutually exclusive with ``--maintenance`` and ``--allow-failover``.

--exec

  A shell command to run while Postgres is stopped, before starting it
  back up again -- for an offline step such as ``pg_checksums --enable``
  that can't run on a live cluster. Run the same way Postgres itself
  runs ``archive_command``: the whole string is handed to the shell via
  ``system(3)``, so it can be a one-liner, a script in any language, or
  a pipeline, not only a single directly-executable binary. A non-zero
  exit (or termination by signal) aborts before Postgres is started back
  up and before maintenance mode is disabled, leaving the node exactly
  where you can inspect and finish the job by hand.

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
