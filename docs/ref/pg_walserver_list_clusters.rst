.. _pg_walserver_list_clusters:

pg_walserver list clusters
============================

pg_walserver list clusters - List every route, its backup/receivewal status, and its WAL range

Synopsis
--------

::

  pg_walserver list clusters --pgdata <path> [--config-file <path>]
      [--cluster <name>]

Lists every route configured in the config file: whether it
has a base backup, its ``receivewal`` setting, whether its embedded
receivewal worker is currently running, and the WAL range it currently
covers (the start LSN from its latest base backup's own
``backup_label``, the end LSN from the newest WAL segment actually
present).

Options
-------

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--cluster

  Limit output to a single route.

Examples
--------

::

  archive$ pg_walserver list clusters
  CLUSTER              BACKUP   RECEIVEWAL WORKER   WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull       yes      0/2C000028             0/2D000060
  another               yes      none       n/a      0/18000028             -
  third                yes      pull       yes      0/1C000028             0/2D000060
  bbdemo               yes      none       n/a      0/2A000028             -

Having no embedded receivewal worker to report on, running or
otherwise, is why ``another`` shows ``WORKER n/a``: it was registered
with ``--no-receivewal``, and its WAL arrives only through
``archive-wal``/``ARCHIVE_FILE`` pushes. For a route with a running
worker, ``WAL END`` is that worker's own live, currently-observed LSN
(see :ref:`pg_walserver_ps`) when one is available, falling back to a
directory scan's coarser segment-boundary approximation otherwise.

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_backups`
* :ref:`pg_walserver_list_wal`
