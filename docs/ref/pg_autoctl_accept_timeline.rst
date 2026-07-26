.. _pg_autoctl_accept_timeline:

pg_autoctl accept timeline
==========================

pg_autoctl accept timeline - Accept a timeline as the ground truth after a detected fork

Synopsis
--------

This command pins a timeline as the group's ground-truth lineage on the
pg_auto_failover monitor::

  usage: pg_autoctl accept timeline  [ --pgdata --formation --group ] --tli <tli>

  --pgdata      path to data directory
  --formation   formation to target, defaults to 'default'
  --group       group to target, defaults to 0
  --tli         timeline to accept, as shown by `pg_autoctl show timeline`
  --reason      free-text note explaining the decision

Description
-----------

pg_auto_failover normally detects and resolves timeline forks on its own: a
standby whose local WAL diverges from the group's reference lineage is
rewound onto it automatically the next time it reconnects (see
:ref:`timeline_forks` for the full scenario, and the ``Report_LSN`` and
``Catchingup`` sections of :ref:`failover_state_machine` for how the
election and the reconnect path use this).

Auto-detection compares each node's known timeline history against every
other node's, and only reaches a confident answer when there is a sibling to
compare against. In a two-node formation, or when every surviving node
happens to already be on the same diverged branch, there is nothing left to
disagree with — the fork stays invisible to the automatic check even though
one of the branches is not real ground truth. ``pg_autoctl accept timeline``
is the operator override for that case: it pins which timeline is ground
truth, and the election's ancestry filter (``FilterNodesByTimelineAncestry``)
uses the pinned value instead of trying to auto-detect it. Nodes not on the
accepted lineage need a ``pg_rewind`` (done automatically once they
reconnect) before they can rejoin.

Use :ref:`pg_autoctl_show_timeline` first to see the group's known timeline
history and each node's current status against it.

The pin is automatically marked resolved once a primary is promoted on the
accepted lineage — there is no ``pg_autoctl resolve timeline`` command to
run afterwards.

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

--tli

  The timeline id to accept as ground truth, as shown in the ``TLI`` column
  of ``pg_autoctl show timeline``. Mandatory. The monitor refuses to pin a
  timeline that no node in the group has ever reported.

--reason

  A free-text note recording why this timeline was accepted. Stored
  alongside the pin as a permanent audit record; purely informational.

Environment
-----------

PGDATA

  Postgres directory location. Can be used instead of the ``--pgdata``
  option.

PG_AUTOCTL_MONITOR

  Postgres URI to connect to the monitor node, can be used instead of the
  ``--monitor`` option.

PGHOST, PGPORT, PGDATABASE, PGUSER, PGCONNECT_TIMEOUT, ...

  See the `Postgres docs about Environment Variables`__ for details.

  __ https://www.postgresql.org/docs/current/libpq-envars.html

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

Accepting a timeline that no node has ever reported fails::

   $ pg_autoctl accept timeline --tli 99 --formation default
   07:50:28 522 INFO  Targetting group 0 in formation "default"
   07:50:28 522 ERROR Monitor ERROR:  timeline 99 has never been reported by any node in formation "default" group 0
   07:50:28 522 ERROR Monitor CONTEXT:  PL/pgSQL function pgautofailover.accept_timeline(text,integer,integer,text) line 15 at RAISE
   07:50:28 522 ERROR SQL query: SELECT pgautofailover.accept_timeline($1, $2, $3, $4)
   07:50:28 522 ERROR SQL params: 'default', '0', '99', NULL
   07:50:28 522 ERROR Failed to accept timeline 99 for formation default and group 0
   07:50:28 522 FATAL Failed to accept timeline 99 for formation "default" group 0, see above for details

Pinning the real lineage after confirming it against ``pg_autoctl show
timeline`` (see :ref:`pg_autoctl_show_timeline` for the full example this
continues from) succeeds and immediately affects the next election::

   $ pg_autoctl accept timeline --tli 1 --formation default \
       --reason "node2 self-promoted out of band during a network partition; tli 2 confirmed false via pg_controldata"
   07:50:35 569 INFO  Targetting group 0 in formation "default"
   Timeline 1 accepted as ground truth for formation "default" group 0. The election will now only consider nodes on that lineage; other nodes need pg_rewind before rejoining.

From this point on, ``node2`` is excluded from candidacy until it rewinds
onto timeline 1, which happens automatically the next time it reconnects
(see the recovery example in :ref:`pg_autoctl_show_timeline`).
