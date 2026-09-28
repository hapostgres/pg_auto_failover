.. _pg_walserver_ps:

pg_walserver ps
================

pg_walserver ps - Show serve's own process-level status (pid, receivewal workers, bootstrap jobs)

Synopsis
--------

::

  pg_walserver ps --pgdata <path>

Shows ``serve``'s own process-level status as a small process tree,
``pstree``-style: its own pid at the root, one branch per supervised
embedded receivewal worker child (cluster, running or stopped, uptime,
restart count, and its current receiving LSN when one has been
observed) or in-flight one-time bootstrap base backup job.
Reads the same pidfile :ref:`pg_walserver_reload` does to decide
whether ``serve`` is running at all; with none running, prints a clean
message and exits 0, never an error.

Options
-------

--pgdata

  This instance's own top-level storage root. Defaults to ``PGDATA``.

Examples
--------

With ``serve`` running, one route with an embedded receivewal worker::

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver(25671) running, uptime 0h04m31s
  `-- receivewal(25673) mycluster, running, uptime 0h04m31s, restarts 0, lsn 0/04000060 (timeline 1, 1s ago)

With more than one route, each gets its own branch, ``|--`` for every
one but the last, ``\`--`` for the last -- an in-flight bootstrap base
backup job (see :ref:`pg_walserver_serve`'s "Archiving one cluster")
appears the same way, as ``bootstrap(<pid>) <cluster>, running, uptime
...``. A route with no receivewal worker (``--receivewal none``) never
gets a branch at all: it has nothing running to show.

Nothing running::

  archive$ pg_walserver ps --pgdata /var/lib/archiver
  pg_walserver: not running (--pgdata "/var/lib/archiver")

See Also
--------

* :ref:`pg_walserver`
* :ref:`pg_walserver_status`
* :ref:`pg_walserver_list`
