.. _orchestrated_operations:

Orchestrated Whole-Cluster Operations (design exploration)
=============================================================

This page explores what it would take to let pg_auto_failover drive a
multi-step, multi-node change -- such as the :ref:`wal_level_rollout`
runbook -- as a single declared operation: started once, resumable,
checkable, and cancellable, rather than a sequence of commands an operator
runs and tracks by hand. It's a design exploration, not a committed
feature: nothing described here is implemented. It came out of looking for
a way to automate the ``wal_level`` rollout, but the problem is general
enough to be worth solving once, for every future case that needs it
(a rolling pg_autoctl/Postgres minor-version upgrade across a fleet, a
formation-wide SSL certificate rotation, re-balancing candidate priorities
-- anything that is "do a safe sequence of steps across several nodes, in
an order that matters").

Why this fits pg_auto_failover's existing architecture
--------------------------------------------------------

pg_auto_failover is **pull-based**, not push-based, and that invariant is
worth preserving: the monitor never opens a connection *to* a data node,
runs commands there, or otherwise reaches out. Every ``pg_autoctl`` keeper
process is the one initiating contact, on its own schedule, asking the
monitor "what's my goal state?" (``node_active()``, see
``src/monitor/node_active_protocol.c``) and reporting back once it's
reached it. The monitor's entire job is to *decide* goal states from a
declarative dispatch table (``MonitorFSM[]`` in
``src/monitor/group_state_machine.c``) and record intent; keepers are the
only thing that ever executes anything against a live Postgres.

A generalized operation has to respect that split. The step in the
``wal_level`` runbook that doesn't exist today -- "edit postgresql.conf,
restart Postgres" -- can only ever be performed by the keeper that already
owns that node's Postgres process (``service_postgres.c``,
``primary_standby.c``), never by the monitor reaching out to do it. So the
right shape is: the monitor stores *intent* ("this node should apply GUC
X=Y, requires a restart"); each keeper discovers its own pending intent
the same way it already discovers its goal state, and applies it
autonomously, in-process, using machinery it already has (it already
stops/edits/restarts Postgres routinely -- see ``fsm_transition.c``'s
maintenance and promotion paths, and ``postgres_add_default_settings()``
in ``primary_standby.c``, which is the same function the ``wal_level``
default added in this PR goes through).

What's missing is the thing *above* individual keeper intent: a way to
express "do this to every node in the group, standbys first, in a
specific order, and track where we are" -- a level pg_auto_failover
doesn't have today, sitting between the formation/group model
(``pgautofailover.formation`` / ``pgautofailover.node``) and the per-node
FSM.

Proposed shape
---------------

**Monitor schema** (new tables, alongside ``pgautofailover.node``):

.. code-block:: sql

  CREATE TYPE pgautofailover.operation_status AS ENUM
   (
      'pending', 'running', 'blocked', 'completed', 'failed', 'cancelled'
   );

  CREATE TABLE pgautofailover.operation
   (
      operationid    bigserial PRIMARY KEY,
      formationid    text NOT NULL REFERENCES pgautofailover.formation,
      groupid        int,                      -- NULL = every group in the formation
      kind           text NOT NULL,             -- 'guc_rollout', future kinds...
      parameters     jsonb NOT NULL,            -- e.g. {"guc": "wal_level", "value": "logical"}
      status         pgautofailover.operation_status NOT NULL DEFAULT 'pending',
      allow_failover bool NOT NULL DEFAULT false,
      created_by     text,
      created_at     timestamptz NOT NULL DEFAULT now(),
      started_at     timestamptz,
      completed_at   timestamptz,
      error_message  text
   );

  CREATE TABLE pgautofailover.operation_step
   (
      operationid    bigint NOT NULL REFERENCES pgautofailover.operation,
      stepno         int NOT NULL,
      nodeid         bigint NOT NULL REFERENCES pgautofailover.node,
      action         text NOT NULL,             -- 'apply_pending_guc', 'promote', ...
      status         pgautofailover.operation_status NOT NULL DEFAULT 'pending',
      started_at     timestamptz,
      completed_at   timestamptz,
      error_message  text,
      PRIMARY KEY (operationid, stepno)
   );

A per-node **pending change** column on ``pgautofailover.node`` is the
bridge to the keeper side -- the only thing a keeper needs to notice on
its own next ``node_active()`` round-trip::

  ALTER TABLE pgautofailover.node
      ADD COLUMN pending_guc_changes jsonb;

**Monitor API** (new SQL functions, mirroring ``perform_failover()`` /
``start_maintenance()``'s existing style in
``src/monitor/node_active_protocol.c`` / ``formation_metadata.c``):

- ``pgautofailover.start_operation(formation_id, kind, parameters jsonb,
  allow_failover bool default false) RETURNS bigint`` -- validates
  preconditions for the given ``kind`` (e.g. for ``guc_rollout``: the GUC
  name/value pair is well-formed, the formation/group exists), computes
  the step plan (standbys in arbitrary order, then -- if the primary also
  needs the change -- a promotion step requiring ``allow_failover``, then
  the demoted former primary), inserts the ``operation`` and
  ``operation_step`` rows, and sets ``pending_guc_changes`` on the first
  step's target node(s). Takes the same ``LockFormation()``/
  ``LockNodeGroup()`` discipline already used by ``perform_failover()`` and
  ``remove_node()``, since it mutates the same per-group state those do.

- ``pgautofailover.get_operation(operation_id)`` / ``list_operations(formation_id)``
  -- read-only status, current step, per-step history. This is what
  ``pg_autoctl show operation <id>`` / ``show operations`` would query.

- ``pgautofailover.advance_operations()`` -- the periodic sweep: for every
  ``running`` operation, check whether the current step's target node(s)
  have cleared their ``pending_guc_changes`` (keeper-reported, see below)
  or reached the expected state (e.g. ``maintenance``, or back to
  ``secondary``/``primary`` after a promotion step); if so, mark the step
  ``completed`` and set ``pending_guc_changes`` on the *next* step's
  node(s); if a step has been stuck past a configurable timeout, mark it
  ``blocked`` instead of silently waiting forever. Called the same way
  ``ProceedGroupState()`` already gets invoked opportunistically on every
  ``node_active()`` call (no new background worker needed, in contrast to
  ``health_check_worker.c``'s separate bgworker approach -- an operation
  only needs to progress when *some* node next reports in, which already
  happens on pg_auto_failover's existing health-check cadence).

- ``pgautofailover.unblock_operation(operation_id, ...)`` -- operator
  override for a ``blocked`` step: either retry it, or mark it manually
  resolved (e.g. "I restarted Postgres on that node myself, move on") and
  advance to the next step.

- ``pgautofailover.cancel_operation(operation_id)`` -- clears
  ``pending_guc_changes`` on any node whose step hasn't completed yet, and
  marks the operation ``cancelled``. Steps already applied are not
  automatically rolled back (see *Rollback* below).

**Keeper side** (``src/bin/pg_autoctl``): the keeper's existing per-tick
loop (``keeper_ensure_current_state()`` in ``keeper.c``, the same place
that already drives replication-slot maintenance and settings
application) gains one more check: if ``pending_guc_changes`` is non-empty
for this node,

- on a **standby**: apply it the same way ``postgres_add_default_settings()``
  already writes GUCs today, restart Postgres (the same stop/start
  machinery ``service_postgres.c`` already uses for its own "ensure"
  restarts), report success by clearing its own ``pending_guc_changes``
  via ``node_active()``'s existing report payload (one more field,
  alongside ``reportedLSN``/``reportedTLI``/etc. in
  ``node_active_protocol.c``);
- on the **primary**: refuse and report a benign "not yet" status *unless*
  the operation's ``allow_failover`` was set, in which case it requests a
  promotion the same way ``pg_autoctl enable maintenance
  --allow-failover`` already does today (reusing
  ``perform_failover()``), and only applies its own pending change once
  demoted.

This is deliberately the *same* sequence as the manual runbook in
:ref:`wal_level_rollout` -- the design doesn't invent new mechanics, it
automates the existing ones end to end.

**Client CLI surface** (new subcommands, following the existing
``pg_autoctl perform failover`` / ``enable maintenance`` naming):

.. code-block:: bash

   pg_autoctl perform rolling-change --formation default \
       --guc wal_level=logical [--allow-failover]

   pg_autoctl show operations [--formation default]
   pg_autoctl show operation <id>
   pg_autoctl operation unblock <id> [--step <n>]
   pg_autoctl operation cancel <id>

``perform rolling-change`` is a thin wrapper around
``start_operation()``; ``show operations``/``show operation`` wrap
``get_operation()``/``list_operations()``; the two ``operation``
subcommands wrap ``unblock_operation()``/``cancel_operation()``. None of
these need the client to stay connected or keep polling itself -- all the
state lives on the monitor, same as every other pg_auto_failover
operation, so the CLI can be re-run from anywhere, at any time, to check
or act on an in-flight operation.

Open questions
---------------

- **Rollback semantics are necessarily per-``kind``, not generic.**
  "Cancel" can always stop handing out *new* pending changes, but a step
  already applied (Postgres already restarted with the new ``wal_level``)
  can only be undone by another forward-moving change (queue
  ``wal_level=replica`` as a new pending change on the nodes that already
  flipped) -- there is no actual "undo a restart". Each ``kind`` would
  need to define both its forward step plan and its compensating one.

- **Concurrent operations on the same formation/group.** ``start_operation()``
  needs to refuse (or queue) a second operation against a group that
  already has one ``running``/``blocked`` -- otherwise two operations
  could both decide it's safe to promote at the same time. The existing
  ``LockNodeGroup()`` discipline likely extends to this directly, since
  it's the same lock ``perform_failover()`` already takes for exactly this
  class of race.

- **Permissions.** ``start_operation()``/``unblock_operation()``/
  ``cancel_operation()`` are operator actions, not something a keeper
  should ever call autonomously -- same ``autoctl`` vs. ``autoctl_node``
  role split already used for ``start_maintenance()`` vs. the keeper's own
  calls (see :ref:`operations`'s note on the ``autoctl_node`` role being
  "not allowed to perform maintenance operations").

- **Interaction with manual maintenance mode.** An operator running
  ``pg_autoctl enable maintenance`` by hand on a node that's also mid-step
  in an orchestrated operation needs a defined precedence -- probably:
  manual maintenance always wins, and the operation's own step simply
  waits (or is marked ``blocked`` once its timeout elapses), so an
  operator always retains the ability to intervene without the two
  mechanisms fighting each other.

- **Scope of "whole-cluster."** The schema above is written per-formation/
  per-group, matching how pg_auto_failover already scopes everything else
  (``synchronous_standby_names``, ``synchronized_standby_slots``,
  failover). A true multi-formation operation (e.g. a Citus cluster's
  coordinator and every worker group) would need an operation to *contain*
  other operations, one per group -- not explored further here.
