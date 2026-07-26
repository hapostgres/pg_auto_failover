.. _pg_autoctl_show_timeline:

pg_autoctl show timeline
========================

pg_autoctl show timeline - Show the timeline history known to the monitor for a group, and each node's position against it

Synopsis
--------

This command prints the group's known timeline history and each node's
current position against it, as computed by the monitor::

  usage: pg_autoctl show timeline  [ --pgdata ] --formation --group

    --pgdata      path to data directory
    --monitor     pg_auto_failover Monitor Postgres URL
    --formation   formation to query, defaults to 'default'
    --group       group to query formation, defaults to 0

Description
-----------

Every node periodically publishes its own known timeline history (each
timeline it has ever been on, its parent timeline, and the LSN at which it
switched) to the monitor. ``pg_autoctl show timeline`` prints two tables
built from that data:

- A **timeline history** table: every ``(tli, parent tli, switchpoint LSN)``
  triple known to the group, one row per timeline, in ascending order.
- A **per-node status** table: each node's currently reported timeline and
  LSN, and whether that timeline is on the group's reference lineage — the
  accepted timeline if one has been pinned with
  :ref:`pg_autoctl_accept_timeline`, otherwise the highest reported timeline
  that nothing else in the group disagrees with. A node not on the reference
  lineage is flagged ``FORK: diverges from the reference timeline, pg_rewind
  required``.

See :ref:`timeline_forks` for the failure scenario this detects, and the
``Report_LSN`` section of :ref:`failover_state_machine` for how the
election uses this same ancestry information to exclude a diverged
candidate.

.. important::

   Auto-detection only has something to compare against when at least two
   nodes disagree. In a two-node formation — or whenever every surviving
   node happens to already be on the same diverged branch — a fork can read
   as clean, because it's the only lineage anyone is reporting. Use
   :ref:`pg_autoctl_accept_timeline` to pin the correct lineage explicitly
   when you know, from other evidence, that auto-detection got it wrong.

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

  Show the timeline history for the given formation. Defaults to the
  ``default`` formation.

--group

  Show the timeline history for the given group in the given formation.
  Defaults to group ``0``.

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

In this two-node example, ``node2`` was network-partitioned, promoted
directly at the Postgres level (bypassing ``pg_autoctl``), and given a few
local-only writes — a genuine fork onto timeline 2. Unpinned, ``node2``'s
fork reads as clean: it's the only lineage being reported, and nothing
disagrees with it (the two-node limitation described above)::

   $ pg_autoctl show timeline --formation default
        TLI | Parent TLI | Switchpoint LSN
   ---------+------------+----------------
          1 |          0 |             0/0
          2 |          1 |       0/3000130

                   Name | NodeId |  TLI |         LSN | Status
   ---------------------+--------+------+-------------+-----------------------------------------
                  node1 |      1 |    1 |   0/3000130 | ok, on accepted lineage
                  node2 |      2 |    2 |   0/3016330 | ok, on accepted lineage

After confirming, from other evidence (here, knowing which node was
manually promoted), that timeline 1 is the real lineage, pin it with
:ref:`pg_autoctl_accept_timeline`. ``node2`` is now unambiguously flagged::

   $ pg_autoctl accept timeline --tli 1 --formation default \
       --reason "node2 self-promoted out of band during a network partition"
   Timeline 1 accepted as ground truth for formation "default" group 0. The election will now only consider nodes on that lineage; other nodes need pg_rewind before rejoining.

   $ pg_autoctl show timeline --formation default
        TLI | Parent TLI | Switchpoint LSN
   ---------+------------+----------------
          1 |          0 |             0/0
          2 |          1 |       0/3000130

                   Name | NodeId |  TLI |         LSN | Status
   ---------------------+--------+------+-------------+-----------------------------------------
                  node1 |      1 |    1 |   0/30599B8 | ok, on accepted lineage
                  node2 |      2 |    2 |   0/3016330 | FORK: diverges from the reference timeline, pg_rewind required

   One or more nodes have diverged from the reference timeline (see FORK above).
   See `pg_autoctl accept timeline --help` to resolve.

Forcing ``node2`` through a resync (here, a maintenance cycle) triggers the
ancestry check and the automatic rewind. It rejoins on the accepted
lineage, and the fork clears on its own — no further operator action, and
no need to run an "accept" or "resolve" command a second time::

   $ pg_autoctl show timeline --formation default
        TLI | Parent TLI | Switchpoint LSN
   ---------+------------+----------------
          1 |          0 |             0/0
          2 |          1 |       0/3000130

                   Name | NodeId |  TLI |         LSN | Status
   ---------------------+--------+------+-------------+-----------------------------------------
                  node1 |      1 |    1 |   0/70000F8 | ok, on accepted lineage
                  node2 |      2 |    1 |   0/70000F8 | ok, on accepted lineage

See :ref:`resolving_timeline_fork` for the full walkthrough, including how
to drive the resync.
