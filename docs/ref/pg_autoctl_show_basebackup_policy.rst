.. _pg_autoctl_show_basebackup_policy:

pg_autoctl show basebackup-policy
===================================

pg_autoctl show basebackup-policy - Show a named base-backup
production/retention policy

Synopsis
--------

This command fetches a base-backup production/retention policy by name
from the monitor and prints it::

  usage: pg_autoctl show basebackup-policy  --monitor --name [ --json ]

  --monitor   pg_auto_failover Monitor Postgres URL
  --name      policy name
  --json      output data in the JSON format

Description
-----------

Prints every field of the named policy: ``source``, ``replaymode``,
``cache``, ``frequency``, ``maxcount``, ``maxage``, ``onpromotion``, and
``concurrency`` -- see :ref:`pg_autoctl_create_basebackup_policy` for what
each one controls.

Options
-------

The following options are available to ``pg_autoctl show basebackup-policy``:

--monitor

  Postgres URI used to connect to the monitor. Must use the ``autoctl_node``
  username and target the ``pg_auto_failover`` database name. It is possible
  to show the Postgres URI from the monitor node using the command
  :ref:`pg_autoctl_show_uri`.

--name

  Name of the policy to show.

--json

  Output data in the JSON format.
