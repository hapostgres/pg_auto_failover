.. _pg_walserver_list_clusters:

pg_walserver list clusters
============================

pg_walserver list clusters - List every route, its backup/capture status, and its WAL range

Synopsis
--------

::

  pg_walserver list clusters --pgdata <path> [--cluster <name>]

Lists every route configured in ``<pgdata>/pg_walserver.ini``: whether it
has a base backup, its ``capture`` setting, whether its embedded pull
capturer is currently running, and the WAL range it currently covers (the
start LSN from its latest base backup's own ``backup_label``, the end LSN
from the newest WAL segment actually present).

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
  CLUSTER              BACKUP   CAPTURE   CAPTURER  WAL START              WAL END
  --------------------------------------------------------------------------------------------
  mycluster            yes      pull      yes       0/04000028             0/05000000
  another               yes      none      n/a       0/09000028             -

Having no embedded capturer to report on, running or otherwise, is why
``another`` shows ``CAPTURER n/a``: it was set up with ``--no-capture``,
and its WAL arrives only through ``archive-wal``/``ARCHIVE_FILE`` pushes.

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_backups`
* :ref:`pg_walserver_list_wal`
