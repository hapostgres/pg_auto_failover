.. _archiving_operations:

Archiving
=========

This page covers the operational side of running an **archiver** node:
registering one, attaching a base-backup policy to control how it produces
and prunes base backups, watching what it's captured, and rebuilding a
node from its cache when disaster recovery is what's needed. For the
architecture and the reasoning behind archiving nodes, see
:ref:`archiving_architecture` and :ref:`archiving_fault_tolerance`; for the
``archiving`` state's exact transitions in the keeper's state machine, see
:ref:`failover_state_machine`.

Registering an archiver
------------------------

An archiver is created the same way as any other node kind, with its own
dedicated verb::

  $ pg_autoctl create archiver \
      --pgdata /var/lib/pgaf/archiver1 \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --hostname archiver1.example.com \
      --formation default \
      --run

Unlike ``pg_autoctl create postgres``, this does not initialize a
PostgreSQL data directory: ``--pgdata`` here names the archiver's local
cache directory for captured WAL segments and base backups. Once
registered, the archiver starts one ``pg_receivewal`` per group of the
formation it's attached to (every worker of a Citus formation included,
not just the coordinator) against each group's current primary, following
it across any later promotion, and reports its progress to the monitor
the same way a standby reports replication state -- see
:ref:`archiving_architecture` for the full process model.

``--formation`` may be given more than once, to attach the same archiver
to several formations right from the start::

  $ pg_autoctl create archiver \
      --pgdata /var/lib/pgaf/archiver1 \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --hostname archiver1.example.com \
      --formation default \
      --formation billing \
      --run

There is currently no command to attach an already-running archiver to a
further formation later on -- covering an additional formation, or a
worker group added to an already-attached Citus formation, requires
specifying every formation up front with a repeated ``--formation``.

``--region`` labels which data-centre or availability zone this archiver
runs in -- purely informational, shown by ``pg_autoctl watch``. More than
one archiver can be attached to the very same formation at once (each
gets its own independent capture and its own replication slot against
that formation's primary), so registering a second archiver in a
different region against the same formation is how geographically
redundant disaster-recovery coverage is set up::

  $ pg_autoctl create archiver \
      --pgdata /var/lib/pgaf/archiver-eu \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --hostname archiver-eu.example.com \
      --formation default \
      --region eu-west \
      --run

The full set of options::

  --pgdata            path to the archiver's local data/cache directory
  --pgctl             path to pg_ctl (used to locate pg_receivewal)
  --monitor           pg_auto_failover Monitor Postgres URL
  --hostname           hostname to advertise for this archiver
  --formation          formation to attach to, may be repeated
                       (default: "default")
  --region             data-centre or availability-zone label for this
                       archiver (default: "default")
  --basebackup-policy   base-backup production/retention policy to attach
                        (default: "default")
  --run                create node then run pg_autoctl service

Base-backup policies
----------------------

Every archiver produces full base backups on a schedule, and prunes older
ones, according to a **base-backup policy** attached to its formation (or
overridden per group). A formation that never attaches one of its own
falls back to the schema's built-in ``default`` policy: a base backup
every 24 hours, keeping the 3 most recent, none older than 3 days.

Create a policy from a JSON document::

  $ cat > /tmp/nightly.json <<'EOF'
  {
    "source": "replay",
    "replaymode": "volatile",
    "frequency": "6h",
    "maxcount": 3,
    "maxage": "7d",
    "onpromotion": true
  }
  EOF

  $ pg_autoctl create basebackup-policy \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --name nightly --config /tmp/nightly.json

Attach it to an archiver at creation time with
``--basebackup-policy nightly`` (see above), or to an already-running
archiver's formation with :ref:`pg_autoctl_set`::

  $ pg_autoctl set basebackup-policy \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --name nightly --config /tmp/nightly.json

Every archiver whose formation resolves to a changed policy picks up the
change on its own next tick, no restart needed. Read a policy back with::

  $ pg_autoctl show basebackup-policy \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --name nightly --json

Two fields are worth calling out:

  - ``source`` chooses whether the *next* base backup is taken ``live``
    (a real ``pg_basebackup`` against a running node) or ``replay``
    (replayed locally from already-captured WAL, at no cost to the live
    primary or any standby). An archiver's very first base backup is
    always taken live, regardless of policy, since a replay needs an
    existing backup to start from.
  - ``onpromotion``, when true, forces an extra base backup right after a
    failover or switchover, independent of ``frequency`` -- useful when a
    fresh backup taken on the new primary's timeline is worth more than
    waiting out the rest of the schedule.

See :ref:`pg_autoctl_create_basebackup_policy` for the full field
reference, and :ref:`pg_autoctl_show_basebackup_policy` /
:ref:`pg_autoctl_set_basebackup_policy` for the read and update commands.

Watching an archiver
----------------------

An archiver reports state through the same node-active protocol as every
other node, so it shows up in the usual commands::

  $ pg_autoctl show state
  $ pg_autoctl watch

alongside its captured WAL position, replication lag, and current disk
usage on its cache volume -- the same signals an operator already checks
for a standby, applied to an archiver's own job of holding onto WAL and
base backups rather than serving traffic.

Rebuilding a node from an archiver
-------------------------------------

When a node needs a fresh copy of the data -- provisioning a new standby
without adding load to the live primary, or rebuilding after every other
node in the formation was lost -- point ``pg_autoctl create postgres`` at
the archiver instead of a live node::

  $ pg_autoctl create postgres \
      --pgdata /var/lib/postgresql/data \
      --monitor postgresql://autoctl_node@monitor/pg_auto_failover \
      --formation default \
      --from-archiver

This bootstraps from the archiver's latest base backup and then catches
up using its cached WAL, the same recovery machinery ``pg_rewind``/
``pg_basebackup`` fallback already uses elsewhere in pg_auto_failover --
just sourced from the archiver's cache instead of a running node. Once
caught up, the new node joins the formation and is assigned a role by the
monitor the ordinary way.

This is also the disaster-recovery path described in
:ref:`archiving_fault_tolerance`: if the primary and every standby are
lost at once, a single surviving archiver is enough to rebuild a new
primary from scratch with ``--from-archiver``, and re-grow standbys from
there.

See also
--------

- :ref:`archiving_architecture` -- what an archiver is and where it fits
  among the other architectures
- :ref:`archiving_fault_tolerance` -- WAL capture independent of any
  standby, and rebuilding after every other node is lost
- :ref:`failover_state_machine` -- the ``archiving`` state's own
  transitions
- :ref:`pg_autoctl_create_basebackup_policy`,
  :ref:`pg_autoctl_show_basebackup_policy`,
  :ref:`pg_autoctl_set_basebackup_policy`
