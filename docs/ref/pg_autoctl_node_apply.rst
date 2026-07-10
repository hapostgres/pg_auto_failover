.. _pg_autoctl_node_apply:

pg_autoctl node apply
=====================

pg_autoctl node apply - Apply mutable settings from a pg_autoctl_node.ini to a running node

Synopsis
--------

::

    pg_autoctl node apply [<file.ini>]

    <file.ini>   path to the pg_autoctl_node.ini file
                 (default: /etc/pgaf/node.ini)

Description
-----------

``pg_autoctl node apply`` reads a ``pg_autoctl_node.ini`` file, compares its
mutable fields against the node's current configuration, and converges any
differences without restarting the node or interrupting replication.

This is the same convergence logic the supervisor runs automatically when it
detects a file change.  Running ``pg_autoctl node apply`` manually is useful
when the supervisor is not running, or when you want to apply a change
immediately rather than waiting for the next watch interval.

Mutable fields applied by this command:

``candidate_priority``
    Applied via :ref:`pg_autoctl_set_node_candidate_priority`.

``replication_quorum``
    Applied via :ref:`pg_autoctl_set_node_replication_quorum`.

``ssl``, ``ca_file``, ``cert_file``, ``key_file``
    Applied via ``pg_autoctl enable ssl``.  Postgres reloads its SSL
    configuration without a full restart.

``monitor.pguri``
    Applied via ``pg_autoctl disable monitor --force`` followed by
    ``pg_autoctl enable monitor <new_uri>``.  The node is re-registered to
    the new monitor without stopping Postgres.

Immutable fields (``kind``, ``pgdata``, ``hostname``, ``port``,
``auth``, ``pg_hba_lan``) are logged as warnings when they differ; they take
effect only the next time the node is created or started from scratch.

See Also
--------

:ref:`pg_autoctl_node`, :ref:`pg_autoctl_node_run`,
:ref:`pg_autoctl_set`, :ref:`pg_autoctl_enable`
