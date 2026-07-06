.. _pg_autoctl_node_show:

pg_autoctl node show
====================

pg_autoctl node show - Dump current node configuration as a pg_autoctl_node.ini file

Synopsis
--------

::

    pg_autoctl node show [--pgdata <dir>]

    --pgdata      path to data directory (default: $PGDATA)

Description
-----------

``pg_autoctl node show`` reads the running node's ``pg_autoctl.cfg``
configuration file and writes its contents to stdout in
``pg_autoctl_node.ini`` format.

This is useful for:

- Capturing the current configuration of a node that was created with
  ``pg_autoctl create postgres`` flags, so it can be managed declaratively
  going forward.
- Comparing the effective configuration of a running node against a desired
  ``pg_autoctl_node.ini`` file.
- Generating a baseline ini file to store in version control.

The output is a valid ``pg_autoctl_node.ini`` file that can be passed
directly to ``pg_autoctl node run`` or ``pg_autoctl node apply``.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_node_run`,
:ref:`pg_autoctl_node_check`
