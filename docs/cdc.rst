.. _cdc_continuity:

Change Data Capture (CDC) Continuity
=====================================

pg_auto_failover manages **physical** replication slots for its own
primary/standby topology: that part of the design has always been there,
and is covered in :ref:`fault_tolerance`. This page is about **logical**
replication slots, the kind used by Change Data Capture (CDC) tools such as
`Debezium <https://debezium.io/>`_, ``pg_logical``, or a hand-rolled
consumer built on ``pgoutput`` / ``wal2json``.

Historically, a logical replication slot was tied to the single Postgres
instance that created it: if that instance was a pg_auto_failover primary
and a failover promoted one of its standbys, the slot simply didn't exist
on the new primary, and the CDC consumer had to be recreated from scratch
(losing its position, and usually requiring a full resync of the
downstream system). This was discussed at length in `issue #400`__ and
`issue #1030`__, back when Postgres itself had no mechanism to keep a
logical slot in sync on a standby.

__ https://github.com/hapostgres/pg_auto_failover/issues/400
__ https://github.com/hapostgres/pg_auto_failover/issues/1030

Postgres 17 changed this by adding **failover-enabled logical replication
slots**: a slot created with ``failover => true`` gets its position
mirrored to physical standbys by a dedicated background worker (the
*slot-sync worker*), gated by the ``synchronized_standby_slots`` setting on
the primary. When a standby holding such a synced slot gets promoted,
Postgres lets it be used for logical decoding immediately, with its
existing position intact -- no resync, no special handling required from
whoever is managing the promotion. (The slot's ``synced`` column stays
``true`` as a permanent record of its provenance; it is not reset on
promotion, and isn't the thing that governs whether the slot is usable.)

pg_auto_failover takes advantage of this, automatically, on every node
running Postgres 17 or more recent:

 - **`dbname` in `primary_conninfo`**: the slot-sync worker refuses to
   start at all unless the standby's ``primary_conninfo`` carries a
   ``dbname`` keyword. pg_auto_failover has included it since the PG17/18/19
   compatibility work landed.

 - **`sync_replication_slots` and `hot_standby_feedback`**: both are part
   of the default settings pg_auto_failover writes to every 17+ node's
   configuration, so the slot-sync worker is running on every standby
   without any manual setup. (``hot_standby_feedback`` was already a
   pg_auto_failover default before 17, for unrelated reasons; 17 only adds
   ``sync_replication_slots``.)

 - **`synchronized_standby_slots` on the primary**, computed by the monitor
   and kept in sync automatically as the formation's topology changes. This
   is the setting that makes the *continuity* guarantee hold: it lists the
   physical replication slot names of every current failover candidate
   (every node with a non-zero candidate priority), so a failover-enabled
   logical slot is only allowed to advance once every node that might get
   promoted has already replayed that far. Whichever standby ends up
   promoted, it is guaranteed to already have everything the logical slot
   had confirmed. You can see the computed value at any time with::

       pg_autoctl show settings

   which includes a ``synchronized_standby_slots`` row next to the
   existing ``synchronous_standby_names`` one.

 - **Cleanup of stale slots after an interrupted demotion**: a node that
   was briefly primary before being demoted could otherwise be left
   holding on to its old, no-longer-synced failover slots, which blocks the
   slot-sync worker from ever starting on it (``same name slot ... already
   exists on the standby``). pg_auto_failover now drops those leftover
   slots as part of the same cleanup step that already resets physical slot
   state after a demotion.

What you still need to do yourself
-----------------------------------

pg_auto_failover does not create, drop, or otherwise manage logical
replication slots: that remains entirely your CDC tool's responsibility,
exactly as it would be on a Postgres cluster not managed by
pg_auto_failover. Concretely:

 1. ``wal_level`` is ``logical`` by default on every node pg_auto_failover
    creates (see :ref:`wal_level_default` below) -- there is nothing to do
    here for a new cluster. If you're turning CDC on for an
    *already-running* cluster that was created with the default, or with
    ``--wal-level replica``, see :ref:`wal_level_rollout` for how to change
    it safely: ``wal_level`` is ``PGC_POSTMASTER`` (Postgres won't let you
    change it without a full server restart), and **every** node needs it,
    not just the current primary -- any node could be promoted later, and a
    node's own ``wal_level`` governs whether it's allowed to decode
    logically at all, independently of what the primary that originally
    wrote the WAL had configured.

 2. Create your logical replication slot(s) with ``failover => true``, for
    example::

        SELECT pg_create_logical_replication_slot(
            'debezium', 'pgoutput', false, false, true);

    or, if you're using ``CREATE SUBSCRIPTION`` on a logical subscriber::

        CREATE SUBSCRIPTION ...
          WITH (failover = true, create_slot = true, slot_name = '...');

 3. Point your CDC consumer at the formation the way you would point any
    client application: through the primary. If you use pg_auto_failover's
    own application-facing connection string machinery, a failover already
    redirects regular clients transparently; the same connection logic
    applies to your CDC consumer's connection to the (new) primary.

.. _wal_level_default:

Defaults and opting out
------------------------

Every node pg_auto_failover creates (``pg_autoctl create postgres``,
``create monitor``, ``create coordinator``, ``create worker``) sets
``wal_level = logical`` by default -- the cost (extra WAL volume, catalog
``xmin`` retention pressure) is paid whether or not you ever create a
slot, so if you know you'll never need CDC on a given cluster, you can
opt back out at creation time::

  pg_autoctl create postgres --wal-level replica ...

The flag only takes effect at node creation, before Postgres's first
start: ``wal_level`` can't be changed by editing the config and reloading
on a running node (see :ref:`wal_level_rollout` for that case). There is
no supported way to go the other direction (``replica`` back up to
``logical``) except through that same rollout procedure, applied to a
cluster that opted out at creation time.

.. _wal_level_rollout:

Turning on ``wal_level = logical`` on an already-running cluster
-------------------------------------------------------------------

Because ``wal_level`` is ``PGC_POSTMASTER``, there's no way around a full
Postgres restart on every node -- but pg_auto_failover already has the
primitives to do that fleet-wide with zero *unplanned* downtime, using
exactly the same maintenance-mode mechanism documented in
:ref:`upgrading_from_previous_versions`, which solves the identical
problem -- "a change needs a restart on every node, without triggering a
spurious failover" -- for a different cause (a pg_auto_failover version
upgrade rather than a GUC change). The one planned switchover below costs
the same single, short write-unavailability window as any manual
switchover with this tool, nothing more.

For a formation with one primary and one or more standbys:

 1. For each **standby**, one at a time:

    a. ``pg_autoctl enable maintenance`` -- safe: the monitor switches the
       primary to asynchronous replication and the standby is no longer a
       promotion candidate while in this state. pg_autoctl's own Postgres
       controller also steps out of the way for the duration, precisely so
       that manually stopping/starting Postgres on this node doesn't race
       it.
    b. Apply the change, either directly in ``postgresql.conf`` or via
       ``ALTER SYSTEM SET wal_level = 'logical';`` (both are safe:
       pg_auto_failover's own managed settings live in a separate included
       file, so this never collides with anything it writes).
    c. Stop Postgres on that node (``pg_ctl stop``, your init system, or
       however you'd stop it for a kernel/Postgres point release) -- no
       need to start it back up yourself, the next step does that.
    d. ``pg_autoctl disable maintenance`` to let it rejoin: this starts
       Postgres again as part of bringing the node back, so it comes up
       already running with the new ``wal_level``.

 2. Once **every standby** has been upgraded, promote one of them and
    demote the old primary in one controlled step::

      pg_autoctl enable maintenance --allow-failover

    This runs the same promotion path as a normal switchover and leaves
    the old primary in ``maintenance`` state as a standby -- this is the
    one and only planned write-unavailability window in the whole
    procedure.

 3. Apply steps 1b-1d to the now-demoted former primary, then
    ``pg_autoctl disable maintenance`` to bring it back as a regular
    standby.

At the end, every node has ``wal_level = logical`` and the formation is
back to its original primary/standby layout (or not, if you'd rather
leave the promoted standby as the new primary -- nothing above requires
failing back).

How this compares to other HA tools
------------------------------------

Postgres 17's native slot-sync mechanism is new enough (2024) that most HA
tooling is still catching up. As of writing, only a handful of tools
compute ``synchronized_standby_slots`` automatically instead of leaving it
to the operator to set and maintain by hand:

================== ========================== ============================== ========================= ===============================
Tool               Computes the setting?       Derived from                   Manages slot-sync GUCs?   Handles stale slot on demotion?
================== ========================== ============================== ========================= ===============================
pg_auto_failover   Yes                         failover candidates            Yes, on by default         Yes (this work, issue #1188)
Patroni            Yes (merged, unreleased)    synchronous standbys            No (left to the user)     Not addressed
CloudNativePG      Yes (opt-in)                all HA standbys                 Partial (user must enable) No -- open issue
EDB PGD            No (own Raft mechanism)     n/a                             n/a                        n/a
repmgr             No                          n/a                             n/a                        n/a
================== ========================== ============================== ========================= ===============================

A few points worth calling out:

 - Patroni's equivalent (`patroni/patroni#3527
   <https://github.com/patroni/patroni/pull/3527>`_) derives the slot list
   from the cluster's *synchronous* standbys rather than its failover
   *candidates* -- the two sets aren't always the same, and Patroni's
   feature is inert in an async-only cluster. It's also opt-in behind
   ``synchronous_mode`` and, as of this writing, merged but not in a
   released Patroni version yet.

 - CloudNativePG computes the slot list but still requires the operator to
   turn on ``sync_replication_slots`` themselves, and has an open issue
   (`cloudnative-pg#9969
   <https://github.com/cloudnative-pg/cloudnative-pg/issues/9969>`_) for
   exactly the stale-slot-after-demotion problem this PR's issue #1188 fix
   addresses -- the same underlying Postgres interaction, independently
   discovered in a different project.

 - Neither tool -- nor core Postgres -- offers a quorum/"N of M" knob for
   ``synchronized_standby_slots``; see the first bullet under
   *Limitations* below.

 - EDB Postgres Distributed (PGD/BDR) solves a different, larger problem
   (any node in a multi-master cluster can consume the slot, via its own
   Raft-based coordination) and is not a direct comparison; it layers its
   own ``failover_slot_scope`` on top of, and independently from, the
   native PG17 mechanism.

Limitations
-----------

 - This only applies to Postgres 17 and more recent, on every node of the
   formation. Mixed-version formations (e.g. during a rolling major-version
   upgrade) get no continuity guarantee until every node involved in a
   potential promotion is on 17+.

 - ``synchronized_standby_slots`` only ever lists nodes with a non-zero
   candidate priority. A node with ``candidate_priority = 0`` can still be
   promoted manually (a forced failover), and in that case the usual
   Postgres trade-off applies: a logical slot's consumer may need to
   tolerate replaying already-seen changes, or, in the worst case, be
   recreated.

 - Citus formations are not a primary focus of this feature: Citus already
   sets ``wal_level = logical`` for its own metadata synchronization needs,
   but CDC continuity for Citus worker/coordinator logical slots has not
   been specifically tested. See :ref:`cdc_continuity_citus` below for why
   Citus needs more than this feature to give CDC consumers a coherent
   single stream, and is intentionally out of scope here.

 - **No "N of M" / per-candidate opt-out.** ``synchronized_standby_slots``
   lists *every* current failover candidate unconditionally: Postgres
   itself blocks a failover-enabled logical slot from advancing until
   *all* of them confirm, with no quorum knob. A permanently lagging or
   unreachable candidate therefore permanently stalls logical decoding
   (and piles up WAL on the primary) until it catches up, is dropped as a
   candidate, or is promoted. This is a known, documented trade-off of the
   underlying Postgres feature, not something pg_auto_failover adds; other
   tools that compute this setting (Patroni, CloudNativePG) have the same
   constraint, since core provides no way to ask for "N of M". If this
   bites you in practice, lowering the affected node's candidate priority
   to ``0`` removes it from the list (at the cost of no longer being an
   automatic failover candidate).

 - **A node flip-flopping back to primary can lose its own
   originally-created slot.** If the *same* node that originally created a
   ``failover => true`` slot is demoted, then later promoted back to
   primary a second time, Postgres's slot-sync worker can get stuck
   indefinitely with ``could not synchronize replication slot "...":
   Synchronization could lead to data loss`` (comparing the node's own,
   already-more-advanced local WAL/catalog position against what the
   *other* node's copy of the slot had reached) -- a temporary placeholder
   slot ends up active rather than a real persistent one, and does not
   survive the second promotion. This is a Postgres-level position
   reconciliation limitation specific to round-tripping back to the exact
   original creator, not something pg_auto_failover's demotion cleanup
   causes or can resolve on its own; it was found while testing this
   feature (see ``tests/tap/specs/cdc_continuity.pgaf``) and does not
   affect the common case of failing over to a *different* node. If you
   hit it, drop and recreate the slot after the second promotion.

.. _cdc_continuity_citus:

Citus: a single merged CDC stream is future work
-------------------------------------------------

Everything above gives CDC continuity **per Postgres node** (coordinator or
worker). It does not solve the problem of giving a CDC consumer a single,
correctly-ordered stream for a *distributed* table whose rows are sharded
across several Citus worker groups, each independently managed by
pg_auto_failover and each with its own ``wal_level=logical``/failover-slot
setup.

Today, decoding a sharded table means running a separate consumer per
worker and reconciling downstream -- Citus's own guidance for Debezium-style
CDC is exactly that: one connector per worker node, leaving ordering and
cross-shard transaction reassembly to the consumer. A proper merged stream
would need, roughly:

 - **Topology awareness**: tracking ``pg_dist_shard`` / ``pg_dist_placement``
   (or the ``citus_shards`` view) to know which worker streams currently
   back a given distributed table, including across shard rebalancing.

 - **Transaction reassembly**: a cross-shard write is split into one local
   transaction per worker it touches, usually committed via two-phase
   commit under the hood. The per-worker fragments need to be correlated
   back into a single logical transaction (grouping by the 2PC global
   transaction id used in ``PREPARE TRANSACTION`` / ``COMMIT PREPARED``)
   before being handed to the consumer.

 - **Cross-worker ordering**: once transactions are reassembled, they still
   need a deterministic global order and a way to tell a consumer "nothing
   older than this point remains outstanding on any worker" -- the
   watermark / resolved-timestamp pattern used by other distributed
   change-feed systems (buffer each worker's stream, advance a merged
   watermark only once every worker has confirmed past it, then emit in
   order up to that watermark).

 - **Renumbering**: assigning a single monotonic global sequence/LSN to the
   reassembled, reordered stream so a downstream consumer gets stable,
   dedupable offsets instead of per-worker LSNs that reset independently
   on each worker's own failover.

 - **Failover resilience of the merge layer itself**: each worker's own
   slot already survives its own failover (this feature); the merge
   component additionally needs its own durable checkpoint -- one position
   per worker stream -- so it can resume correctly after its own restart,
   independently of any single worker's HA event.

This is a distinct, substantial piece of work -- effectively a standalone
Citus-aware CDC proxy/gateway sitting in front of per-worker logical
streams, not an extension of pg_auto_failover's monitor/keeper -- and is
intentionally left for a separate effort rather than bundled into this one.

See also :ref:`configuration` for the full list of settings pg_auto_failover
manages, and :ref:`fault_tolerance` for how physical replication slots are
used internally.
