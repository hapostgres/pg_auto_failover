.. _pg_autoctl_archiver_formation:

pg_autoctl archiver formation
===============================

pg_autoctl archiver formation - Manage the formations an archiver is attached to

Synopsis
--------

``pg_autoctl archiver formation`` attaches an already-registered archiver
to a formation, detaches it from one, or lists who is currently attached
-- the same attach/detach mechanism ``pg_autoctl create archiver
--formation`` uses at create time, callable again afterwards. Unlike
``create archiver``, these commands connect straight to ``--monitor``:
they have no ``--pgdata`` of their own, since attaching to a formation is
a monitor-side operation, not something written to any one node's local
files.

::

  usage: pg_autoctl archiver formation add  --monitor --name --formation

    --monitor    monitor uri to connect to
    --name       name of the archiver to attach
    --formation  formation to attach it to

  usage: pg_autoctl archiver formation remove  --monitor --name --formation

    --monitor    monitor uri to connect to
    --name       name of the archiver to detach
    --formation  formation to detach it from

  usage: pg_autoctl archiver formation list  --monitor --formation [ --json ]

    --monitor    monitor uri to connect to
    --formation  formation to list archivers for
    --json       output data in the JSON format

Description
-----------

``add`` attaches the named archiver to one more formation, creating one
ARCHIVING node row per group already in that formation (a multi-group
Citus formation gets one membership per worker group from this single
call). If the target formation has no group registered yet, ``add``
retries -- the same behaviour ``pg_autoctl create archiver --formation``
itself has -- for up to 15 minutes rather than failing immediately, so
this command doesn't need to be sequenced after every node of the target
formation has finished registering.

``remove`` detaches the archiver from a formation, dropping its ARCHIVING
node row (and the replication slot backing it) in every group of that
formation. Segments this archiver already captured stay recorded on the
monitor; other archivers still attached to that formation are unaffected.

``list`` shows every archiver currently attached to a formation, with its
FSM state and storage usage -- the same data ``pg_autoctl watch`` renders
interactively for its archivers panel, exposed here as a plain listing.

Options
-------

--monitor

  PostgreSQL URI used to connect to the monitor.

--name

  Name of the archiver to attach or detach (``add``/``remove`` only) --
  the same ``--name`` given to ``pg_autoctl create archiver``.

--formation

  Formation to attach to, detach from, or list archivers for.

--json

  Output data in the JSON format (``list`` only).

See Also
--------

:ref:`pg_autoctl_create_archiver` sets an archiver's first formation(s)
at create time.

:ref:`pg_autoctl_archiver_show` reads what an archiver attached to a
formation has actually captured.
