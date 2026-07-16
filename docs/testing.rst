.. _testing_pgaftest:

Testing pg_auto_failover
========================

``pgaftest`` is the pg_auto_failover integration test runner. It reads
``.pgaf`` spec files that describe a cluster topology and a set of named
test steps, spins up the cluster using Docker Compose, and drives the steps
to completion.

Two modes are supported:

- **CI mode** (``pgaftest run``): headless, emits TAP output, exits non-zero on failure.
- **Interactive mode** (``pgaftest tmux``): cluster stays up; a tmux session
  gives you a live ``pg_autoctl watch`` view and a shell to step through
  scenarios by hand.

Prerequisites
-------------

- Docker (with Compose v2, i.e. ``docker compose``)
- ``pgaftest`` binary — built by ``make build-pgaftest`` or ``make build``
- Pre-built node images — ``make build`` builds ``pg_auto_failover:pg<N>`` for
  all supported Postgres versions; ``make build-pgaftest`` builds
  ``pgaf:pgaftest``

Running the test suite
----------------------

Run a single spec in CI mode::

   pgaftest run tests/tap/specs/basic_operation.pgaf

Run the full schedule across all supported Postgres versions::

   pgaftest run --schedule tests/tap/schedule

Explore network fault tolerance interactively
---------------------------------------------

This tutorial walks through the ``basic_operation.pgaf`` scenario step by
step to show how pg_auto_failover responds to node failures, network
partitions, and manual failovers.

**Start the cluster**::

   pgaftest tmux tests/tap/specs/basic_operation.pgaf

``pgaftest`` generates a ``docker-compose.yml`` from the spec's ``cluster {}``
block, brings the stack up, runs the ``setup {}`` block to wait for a healthy
primary+secondary, then launches a two-pane tmux session:

- **top pane** — ``pg_autoctl watch`` (live FSM state table, auto-refreshing)
- **bottom pane** — interactive shell inside the ``pgaftest`` service container

All commands below are typed in the **bottom pane**.

**Check cluster state**::

   pgaftest show state

**Review the test sequence**::

   pgaftest show steps

**Preview what the next step will do**::

   pgaftest show step

**Run the next step**::

   pgaftest step

Repeat ``pgaftest show state`` → ``pgaftest step`` to walk through each
scenario step. Notable moments:

- *test_007*: maintenance mode on the primary — ``pg_autoctl enable
  maintenance --allow-failover`` triggers a deliberate failover and
  the standby reaches ``wait_primary``
- *test_010*: network disconnect on node1 — the standby detects primary
  loss and enters ``wait_primary``
- *test_020*: back-to-back failovers — ``perform failover`` twice, checking
  replication slots survive each role change

**Jump to a specific step** (skip ahead or re-run)::

   pgaftest step test_010_fail_primary

**Inject a partition manually** (outside the script)::

   pgaftest network disconnect node1
   pgaftest show state
   pgaftest network connect node1

**Tear down when done**::

   pgaftest cluster down

The full spec language, environment variables, and command reference are at
:ref:`pgaftest`.
