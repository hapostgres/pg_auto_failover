.. _pg_walserver_list:

pg_walserver list
==================

pg_walserver list - List archived clusters, base backups, or WAL

.. toctree::
   :hidden:
   :maxdepth: 1

   pg_walserver_list_clusters
   pg_walserver_list_backups
   pg_walserver_list_wal

Listing something about the archived data itself -- never
``pg_walserver``'s own configuration footprint, see
:ref:`pg_walserver_ls` for that -- is what each of ``pg_walserver
list``'s three sub-commands does, each with its own manual page:
:ref:`pg_walserver_list_clusters`, :ref:`pg_walserver_list_backups`,
and :ref:`pg_walserver_list_wal`.
