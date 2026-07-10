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
2. If ``[launch] mode = deferred``, polls the file until the section is
   removed or changed to ``mode = immediate`` (see ``pg_autoctl node start``).
3. Checks whether the node already exists (looks for ``pg_autoctl.cfg``
   inside ``pgdata``).

   - **First start** — builds the ``pg_autoctl create <kind> [flags] --run``
     argument list from the ini file and exec's into it, which creates Postgres
     and starts the supervisor in one step.
   - **Subsequent starts** — applies any mutable setting changes found in the
     ini file, then exec's into ``pg_autoctl run --pgdata <dir>``.

4. Sets the ``PG_AUTOCTL_NODESPEC`` environment variable to the ini file
   path before exec'ing, so the supervisor can watch the file for live
   changes to ``[settings]``.

Because the command uses ``execv()``, the pg_autoctl supervisor becomes
the direct child process (PID 1 in a container), preserving the standard
Unix signal contract — ``SIGTERM`` stops the supervisor cleanly,
``SIGHUP`` reloads configuration.

The ``launch = deferred`` pattern
----------------------------------

The ``[launch]`` section enables ordered startup without an external
orchestrator::

    [launch]
    mode = deferred

A node with ``mode = deferred`` starts the polling loop and waits. A
second container, sidecar, or init script calls::

    pg_autoctl node start /etc/pgaf/node.ini

which rewrites the file with ``mode = immediate``. The waiting node detects
the change and proceeds. This is useful when you need to ensure the monitor
is fully up before any data node attempts registration, or when bringing up
Citus workers in a specific order.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_create_postgres`,
:ref:`pg_autoctl_run`
