.. _pg_autoctl_manual_fsm_step:

pg_autoctl manual fsm step
===========================

pg_autoctl manual fsm step - Make a state transition if instructed by the monitor

Synopsis
--------

::

  usage: pg_autoctl manual fsm step  [ --pgdata ] [ --json ] [report|advance]

  --pgdata      path to data directory
  --json        output data in the JSON format

Description
-----------

Called with no argument, ``pg_autoctl manual fsm step`` does both halves of
a step in a single, atomic call: it reports the node's current state to the
monitor, and then immediately attempts whatever transition the monitor
assigns back. This is the same thing the node-active service's own
autopilot loop does on every tick, exposed here as a one-shot command for
manual recovery.

Passing ``report`` or ``advance`` as an argument splits that combined call
into its two independently-issuable halves:

``report``
  Reports the node's current state to the monitor and persists whatever
  goal state the monitor assigns back, without attempting the transition.

``advance``
  Attempts the transition already on file (typically from an earlier
  ``report`` call) without talking to the monitor again.

Neither half re-runs the other, so observing the effect of ``advance`` on
the monitor's own view still needs a following ``report`` (or plain
``step``) call.

If the node's node-active service is currently suspended
(``PG_AUTOCTL_SUSPENDED``), it owns the keeper's FSM already, so ``step``,
``report``, and ``advance`` are all dispatched over a small Unix-domain
control socket to that running service instead of stepping the FSM from
this one-shot process, which would otherwise race the running service. See
the ``suspended`` node modifier in :ref:`pgaftest <pgaftest>`, which
starts a ``pgaftest`` node suspended so that a test spec can freeze its
FSM and advance it one transition at a time via the ``fsm step <node>`` DSL
command.

Options
-------

--pgdata

  Location of the Postgres node being managed locally. Defaults to the
  environment variable ``PGDATA``.

--json

  Output JSON formatted data. Not currently supported by this command; a
  warning is printed and plain-text output is used instead.

Examples
--------

Combined step, on a node whose goal state the monitor has just bumped to
``single``::

  $ pg_autoctl manual fsm step --pgdata node2
  catchingup ➜ single

Splitting that same transition into its two halves — first observe what
the monitor assigns, without moving::

  $ pg_autoctl manual fsm step report --pgdata node2
  catchingup ➜ single

then perform it::

  $ pg_autoctl manual fsm step advance --pgdata node2
  catchingup ➜ single

In both examples the printed pair is ``<state before this call>
➜ <state after this call>`` — for ``report`` that's the node's own,
unchanged current state on the left and the monitor's newly assigned goal
state on the right; for ``advance`` it's the state the node started this
call at on the left and the state it just transitioned to on the right.
