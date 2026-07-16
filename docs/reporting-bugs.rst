.. _reporting_bugs:

Reporting a Bug
===============

A good bug report for pg_auto_failover includes three things:

1. **Version information** — pg_auto_failover version (``pg_autoctl version``)
   and PostgreSQL version.
2. **Topology** — number of nodes, regions, sync/async configuration.
3. **Exact reproduction steps** — what you did, what you expected, what
   happened instead.

The gold standard: a ``.pgaf`` spec file
-----------------------------------------

If you can write a ``.pgaf`` spec that reproduces the problem, any contributor
can run it with a single command on their laptop.  A spec captures the full
cluster topology and the reproduction steps in one file, eliminating ambiguity
about setup.

::

   pgaftest run my_bug_report.pgaf

Even a partial spec — one that sets up the topology and gets as far as showing
the unexpected state — is far more useful than a prose description.

Example: issue #997 — replication stall in a 3-DC topology
-----------------------------------------------------------

**The bug.** In a three-datacenter setup where the primary (dc1) and standby
(dc2) lose connectivity while both remain reachable from the monitor (dc3),
``synchronous_standby_names`` stays set on the primary.  Every ``COMMIT``
hangs indefinitely because the primary waits for acknowledgement from a
standby it can no longer reach.

**The fix.** The monitor tracks ``replication_stall_since`` on the primary node
row.  After ``pgautofailover.replication_stall_timeout`` (default 10 s) with no
standby acknowledgement, the FSM assigns ``wait_primary``, which clears
``synchronous_standby_names`` and unblocks writes.

**The spec** (``tests/tap/specs/replication_stall_3dc.pgaf``):

.. code-block:: text

   cluster {
       monitor
       ssl off
       formation {
           node1 region dc1
           node2 region dc2  candidate-priority 50
       }
   }

   setup {
       wait until node1 state = primary    timeout 120s
       wait until node2 state = secondary  timeout 120s

       sql node1 {
           CREATE TABLE smoke (id serial PRIMARY KEY, v int);
           INSERT INTO smoke (v) VALUES (0);
       }
   }

   step test_002_cut_replication_link {
       # Sever the dc1↔dc2 link while both nodes remain reachable from monitor
       network disconnect node2
   }

   step test_003_primary_becomes_wait_primary {
       # Monitor must assign wait_primary within replication_stall_timeout (10 s)
       wait until node1 state = wait_primary  timeout 60s
       assert node1 state = wait_primary
   }

   step test_005_restore_link {
       network connect node2
       wait until node2 state = secondary  timeout 120s
       wait until node1 state = primary    timeout 120s
   }

**Run it in CI mode**::

   pgaftest run tests/tap/specs/replication_stall_3dc.pgaf

**Explore it interactively**::

   pgaftest tmux tests/tap/specs/replication_stall_3dc.pgaf

Then in the bottom pane::

   pgaftest show steps        # list all steps
   pgaftest step              # run next step
   pgaftest show state        # check FSM state after each step

What to include in a bug report
--------------------------------

When opening an issue at https://github.com/hapostgres/pg_auto_failover/issues,
include:

- pg_auto_failover version::

     pg_autoctl version

- PostgreSQL version::

     psql --version

- A ``.pgaf`` spec file, or if you cannot write one yet: a description of
  the topology (number of nodes, regions, sync/async) and the exact steps
  that trigger the problem.

- ``pgaftest run`` output (TAP lines plus any diagnostic lines starting with
  ``#``)::

     pgaftest run my_bug.pgaf 2>&1 | tee bug_report.tap

- Monitor logs::

     docker compose logs monitor

Gathering diagnostics
----------------------

From inside an interactive session (bottom tmux pane):

**Current FSM state table**::

   pgaftest show state

**Raw monitor state** (all node rows)::

   pgaftest sql monitor { SELECT * FROM pgautofailover.node; }

**Formation health**::

   pgaftest sql monitor {
       SELECT nodename, nodestate, goalstate,
              replication_quorum, candidate_priority
       FROM pgautofailover.node
       ORDER BY nodeid;
   }

**Node timeline and LSN**::

   pgaftest sql node1 {
       SELECT pg_current_wal_lsn(), pg_postmaster_start_time();
   }

See also
--------

- :ref:`pgaftest` — full spec language and command reference
- :ref:`testing_pgaftest` — interactive exploration tutorial
