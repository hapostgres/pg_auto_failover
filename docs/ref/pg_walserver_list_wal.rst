.. _pg_walserver_list_wal:

pg_walserver list wal
=======================

pg_walserver list wal - List WAL cache stats per cluster, or every file with --segments

Synopsis
--------

::

  pg_walserver list wal --pgdata <path> [--config <path>]
      [--cluster <name>] [--segments]

Prints aggregate WAL cache stats per route by default (segment count,
total bytes, oldest and newest segment, ``.history`` file count); with
``--segments``, lists every individual WAL/``.partial``/``.backup``/
``.history`` file instead.

Options
-------

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--cluster

  Limit output to a single route.

--segments

  List every individual file instead of the default aggregate stats.

Examples
--------

Aggregate stats::

  archive$ pg_walserver list wal --cluster mycluster
  CLUSTER              SEGMENTS  SIZE       OLDEST                   NEWEST                   HISTORY
  -------------------- --------- ---------- ------------------------ ------------------------ -------
  mycluster            1         32.0MB     00000001000000000000002C 00000001000000000000002C 0

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_clusters`
* :ref:`pg_walserver_archive_cleanup`
