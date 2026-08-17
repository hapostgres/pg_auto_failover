.. _pg_autoctl_create_basebackup_policy:

pg_autoctl create basebackup-policy
====================================

pg_autoctl create basebackup-policy - Create a named base-backup
production/retention policy

Synopsis
--------

This command registers a new base-backup production/retention policy on
the monitor. An archiver's own scheduling (when to take the next base
backup) and retention (which older ones to prune) are driven entirely by
whichever policy applies to its (formation, group) -- attach a policy to
an archiver's own formation with ``pg_autoctl create archiver
--basebackup-policy <name>``::

  usage: pg_autoctl create basebackup-policy  --monitor --name --config

  --monitor   pg_auto_failover Monitor Postgres URL
  --name      policy name
  --config    path to a JSON document with the policy body

Description
-----------

A base-backup policy controls three independent things for whichever
archiver(s) it applies to:

  - **when** to produce the next base backup (``frequency``, and
    ``onpromotion`` to force one immediately after a failover regardless
    of ``frequency``),
  - **how** to produce it (``source``: ``live``, straight from a running
    node, or ``replay``, replayed locally from already-captured WAL --
    and ``replaymode`` when ``source`` is ``replay``),
  - **how many to keep** (``maxcount``, ``maxage``: whichever fires first
    prunes a given backup -- the directory is removed and the base
    backup's own history row is marked deleted, which in turn prunes any
    WAL segments no remaining backup still needs).

A policy is a standalone, independently-referenceable row: the same one
can be shared by every archiver in a fleet, or kept private to a single
(formation, group) via :ref:`pg_autoctl_set` ``archiver-policy``-style
group overrides. A formation that never creates or attaches a policy of
its own uses this schema's own ``default`` policy (nightly-equivalent:
``frequency`` 24 hours, ``maxcount`` 3, ``maxage`` 3 days).

The ``--config`` document is a flat JSON object with any subset of the
following keys -- any key left out keeps its own default (on ``create``)
or its current value (on :ref:`pg_autoctl_set_basebackup_policy`)::

  {
    "source": "replay",
    "replaymode": "volatile",
    "cache": "local",
    "frequency": "6h",
    "maxcount": 3,
    "maxage": "7d",
    "onpromotion": true,
    "concurrency": 1
  }

``frequency`` and ``maxage`` accept any text Postgres itself parses as an
``interval`` (``"6h"``, ``"3 days"``, ``"90 minutes"``, ...).

Options
-------

The following options are available to ``pg_autoctl create basebackup-policy``:

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--name

  Name of the policy to create.

--config

  Path to a JSON document with the policy body, as described above.
