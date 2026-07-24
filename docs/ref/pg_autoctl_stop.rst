.. _pg_autoctl_stop:

pg_autoctl stop
===============

pg_autoctl stop - signal the pg_autoctl service for it to stop

Synopsis
--------

This commands stops the processes needed to run a monitor node or a keeper
node, depending on the configuration file that belongs to the ``--pgdata``
option or ``PGDATA`` environment variable.

::

  usage: pg_autoctl stop  [ --pgdata --fast --immediate ]

  --pgdata      path to data directory
  --fast        fast shutdown mode for the keeper
  --immediate   immediate shutdown mode for the keeper

Description
-----------

The ``pg_autoctl stop`` commands finds the PID of the running service for
the given ``--pgdata``, and if the process is still running, sends a
``SIGTERM`` signal to the process.

When ``pg_autoctl`` receives a shutdown signal a shutdown sequence is
triggered. Depending on the signal received, an operation that has been
started (such as a state transition) is either run to completion, stopped as
the next opportunity, or stopped immediately even when in the middle of the
transition.

A plain ``SIGTERM`` (the default, no ``--fast`` or ``--immediate`` flag) is
a **graceful** shutdown: for a primary, secondary, or catching-up node,
running with the monitor enabled, ``pg_autoctl`` requests :ref:`maintenance
<pg_autoctl_enable_maintenance>` on its own behalf before stopping, the same
mechanism used by ``pg_autoctl enable maintenance``. A healthy standby can
then take over immediately, rather than waiting for the monitor's own
health-check timeout to notice the node is gone. This can take up to 30
seconds. If maintenance cannot be requested — the monitor is disabled, the
monitor is unreachable, or there is no candidate currently available to
take over — ``pg_autoctl`` falls back to stopping Postgres directly and
reporting the shutdown to the monitor for up to another 30 seconds, so a
failover can still be driven by the usual health-check mechanism.

A node that entered maintenance this way (as opposed to an operator running
``pg_autoctl enable maintenance`` directly) automatically leaves maintenance
the next time it is started, with no need to run ``pg_autoctl disable
maintenance`` manually. An operator-initiated maintenance session is left
untouched by a restart, and still needs an explicit ``pg_autoctl disable
maintenance`` to end.

The ``--fast`` and ``--immediate`` options skip all of that: they stop the
node right away without attempting a graceful handoff, which is the closest
equivalent to simulating a hard crash.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--fast

  Fast Shutdown mode for ``pg_autoctl``. Sends the ``SIGINT`` signal to the
  running service, which is the same as using ``C-c`` on an interactive
  process running as a foreground shell job.

--immediate

  Immediate Shutdown mode for ``pg_autoctl``. Sends the ``SIGQUIT`` signal
  to the running service.

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

  
