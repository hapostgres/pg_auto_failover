.. _pg_autoctl_node_run:

pg_autoctl node run
===================

pg_autoctl node run - Create (if needed) and run a node from a pg_autoctl_node.ini file

Synopsis
--------

::

    pg_autoctl node run [<file.ini>]

    <file.ini>   path to the pg_autoctl_node.ini file
                 (default: /etc/pgaf/node.ini)

Description
-----------

``pg_autoctl node run`` is the single entry-point for container-based
deployments. Given a ``pg_autoctl_node.ini`` file it:

1. Reads and validates the ini file.
2. If ``[launch] create = deferred``, polls the file every 0.5s until it's
   changed to ``create = immediate`` (see ``pg_autoctl node start``).
3. Checks whether the node already exists (looks for ``pg_autoctl.cfg``
   inside ``pgdata``).

   - **First start** — runs ``pg_autoctl create <kind> [flags]`` (built
     from the ini file, *without* ``--run``) to create the node.
   - **Subsequent starts** — applies any mutable setting changes found in
     the ini file to the already-existing node.

4. If ``[launch] run = deferred``, polls the file every 0.5s until it's
   changed to ``run = immediate``.
5. Exec's into ``pg_autoctl run --pgdata <dir>``, which starts Postgres (if
   applicable for this node kind) and the supervisor.
6. Sets the ``PG_AUTOCTL_NODESPEC`` environment variable to the ini file
   path before exec'ing, so the supervisor can watch the file for live
   changes to ``[settings]``.

``create`` and ``run`` are independent gates: creating the node (step 3)
and starting it (step 5) can each be deferred on their own. Setting only
``create = deferred`` (leaving ``run`` at its default) creates the node
immediately once released and starts it right away in the same
invocation; setting only ``run = deferred`` creates the node immediately
but leaves it stopped until separately released.

Because the command uses ``execv()``, the pg_autoctl supervisor becomes
the direct child process (PID 1 in a container), preserving the standard
Unix signal contract — ``SIGTERM`` stops the supervisor cleanly,
``SIGHUP`` reloads configuration. See :ref:`pg_autoctl_stop` for what a
graceful ``SIGTERM`` actually does before the node stops.

The deferred-launch pattern
----------------------------

The ``[launch]`` section enables ordered startup without an external
orchestrator::

    [launch]
    create = deferred
    run    = deferred

A node configured this way starts the polling loop and waits. A second
container, sidecar, or init script calls::

    pg_autoctl node start

which clears both flags. The waiting node detects the change and
proceeds. This is useful when you need to ensure the monitor is fully up
before any data node attempts registration, when bringing up Citus
workers in a specific order, or -- for an :ref:`archiving_architecture`
archiver -- when its target formation (or, for a Citus formation, every
one of its groups) might not exist yet: ``pg_autoctl create archiver``
has no retry-until-ready loop of its own the way an ordinary node's
registration does, so it must not run before the formation is ready.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_create_postgres`,
:ref:`pg_autoctl_create_archiver`, :ref:`pg_autoctl_run`
