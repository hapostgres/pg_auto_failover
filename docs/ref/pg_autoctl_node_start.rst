.. _pg_autoctl_node_start:

pg_autoctl node start
=====================

pg_autoctl node start - Release a node waiting in a deferred launch

Synopsis
--------

::

    pg_autoctl node start [<file.ini>]

    <file.ini>   path to the pg_autoctl_node.ini file
                 (default: /etc/pgaf/node.ini)

Description
-----------

``pg_autoctl node start`` releases a node that is waiting on either or
both of ``[launch] create = deferred`` / ``run = deferred``.  It clears
both flags in the ini file (rewriting them to ``immediate``); the waiting
node detects the change within the poll interval and proceeds.

This command is idempotent: calling it on a node that is already running
(both flags already ``immediate``) is a no-op.

The Deferred-Launch Pattern
----------------------------

A node configured with ``[launch] create = deferred`` and/or ``run =
deferred`` starts a polling loop and waits instead of immediately
creating or starting Postgres.  This enables ordered startup without an
external orchestrator::

    # In the ini file for each data node:
    [launch]
    create = deferred
    run    = deferred

The monitor can be left at the defaults (``immediate``), while data nodes
start deferred.  Once the monitor is confirmed ready, release each data
node::

    pg_autoctl node start

An :ref:`archiving_architecture` archiver whose target formation (or, for
a Citus formation, one or more of its groups) might not exist yet at
container-start time follows the same pattern -- see
:ref:`pg_autoctl_node_run`'s own note on why an archiver specifically
needs this, unlike an ordinary node.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_node_run`
