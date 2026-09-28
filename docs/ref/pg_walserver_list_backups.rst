.. _pg_walserver_list_backups:

pg_walserver list backups
===========================

pg_walserver list backups - List base backups per cluster

Synopsis
--------

::

  pg_walserver list backups --pgdata <path> [--cluster <name>]

Lists every base backup found under each matching route's own
``basebackups/`` directory: its label, when it was taken, its size on
disk, and whether it is the route's ``.latest``.

Options
-------

--pgdata

  Where ``<pgdata>/pg_walserver.ini`` lives. Defaults to ``PGDATA``.

--cluster

  Limit output to a single route.

Examples
--------

::

  archive$ pg_walserver list backups --cluster mycluster
  CLUSTER              LABEL                        TAKEN AT               SIZE       LATEST
  --------------------------------------------------------------------------------------------
  mycluster            basebackup-20260928T211526Z  2026-09-28T21:15:26Z   56.1MB     yes

See Also
--------

* :ref:`pg_walserver_list`
* :ref:`pg_walserver_list_clusters`
* :ref:`pg_walserver_archive_cleanup`
