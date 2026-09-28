.. _pg_walserver_list_clusters:

pg_walserver list clusters
============================

pg_walserver list clusters - List every route, its backup/receivewal status, and its WAL range

Synopsis
--------

::

  pg_walserver list clusters --pgdata <path> [--cluster <name>]

Lists every route configured in ``<pgdata>/pg_walserver.ini``: whether it
has a base backup, its ``receivewal`` setting, whether its embedded
receivewal worker is currently running, and the WAL range it currently
covers (the start LSN from its latest base backup's own
``backup_label``, the end LSN from the newest WAL segment actually
present).

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

Examples
--------

::

  archive$ pg_walserver list clusters --pgdata /var/lib/archiver
  CLUSTER              BACKUP   RECEIVEWAL   WORKER   WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull         yes      0/02000028             0/04000060
  another               yes      none         n/a      0/09000028             -

Having no embedded receivewal worker to report on, running or
otherwise, is why ``another`` shows ``WORKER n/a``: it was set up with
``--no-receivewal``, and its WAL arrives only through
``archive-wal``/``ARCHIVE_FILE`` pushes. For a route with a running
worker, ``WAL END`` is that worker's own live, currently-observed LSN
(see :ref:`pg_walserver_ps`) when one is available, falling back to a
directory scan's coarser segment-boundary approximation otherwise.

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_backups`
* :ref:`pg_walserver_list_wal`
