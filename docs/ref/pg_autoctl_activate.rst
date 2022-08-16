.. _pg_autoctl_activate:

pg_autoctl activate
===================

pg_autoctl activate - Activate a Citus worker from the Citus coordinator

Synopsis
--------

This command calls the Citus “activation” API so that a node can be used to
host shards for your reference and distributed tables.

::

  usage: pg_autoctl activate  [ --pgdata ]

    --pgdata      path to data directory

Description
-----------

When creating a Citus worker, ``pg_autoctl create worker`` automatically
activates the worker node to the coordinator. You only need this command
when something unexpected have happened and you want to manually make sure
the worker node has been activated at the Citus coordinator level.

Starting with Citus 10 it is also possible to activate the coordinator
itself as a node with shard placement. Use ``pg_autoctl activate`` on your
Citus coordinator node manually to use that feature.

When the Citus coordinator is activated, an extra step is then needed for it
to host shards of distributed tables. If you want your coordinator to have
shards, then have a look at the Citus API citus_set_node_property_ to set
the ``shouldhaveshards`` property to ``true``.

.. _citus_set_node_property:
  http://docs.citusdata.com/en/v10.0/develop/api_udf.html#citus-set-node-property

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``. Use ``--monitor`` to connect to a monitor
  from anywhere, rather than the monitor URI used by a local Postgres node
  managed with ``pg_autoctl``.
