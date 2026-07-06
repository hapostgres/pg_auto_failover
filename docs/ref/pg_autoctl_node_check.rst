.. _pg_autoctl_node_check:

pg_autoctl node check
=====================

pg_autoctl node check - Validate a pg_autoctl_node.ini file without creating anything

Synopsis
--------

::

    pg_autoctl node check [<file.ini>]

    <file.ini>   path to the pg_autoctl_node.ini file
                 (default: /etc/pgaf/node.ini)

Description
-----------

``pg_autoctl node check`` parses a ``pg_autoctl_node.ini`` file, resolves
defaults, and prints the fully-resolved configuration to stdout.  It exits
with a non-zero status if the file contains any syntax errors or invalid
field values.

No Postgres instance is touched and no pg_autoctl state is modified.  This
command is safe to run at any time.

Use ``pg_autoctl node check`` to:

- Validate a newly written ini file before deploying it to a container.
- Verify that defaults are resolved as expected (for example, that
  ``hostname`` is auto-detected correctly).
- Catch errors in CI before reaching the node creation step.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_node_run`,
:ref:`pg_autoctl_node_show`
