.. _pg_autoctl_node_start:

pg_autoctl node start
=====================

pg_autoctl node start - Release a node waiting in launch=deferred mode

Synopsis
--------

::

    pg_autoctl node start [<file.ini>]

    <file.ini>   path to the pg_autoctl_node.ini file
                 (default: /etc/pgaf/node.ini)

Description
-----------

``pg_autoctl node start`` releases a node that is waiting in
``[launch] mode = deferred``.  It rewrites the ini file with
``mode = immediate``; the waiting node detects the change within the poll
interval and proceeds to create or run.

This command is idempotent: calling it on a node that is already running
(or has ``mode = immediate``) is a no-op.

The ``launch = deferred`` Pattern
----------------------------------

A node configured with ``[launch] mode = deferred`` starts a polling loop
and waits instead of immediately creating or starting Postgres.  This
enables ordered startup without an external orchestrator::

    # In the ini file for each data node:
    [launch]
    mode = deferred

The monitor can be given ``mode = immediate`` (the default), while data nodes
start with ``mode = deferred``.  Once the monitor is confirmed ready, release
each data node::

    pg_autoctl node start /etc/pgaf/node.ini

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_node_run`
