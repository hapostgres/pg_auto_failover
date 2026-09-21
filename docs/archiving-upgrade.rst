.. _archiving_upgrade:

Adding an archiver to an existing cluster
=========================================

Archiving needs the new ``archiving`` node state and a monitor extension at
version 2.3. A cluster that already runs pg_auto_failover 2.2 (or older) must
therefore be upgraded first, in a strict order, and only then can an archiver
be attached. This page gives that rolling upgrade procedure and explains
why the order matters.

Why the order matters
---------------------

Every keeper (the ``pg_autoctl`` process of each Postgres node) asks the
monitor for the list of the other nodes in its group and parses it. Once an
archiver is attached, that list contains a row for the archiver (with a
``host:0`` address, an archiver has no Postgres port to speak of) and the
group may report the new ``archiving`` state. A keeper running an older
version cannot parse either of them and fails its ``node_active`` loop. So:

1. the monitor is upgraded first (a newer monitor still serves older
   keepers, as long as no archiver exists);
2. all the keepers are upgraded next;
3. only when every keeper runs the new version is the archiver created.

Procedure
---------

1. **Upgrade the monitor.** Install the new packages on the monitor host,
   restart the monitor's Postgres so that it loads the new
   ``pgautofailover`` shared library, then update the extension::

     $ pg_autoctl stop --pgdata /path/to/monitor      # or restart the service
     $ # install the new packages, then start the monitor again
     $ psql -d pg_auto_failover -c 'ALTER EXTENSION pgautofailover UPDATE'
     $ psql -d pg_auto_failover -c \
         "SELECT extversion FROM pg_extension WHERE extname = 'pgautofailover'"
      extversion
     ------------
      2.3

   Existing nodes keep their rows; the new columns take their defaults
   (for example ``region`` is ``default``).

2. **Upgrade the keepers, one node at a time.** For each node, install the
   new packages and restart ``pg_autoctl`` (``pg_autoctl`` restarts Postgres
   only when needed). Do the standbys first, waiting for each to be
   ``secondary`` again (``pg_autoctl show state``) before moving on. Then
   move the primary role away with ``pg_autoctl perform switchover`` and
   upgrade the former primary last, so that there is always a healthy node
   serving.

3. **Verify that every keeper runs the new version**, on every host::

     $ pg_autoctl version
     $ pg_autoctl show state

   and that all nodes report the expected state (``primary`` and
   ``secondary``, none in ``catchingup`` or ``report_lsn``). Do not go on
   while any keeper still runs the old version.

4. **Create the archiver** (or attach an existing one)::

     $ pg_autoctl create archiver \
         --pgdata /var/lib/pgaf/archiver1 \
         --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
         --hostname archiver1.example.com \
         --replication-password '...' \
         --run

   or, for an archiver already registered, ``pg_autoctl archiver formation
   add --monitor ... --name archiver1 --formation default``.

Authentication requirement
--------------------------

The archiver's default access rule is::

  hostssl  all  pgautofailover_replicator  monitor  scram-sha-256

which admits the nodes the monitor lists, over TLS, with the replication
password. Every node in the formation, and the archiver, must therefore use
the *same* replication password (``--replication-password`` given to
``pg_autoctl create postgres`` and to ``pg_autoctl create archiver``); a node
without one is refused. If you cannot change the nodes, replace the line in
``<archiver pgdata>/archiver-hba.conf`` (no restart needed, the file is
read for each connection) with::

  hostssl  all  pgautofailover_replicator  monitor  trust

``trust`` admits any host listed by the monitor without a password; keep
TLS (``hostssl``) and network-level restrictions in place.

Rollback: what happens if the archiver is attached too early
------------------------------------------------------------

If an archiver is attached while some keeper still runs the old version, that
keeper fails to parse the node list or the ``archiving`` state and stops
making progress with the monitor: its health checks fail and, if it is the
primary, a failover may follow. To roll back:

1. detach the archiver: ``pg_autoctl archiver formation remove --monitor ...
   --name archiver1 --formation default``. This drops its rows in the
   formation and its replication slot; the old keepers recover on their next
   ``node_active`` call.
2. finish upgrading the keepers (step 2 and 3 above), then attach the
   archiver again.

The monitor extension itself is not rolled back: ``ALTER EXTENSION UPDATE``
to 2.3 is compatible with older keepers as long as no archiver exists.

Verification checklist
----------------------

::

  monitor$ psql -d pg_auto_failover -Atc \
      "SELECT extversion FROM pg_extension WHERE extname = 'pgautofailover'"
  node$    pg_autoctl show state
  node$    pg_autoctl version

The archiver must reach the ``archiving`` state, and
``SELECT pgautofailover.wal_archived('default', 0, '<segment>')`` becomes
true for newly closed WAL segments (see :ref:`archiving_operations`).
