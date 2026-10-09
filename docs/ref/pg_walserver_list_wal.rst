.. _pg_walserver_list_wal:

pg_walserver list wal
=======================

pg_walserver list wal - List WAL cache stats per cluster, or every file with --segments

Synopsis
--------

::

  pg_walserver list wal --pgdata <path> [--config <path>]
      [--cluster <name>] [--segments]

Prints aggregate WAL cache stats per cluster by default (segment count,
total bytes, oldest and newest segment, ``.history`` file count), one
``pg_controldata``-style ``Label:  value`` block per cluster, rather than
a table -- one cluster's worth of facts read more naturally stacked than
crammed into a row; with ``--segments``, lists every individual
WAL/``.partial``/``.backup``/``.history`` file instead, still a table.

Options
-------

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--cluster

  Limit output to a single cluster.

--segments

  List every individual file instead of the default aggregate stats.

Examples
--------

Aggregate stats::

  archive$ pg_walserver list wal --cluster mycluster
  Cluster:   mycluster
  Segments:  4
  Size:      80 MB
  Oldest:    00000001000000000000001C
  Newest:    00000001000000000000001F
  History:   0

With more than one cluster matching (no ``--cluster``, or a ``--cluster``
that isn't given at all), each cluster's own block is separated by a
blank line.

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_clusters`
* :ref:`pg_walserver_archive_cleanup`
