.. _pgaftest:

pgaftest — Integration Test Framework
======================================

``pgaftest`` is the integration-test binary for pg_auto_failover.  It reads
``.pgaf`` specification files that describe a cluster topology and a sequence
of named test steps, then drives a Docker Compose stack to verify the cluster
behaves correctly.

The same specification file works in two modes:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Command
     - Effect
   * - ``pgaftest run spec.pgaf``
     - Headless CI: start → setup → sequence → teardown → TAP output + exit code
   * - ``pgaftest setup spec.pgaf``
     - Interactive: start cluster, run setup, then drop into a shell or tmux session

Quick start
-----------

.. code-block:: bash

   # Build the pg_auto_failover Docker image once
   docker build -t pg_auto_failover:pg17 .

   # Run a spec file headlessly
   PGAF_IMAGE=pg_auto_failover:pg17 pgaftest run tests/tap/specs/basic_operation.pgaf

   # Start a cluster interactively
   PGAF_IMAGE=pg_auto_failover:pg17 pgaftest setup tests/tap/specs/basic_operation.pgaf

   # Run one named step against the live cluster
   # (work dir is auto-derived: $TMPDIR/pgaftest/basic_operation)
   pgaftest step fail_primary tests/tap/specs/basic_operation.pgaf

   # Tear down
   pgaftest down tests/tap/specs/basic_operation.pgaf

.. _pgaftest_spec:

Spec file format
----------------

A ``.pgaf`` file contains four kinds of top-level blocks:

1. ``cluster { }`` — topology declaration
2. ``setup { }`` — commands to run after ``docker compose up``
3. ``teardown { }`` — commands to run at the end of a run (or on ``pgaftest down``)
4. ``step <name> { }`` — named test step
5. ``sequence`` — ordered list of step names for CI mode

cluster { }
~~~~~~~~~~~

The ``cluster`` block describes the Docker Compose topology.  ``pgaftest``
generates:

* A ``docker-compose.yml`` with one service per node
* A ``pg_autoctl_node.ini`` file per node (see :ref:`node_spec_format`)
* Each container mounts its ini file at ``/etc/pgaf/node.ini`` and runs the
  same command: ``pg_autoctl node run /etc/pgaf/node.ini``

The topology follows a three-level hierarchy:
``cluster`` → ``monitor`` + ``formation(s)`` → ``nodes``

**Syntax**

.. code-block:: text

   cluster {
       # Cluster-level options (all optional)
       monitor [port N]               # expose monitor on host port; 0 = auto
       image   "pg_auto_failover:pg17"  # Docker image; "" = build from source
       ssl     self-signed             # self-signed | cert | off
       auth    trust                   # trust | md5 | scram

       # One or more formation blocks
       formation [name] [num-sync N] {
           nodename [kind] [option...]
           ...
       }
   }

**Cluster-level options**

``monitor [port N]``
  Declares the monitor service.  The optional ``port N`` exposes the
  monitor's Postgres port on host port ``N`` so the test runner can connect
  directly with libpq.  When omitted, a random free port is chosen.

``image``
  Docker image to use for every container.  Overrides the ``PGAF_IMAGE``
  environment variable.  When neither is set, a ``build:`` stanza pointing at
  the project root is used instead.

``ssl``
  SSL mode written into the ``[options]`` section of each node's ini file.
  Allowed values: ``self-signed`` (default), ``cert``, ``off``.

``auth``
  Authentication method.  Passed as ``--auth`` to ``pg_autoctl create``.
  Default: ``trust``.

**Formation block**

.. code-block:: text

   formation [name] [num-sync N] {
       ...nodes...
   }

``name``
  Formation name on the monitor.  Defaults to ``default`` when omitted.

``num-sync N``
  Sets ``number-sync-standbys`` on this formation after the cluster comes up.
  When absent, the monitor default (0) applies.

**Node lines** (inside a formation block)

.. code-block:: text

   nodename [kind] [option...]

``nodename``
  The service name — used as the Docker Compose service name and as the
  ``hostname`` in the container.

``kind`` (optional, default ``postgres``)
  * ``postgres`` — standalone Postgres node
  * ``coordinator`` — Citus coordinator
  * ``worker`` — Citus worker

Options:

* ``async`` — makes this node asynchronous (``replication_quorum = false``)
* ``candidate-priority N`` — integer 0–100; default 50.  ``0`` prevents this
  node from ever being elected primary.  Can also be written ``candidate-priority=N``.
* ``replication-quorum false`` — same as ``async``; can also be
  written ``replication-quorum=false``.
* ``group N`` — Citus group id (required for ``worker`` kind).
  Can also be written ``group=N``.
* ``no-monitor`` — standalone node: does not register with a monitor.
* ``listen`` — bind Postgres to all interfaces (``0.0.0.0``).
* ``citus-secondary`` — marks this node as a Citus secondary.
* ``citus-cluster-name NAME`` — sets the Citus cluster name.
* ``port N`` — override the default Postgres port (5432).
* ``debian-cluster NAME`` — Debian-style Postgres cluster name.
* ``ssl MODE`` — per-node SSL mode override (overrides the cluster-level ``ssl``).
* ``auth-method METHOD`` — per-node auth method override.

**Examples**

Minimal two-node HA cluster:

.. code-block:: text

   cluster {
       monitor
       formation {
           node1
           node2
       }
   }

Three-node cluster, ``node3`` is a non-candidate async standby:

.. code-block:: text

   cluster {
       monitor
       formation {
           node1
           node2
           node3  async  candidate-priority 0
       }
   }

Named formation with ``num-sync``:

.. code-block:: text

   cluster {
       monitor
       formation default num-sync 1 {
           node1
           node2
           node3
       }
   }

Citus cluster (one coordinator, two worker HA pairs in separate formations):

.. code-block:: text

   cluster {
       image "pg_auto_failover:pg17-citus"
       monitor
       formation coordinators {
           coord  coordinator
       }
       formation workers num-sync 1 {
           worker1  worker  group 1
           worker2  worker  group 1
           worker3  worker  group 2
           worker4  worker  group 2
       }
   }

Custom image and non-default auth:

.. code-block:: text

   cluster {
       image "myregistry.example.com/pgaf:latest"
       ssl   self-signed
       auth  scram-sha-256
       monitor port 15432
       formation {
           node1
           node2
       }
   }

setup { } and teardown { }
~~~~~~~~~~~~~~~~~~~~~~~~~~

These blocks contain the same DSL commands as named steps (see below).
``setup`` runs after ``docker compose up -d``, before the first step.
``teardown`` runs at the end of ``pgaftest run`` (always, even on failure)
and at the end of ``pgaftest down`` in interactive mode.

A typical setup block waits for all nodes to reach their initial states:

.. code-block:: text

   setup {
       wait until node1 state = primary   timeout 120s
       wait until node2 state = secondary timeout 120s
   }

step <name> { }
~~~~~~~~~~~~~~~

Named blocks of commands that can be run individually (``pgaftest step``) or
collectively via the ``sequence`` directive.

.. code-block:: text

   step my_step {
       <command>
       <command>
       ...
   }

sequence
~~~~~~~~

Declares the ordered list of steps to run in CI mode (``pgaftest run``).
Each step name appears on its own line or space-separated:

.. code-block:: text

   sequence
       create_table
       fail_primary
       check_failover
       restart_old_primary

Step commands (DSL reference)
------------------------------

All commands are available inside ``setup``, ``teardown``, and ``step`` blocks.

``exec <service> <command...>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Run a command inside a running container via ``docker compose exec``.

.. code-block:: text

   exec node1  pg_autoctl perform failover
   exec monitor  psql -c "SELECT count(*) FROM pgautofailover.node"

``wait until <node> state = <state>  [timeout Ns]``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Poll the monitor until ``<node>`` reports ``<state>``.  The timeout defaults
to 90 seconds.  Fails the current step if the timeout expires.

.. code-block:: text

   wait until node1 state = primary   timeout 120s
   wait until node2 state = secondary timeout 60s
   wait until node2 state = wait_primary

Valid state names are the pg_auto_failover FSM states: ``primary``,
``secondary``, ``wait_primary``, ``draining``, ``demoted``,
``maintenance``, ``join_primary``, ``catchingup``, etc.

``assert <node> state = <state>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Immediately check that ``<node>`` is in ``<state>`` on the monitor.  Fails
the step if the state does not match.  No polling.

.. code-block:: text

   assert node2 state = primary
   assert node1 assigned-state = secondary

``assert <node> assigned-state = <state>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Like ``assert state`` but checks the ``assigned_state`` column on the monitor
rather than ``reportedstate``.

``sql <service> { SQL }``
~~~~~~~~~~~~~~~~~~~~~~~~~

Run SQL inside ``<service>``'s container and capture the output.  Use a
multi-line ``{ ... }`` block for multi-statement SQL.

.. code-block:: text

   sql node1 {
       CREATE TABLE t1 (a int);
       INSERT INTO t1 VALUES (1), (2), (3);
   }
   sql node2 { SELECT count(*) FROM t1; }

``expect { text }``
~~~~~~~~~~~~~~~~~~~~

Compare the output of the most recent ``sql`` command against ``text``.
Fails the step when the output does not match.

Single-value form:

.. code-block:: text

   sql node2 { SELECT count(*) FROM t1; }
   expect { 3 }

Multi-line form (matches ``psql --tuples-only --no-align`` output, one row per line):

.. code-block:: text

   sql node1 { SELECT a, b FROM t ORDER BY a; }
   expect {
       1	hello
       2	world
   }

Inline tuple form — each ``{ value }`` group corresponds to one output row.
This is equivalent to the multi-line form above.

.. code-block:: text

   sql node1 { SELECT count(*) FROM t GROUP BY status; }
   expect { { 2 } { 5 } }

``network disconnect <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Disconnect ``<node>``'s container from the Docker network, simulating a
network partition.

.. code-block:: text

   network disconnect node1

``network connect <node>``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Reconnect a previously disconnected container.

.. code-block:: text

   network connect node1

``sleep Ns``
~~~~~~~~~~~~~

Wait ``N`` seconds (for example ``sleep 5s`` or ``sleep 30s``).

``compose down``
~~~~~~~~~~~~~~~~

Run ``docker compose down --volumes``.  Used inside ``teardown { }`` to clean
up after a test run.

.. code-block:: text

   teardown {
       compose down
   }

TAP output
----------

In CI mode (``pgaftest run``) the runner emits
`TAP <https://testanything.org>`_ (Test Anything Protocol) output to stdout.
One TAP line is emitted per step:

.. code-block:: text

   1..5
   ok 1 - create_table
   ok 2 - fail_primary
   not ok 3 - check_failover
   # FAIL: wait until node2 state = primary: timed out after 120s (current: demoted)
   ok 4 - restart_old_primary
   ok 5 - verify_replication

Each failing step logs a ``# FAIL: …`` diagnostic line with the exact command
that failed.  Standard TAP consumers (``prove``, GitHub Actions, Jenkins) can
parse this output directly.

The exit code is ``0`` when all steps passed and non-zero otherwise.

Complete example — basic failover test
---------------------------------------

The following is a complete spec file that creates a two-node cluster,
verifies replication, triggers a failover, and checks recovery.

.. code-block:: text

   # tests/tap/specs/basic_operation.pgaf
   #
   # Two-node HA: create primary + secondary, test failover and recovery.

   cluster {
       monitor
       formation {
           node1
           node2
       }
   }

   setup {
       wait until node1 state = primary   timeout 120s
       wait until node2 state = secondary timeout 120s
   }

   teardown {
       compose down
   }

   # ---------------------------------------------------------------
   # Verify that data written on the primary appears on the standby.
   # ---------------------------------------------------------------

   step create_table {
       sql node1 {
           CREATE TABLE t1(a int);
           INSERT INTO t1 VALUES (1), (2), (3);
       }
       sql node2 { SELECT count(*) FROM t1; }
       expect { 3 }
   }

   # ---------------------------------------------------------------
   # Trigger a failover by stopping the primary's pg_autoctl process.
   # ---------------------------------------------------------------

   step fail_primary {
       exec node1  pg_autoctl stop
   }

   step check_failover {
       wait until node2 state = wait_primary  timeout 120s
       wait until node2 state = primary       timeout 120s
       assert node2 state = primary
   }

   step restart_old_primary {
       exec node1  pg_autoctl node run /etc/pgaf/node.ini
       wait until node1 state = secondary  timeout 120s
   }

   step verify_replication {
       sql node2 {
           INSERT INTO t1 VALUES (4), (5);
       }
       sql node1 { SELECT count(*) FROM t1; }
       expect { 5 }
   }

   sequence
       create_table
       fail_primary
       check_failover
       restart_old_primary
       verify_replication

Running the test suite
----------------------

A ``schedule`` file lists the spec files to run, one per line:

.. code-block:: text

   # tests/tap/schedule
   tests/tap/specs/basic_operation.pgaf
   tests/tap/specs/multi_standbys.pgaf

Run all specs in the schedule:

.. code-block:: bash

   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest run --schedule tests/tap/schedule

Run a single spec file:

.. code-block:: bash

   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest run tests/tap/specs/basic_operation.pgaf

Interactive session (useful for debugging):

.. code-block:: bash

   # Start the cluster and drop into a shell
   PGAF_IMAGE=pg_auto_failover:pg17 \
     pgaftest setup tests/tap/specs/basic_operation.pgaf

   # In a separate terminal, run individual steps
   # (work dir auto-derived to $TMPDIR/pgaftest/basic_operation)
   pgaftest step create_table   tests/tap/specs/basic_operation.pgaf
   pgaftest step fail_primary   tests/tap/specs/basic_operation.pgaf
   pgaftest step check_failover tests/tap/specs/basic_operation.pgaf

   # Tear down when done
   pgaftest down tests/tap/specs/basic_operation.pgaf

Environment variables
---------------------

``PGAF_IMAGE``
  Docker image tag to use for all containers.  Overrides the ``image``
  directive in the ``cluster { }`` block.  When neither is set, ``pgaftest``
  uses a ``build:`` stanza pointing at the project root, which triggers a
  local Docker build.

  .. code-block:: bash

     export PGAF_IMAGE=pg_auto_failover:pg17

How pgaftest and pg_autoctl node relate
----------------------------------------

``pgaftest`` uses ``pg_autoctl node run /etc/pgaf/node.ini`` as the container
command, which means every container benefits from the :ref:`live
reconfiguration <pg_autoctl_node>` feature: editing a node's ini file
on the host and saving it causes the running supervisor inside the container
to converge the changed settings within seconds (inotify) or within
``NODESPEC_WATCH_INTERVAL_SECS`` (10 s, on platforms without inotify).

This is particularly useful during interactive sessions.  For example, to
change ``node2``'s candidate priority without restarting the container:

.. code-block:: bash

   # Edit the generated ini file in the work directory
   # (auto-derived from the spec name, e.g. $TMPDIR/pgaftest/basic_operation)
   WORKDIR=${TMPDIR:-/tmp}/pgaftest/basic_operation
   sed -i 's/candidate_priority = 50/candidate_priority = 0/' \
       $WORKDIR/node2.ini

   # The supervisor inside node2 picks up the change automatically.
   # Verify on the monitor:
   docker compose exec monitor \
     pg_autoctl get node candidate-priority --pgdata /var/lib/postgres/pgaf

See also
--------

* :ref:`pg_autoctl_node` — the ``pg_autoctl node run`` command and node spec
  file format used by every container that ``pgaftest`` starts
* :ref:`pg_autoctl_create_postgres`
* :ref:`pg_autoctl_perform_failover`
