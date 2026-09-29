.. _pg_walserver_drop:

pg_walserver drop
===================

pg_walserver drop - Drop one cluster's registration

Synopsis
--------

::

  pg_walserver drop cluster <name> --pgdata <path> [--config-file <path>]
      [--purge]

pg_walserver drop cluster
----------------------------

Removes a route's registration from the config file
(:ref:`pg_walserver_register` writes it). By default its own on-disk
data (every base backup and WAL segment it holds) is left in place;
pass ``--purge`` to also remove it. Reloads an already-running
``pg_walserver serve`` for the same ``--pgdata``, if there is one, so it
stops serving the dropped route immediately.

Options
^^^^^^^

<name>

  The cluster's own name, given positionally (never a flag).

--pgdata

  This instance's own data root. Defaults to ``PGDATA``.

--config-file

  Where the config file itself lives (defaults to
  ``<pgdata>/pg_walserver.ini``, or ``PG_WALSERVER_CONFIG_FILE``).

--purge

  Also remove the route's own on-disk data. Without it, only the
  registration itself is removed.

Examples
^^^^^^^^

Drop a route, keeping its data::

  archive$ pg_walserver drop cluster third --pgdata /var/lib/archiver
  22:57:33 90 INFO  Dropped route "third" from "/var/lib/archiver/pg_walserver.ini"
  22:57:33 90 INFO  Route "third" dropped from "/var/lib/archiver/pg_walserver.ini"; its own data under "/var/lib/archiver/third" was left in place (pass --purge to remove it too)
  22:57:33 90 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

Drop a route and remove its data with it::

  archive$ pg_walserver drop cluster tmp --pgdata /var/lib/archiver --purge
  22:57:34 104 INFO  Dropped route "tmp" from "/var/lib/archiver/pg_walserver.ini"
  22:57:34 104 INFO  Removed "/var/lib/archiver/tmp" (--purge)
  22:57:34 104 INFO  Reloaded the running pg_walserver (pid 47): it will pick up this route immediately

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_register`
* :ref:`pg_walserver_set_upstream`
