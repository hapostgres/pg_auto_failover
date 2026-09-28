.. _pg_walserver_list_wal:

pg_walserver list wal
=======================

pg_walserver list wal - List WAL cache stats per cluster, or every file with --segments

Synopsis
--------

::

  pg_walserver list wal --pgdata <path> [--cluster <name>] [--segments]

Prints aggregate WAL cache stats per route by default (segment count,
total bytes, oldest and newest segment, ``.history`` file count); with
``--segments``, lists every individual WAL/``.partial``/``.backup``/
``.history`` file instead.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

--segments

  List every individual file instead of the default aggregate stats.

Examples
--------

Aggregate stats::

  archive$ pg_walserver list wal --pgdata /var/lib/archiver
  CLUSTER              SEGMENTS  SIZE       OLDEST                   NEWEST                   HISTORY
  --------------------------------------------------------------------------------------------
  mycluster            2         48.0MB     000000010000000000000001 000000010000000000000002 0

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_clusters`
* :ref:`pg_walserver_archive_cleanup`
