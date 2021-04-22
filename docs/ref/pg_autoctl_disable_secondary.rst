.. _pg_autoctl_disable_secondary:

pg_autoctl disable secondary
============================

pg_autoctl disable secondary - Disable secondary nodes on a formation

Synopsis
--------

This feature makes the most sense when using the Enterprise Edition of
pg_auto_failover, which is fully compatible with Citus formations. When
``secondary`` are disabled, then Citus workers creation policy is to assign a
primary node then a standby node for each group. When ``secondary`` is
disabled the Citus workers creation policy is to assign only the primary
nodes.

::

   usage: pg_autoctl disable secondary  [ --pgdata --formation ]

  --pgdata      path to data directory
  --formation   Formation to disable secondary on


Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.

--formation

  Target formation where to disable secondary feature.
